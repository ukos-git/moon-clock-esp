#!/usr/bin/env python3
"""Bench demo recorder: camera + per-board serial logs, phase-aligned.

Records the demo tour (UI_DEMO_MODE / KNOB_DEMO_MODE,
docs/verification/demo-mode) with the bench C920 while capturing
timestamped serial logs from every board at once. The firmware logs
`phase=<name> cycle=<n> screen=<id>` at each phase entry (tag DEMO);
this script parses those lines from the logs and extracts one
comparison frame per (board, phase, cycle) plus an hstack montage per
(cycle, phase) -- the "do all devices look the same" evidence.

Run under the IDF python (it owns pyserial):

    source ~/esp/esp-idf/export.sh
    python3 bench/record_demo.py --minutes 12 [--boot-pulse] [--no-camera]

Board identity comes from udev, never from a hard-coded port (the
mapping changes with USB topology): 1a86:55d3 -> p4; 303a:1001 with the
bench MACs -> s3 / knob. Overrides: --map PORT=BOARD.

Camera: raw YUYV (the C920's own MJPEG encoder is the artifact source,
memory bench-camera-capture). Serial ports open PASSIVELY (dtr/rts held
low BEFORE open -- a naive open cold-boots the USB-JTAG boards);
--boot-pulse deliberately pulses RTS (EN) once at start so all boards
boot in the same second as the camera.

Output: bench/recordings/<stamp>/  video.mp4  logs/<board>.log
        frames/  compare/  timeline.csv  meta.json
"""

import argparse
import json
import os
import re
import signal
import subprocess
import sys
import threading
import time
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("pyserial missing -- run `source ~/esp/esp-idf/export.sh` first")

PHASE_RE = re.compile(r"phase=(\S+)\s+cycle=(\d+)(?:\s+screen=(\S+))?")

# Montage grouping aliases: the knob's live-now step and the clocks'
# HOME step render the same content (the current phase, events on), so
# they group as one -- otherwise the knob appears in zero montages
# (its phase names never collide with the clocks'). cycle is a
# board-local counter (different pacing per board); each frame is
# extracted at its own board's marker time, so the pair stays honest
# even when the two tours are mid-different-step.
PHASE_ALIASES = {("knob", "now"): "home"}

# udev (vendor:model[:serial-short]) -> board. Both ESP32-S3 boards are
# 303a:1001; the serial number carries the MAC (bench map 2026-11-08).
USB_MAP = [
    (("1a86", "55d3"), "p4"),
    (("303a", "1001", "<mac-s3>"), "s3"),
    (("303a", "1001", "<mac-knob>"), "knob"),
]


def udev_attrs(port):
    """(vendor, model, serial) of a /dev/ttyACM* node: the FIRST
    ATTRS{idVendor}/idProduct/serial in `udevadm info -a` output is the
    adapter's own (the node's closest parent) -- that is the identity
    the bench map keys on (the P4's CH9102 reports a non-MAC serial,
    the ESP32 USB-JTAG units report their MAC)."""
    try:
        out = subprocess.run(
            ["udevadm", "info", "-a", "-n", port], capture_output=True, text=True, timeout=5
        ).stdout
    except Exception:
        return ("", "", "")

    def first(key):
        m = re.search(r'ATTRS\{' + key + r'\}=="([^"]*)"', out)
        return m.group(1) if m else ""

    return (first("idVendor").lower(), first("idProduct").lower(), first("serial"))


def identify(port, overrides):
    for spec in overrides or []:
        p, b = spec.split("=", 1)
        if port.endswith(p.split("/")[-1]) or port == p:
            return b
    v, m, s = udev_attrs(port)
    for keys, board in USB_MAP:
        if (v, m) == keys[:2] and (len(keys) == 2 or keys[2].upper() == s.upper()):
            return board
    return f"unknown-{os.path.basename(port)}"


def port_held(port):
    try:
        r = subprocess.run(["fuser", port], capture_output=True, text=True)
        return bool(r.stdout.strip())
    except Exception:
        return False


