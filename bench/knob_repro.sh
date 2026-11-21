#!/usr/bin/env bash
# Passive serial watch for the knob "redraw loop" (2026-11-21): a deterministic
# boot -> crash -> reboot cycle, NOT a steady-state redraw. Root cause: the
# knob_boot task's 4096 B stack overflowed once KNOB_UI_LAST_FRAME's
# last_frame_warm() (a SPIFFS open + 259 K fread + crc32 + a whole-panel
# lv_refr_now) landed on the same tail as the NTP window (fix: stack 4096
# -> 16384, commit ff7385f).
#
# This is the red-capable feedback loop for that class of bug: PASSIVE
# capture of the knob's CDC node (dtr/rts held low so a naive open never
# cold-boots the USB-JTAG board), then assert the crash-loop symptoms.
#
#   RED   = any "Rebooting..." / "stack overflow in task" / "Guru
#           Meditation" line inside the window (the board is cycling
#           instead of staying up).
#   GREEN = the arm lands ("last frame: ... shown on glass" or the
#           "no time" caption) AND the UI is alive (touch heartbeat)
#           with zero reboots.
#
# Usage: knob_repro.sh PORT [seconds]   (seconds default 60)
#   The knob is one of TWO 303a:1001 nodes on the bench (the S3 AMOLED is
#   the other) -- identify the port by MAC / partition table (CLAUDE.md
#   "Identifying the boards") and pass it. No default: a hard-coded port
#   is the exact mistake the identification rule exists to prevent.
# Exit:  0 = GREEN, 1 = RED, 2 = usage.
if [ "$#" -lt 1 ]; then
    echo "usage: knob_repro.sh PORT [seconds]" >&2
    exit 2
fi
python3 - "${1}" "${2:-60}" <<'EOF'
import serial, sys, time

port = sys.argv[1]
dur = int(sys.argv[2])
s = serial.Serial()
s.port = port
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()
buf, t0 = [], time.time()
while time.time() - t0 < dur:
    l = s.readline().decode(errors="replace").rstrip()
    if l:
        buf.append(l)
s.close()

RED = ("Rebooting...", "stack overflow in task", "Guru Meditation")
GREEN = ("shown on glass", "no time", "touch heartbeat", "Returned from app_main()")
red = [l for l in buf if any(k in l for k in RED)]
green = [l for l in buf if any(k in l for k in GREEN)]
print(f"port={port} lines={len(buf)}  red={len(red)}  green_markers={len(green)}")
for l in red[:12]:
    print("RED   ", l)
for l in green[:6]:
    print("GREEN ", l)
sys.exit(1 if red else 0)
EOF