class Reader(threading.Thread):
    """Passive serial reader: dtr/rts low BEFORE open (a naive open
    cold-boots the USB-JTAG boards); one host-timestamped line per
    device line into logs/<board>.log + the shared events list."""

    def __init__(self, port, board, t0, log_dir, events, lock):
        super().__init__(daemon=True)
        self.port, self.board, self.t0 = port, board, t0
        self.log_path = os.path.join(log_dir, f"{board}.log")
        self.events, self.lock = events, lock
        self.s = serial.Serial()
        self.s.port = port
        self.s.baudrate = 115200
        self.s.timeout = 0.2
        self.s.dtr = False
        self.s.rts = False
        self.stop = threading.Event()

    def run(self):
        try:
            self.s.open()
        except Exception as e:  # port vanished (a sleeping knob) -- log-only
            with open(self.log_path, "a") as f:
                f.write(f"[open failed: {e}]\n")
            return
        buf = b""
        with open(self.log_path, "a", buffering=1) as f:
            while not self.stop.is_set():
                b = self.s.read(4096)
                if not b:
                    continue
                buf += b
                while b"\n" in buf:
                    raw, buf = buf.split(b"\n", 1)
                    line = raw.decode("utf-8", "replace").rstrip("\r")
                    t = time.time() - self.t0
                    f.write(f"[{t:9.3f}] {line}\n")
                    m = PHASE_RE.search(line)
                    if m:
                        with self.lock:
                            self.events.append(
                                {
                                    "board": self.board,
                                    "phase": m.group(1),
                                    "cycle": int(m.group(2)),
                                    "screen": m.group(3),
                                    "t": t,
                                }
                            )

    def boot_pulse(self):
        """Deliberate EN reset: rts=True 0.2 s (the kcap.py recipe)."""
        try:
            self.s.rts = True
            time.sleep(0.2)
            self.s.rts = False
        except Exception:
            pass

    def close(self):
        self.stop.set()
        try:
            self.s.close()
        except Exception:
            pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--minutes", type=float, default=12.0)
    ap.add_argument("--camera", default="/dev/video0")
    ap.add_argument("--outdir", default="bench/recordings")
    ap.add_argument("--no-camera", action="store_true")
    ap.add_argument("--boot-pulse", action="store_true", help="RTS-pulse every board at start")
    ap.add_argument("--lead", type=float, default=1.5, help="s after a phase marker to grab its frame")
    ap.add_argument("--map", action="append", default=[], help="PORT=BOARD override")
    args = ap.parse_args()

    ports = sorted(
        os.path.join("/dev", p) for p in os.listdir("/dev") if p.startswith("ttyACM")
    )
    if not ports:
        sys.exit("no /dev/ttyACM* nodes -- plug the boards in (a sleeping knob needs a dial turn)")
    boards = {}
    for p in ports:
        if port_held(p):
            sys.exit(f"{p} is held by another process (fuser) -- close the peer session first")
        boards[identify(p, args.map)] = p
    print("boards:", boards)

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    out = os.path.join(args.outdir, stamp)
    for sub in ("logs", "frames", "compare"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)

    t0 = time.time()
    events, lock = [], threading.Lock()
    readers = [Reader(p, b, t0, os.path.join(out, "logs"), events, lock) for b, p in boards.items()]
    for r in readers:
        r.start()
    time.sleep(1.0)  # let the readers bind before any pulse/reset
    if args.boot_pulse:
        for r in readers:
            r.boot_pulse()
        print("boot pulse fired -- boards reset together")

    video_t0 = None
    ff = None
    if not args.no_camera:
        if not os.path.exists(args.camera):
            sys.exit(f"{args.camera} missing")
        video_path = os.path.join(out, "video.mp4")
        ff = subprocess.Popen(
            [
                "ffmpeg", "-hide_banner", "-loglevel", "error",
                "-f", "v4l2", "-input_format", "yuyv422",
                # 5 fps = the C920's raw-YUYV USB2 bandwidth ceiling at
                # 1080p (measured: 30 frames in 6 s at any requested
                # rate). Resolution wins over rate for panel comparison.
                "-video_size", "1920x1080", "-framerate", "5",
                "-i", args.camera,
                "-c:v", "libx264", "-preset", "veryfast", "-crf", "20",
                "-pix_fmt", "yuv420p", "-y", video_path,
            ],
            stderr=subprocess.DEVNULL,
        )
        video_t0 = time.time()
        print("camera recording (1080p5 yuyv422 -- C920 USB2 raw floor)")

    try:
        deadline = t0 + args.minutes * 60
        while time.time() < deadline:
            time.sleep(2)
    except KeyboardInterrupt:
        print("\ninterrupted -- finishing")
    for r in readers:
        r.close()
    if ff:
        ff.send_signal(signal.SIGINT)
        ff.wait(timeout=30)

    with open(os.path.join(out, "timeline.csv"), "w") as f:
        f.write("board,cycle,phase,screen,t\n")
        for e in events:
            f.write(f"{e['board']},{e['cycle']},{e['phase']},{e['screen'] or ''},{e['t']:.3f}\n")

    # Comparison frames + montages (only meaningful with the video).
    montages = []
    if ff and os.path.exists(video_path) and events:
        # group by (cycle, phase): one frame per board, then hstack
        groups = {}
        for e in events:
            phase = PHASE_ALIASES.get((e["board"], e["phase"]), e["phase"])
            groups.setdefault((e["cycle"], phase), {})[e["board"]] = e["t"]
        for (cycle, phase), per_board in sorted(groups.items()):
            pngs = []
            for board, t in sorted(per_board.items()):
                sec = max(0.0, t + args.lead - (video_t0 - t0))
                png = os.path.join(out, "frames", f"c{cycle}-{phase}-{board}.png")
                subprocess.run(
                    ["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", f"{sec:.2f}",
                     "-i", video_path, "-frames:v", "1", "-y", png],
                    check=False,
                )
                if os.path.exists(png):
                    pngs.append(png)
            if len(pngs) >= 2:
                m = os.path.join(out, "compare", f"c{cycle}-{phase}.png")
                cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error"]
                for p in pngs:
                    cmd += ["-i", p]
                cmd += ["-filter_complex", f"hstack=inputs={len(pngs)}", "-y", m]
                subprocess.run(cmd, check=False)
                if os.path.exists(m):
                    montages.append(os.path.relpath(m, out))

    meta = {
        "start_iso": datetime.fromtimestamp(t0).isoformat(timespec="seconds"),
        "minutes": args.minutes,
        "boards": boards,
        "video_t0_rel": (video_t0 - t0) if video_t0 else None,
        "phase_events": events,
        "montages": montages,
    }
    with open(os.path.join(out, "meta.json"), "w") as f:
        json.dump(meta, f, indent=1)
    print(f"done -> {out}  ({len(events)} phase events, {len(montages)} montages)")


if __name__ == "__main__":
    main()
