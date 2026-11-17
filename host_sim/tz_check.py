#!/usr/bin/env python3
"""Verify clock_tz.c's POSIX TZ strings against the system zoneinfo.

The device's newlib has no /usr/share/zoneinfo: every selectable zone
carries a hand-written POSIX TZ string (setenv("TZ", ...)+tzset). This
script is the correctness gate for those strings: it replays every zone
of components/clock/clock_tz.c over the NEXT FIVE YEARS from the run
date (6 h steps -- only the forward window matters to the device),
compares the POSIX rule (glibc tzset, the same POSIX grammar newlib
parses) against the real zoneinfo reference, and fails on any offset or
DST-flag mismatch.

Zones whose real rule cannot be expressed in POSIX form at all (holiday-
driven Israel, floating-Sunday Fiji) live in APPROXIMATE: the script
reports their worst-case disagreement and only fails if a single
disagreement run exceeds APPROX_MAX_GAP_DAYS (a badly wrong rule still
fails).

Usage: host_sim/.venv/bin/python host_sim/tz_check.py
"""

import os
import re
import sys
import time
from datetime import datetime, timedelta, timezone
from zoneinfo import ZoneInfo

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "components", "clock", "clock_tz.c")

# Forward window only: the device's clock needs the rules to be RIGHT
# FROM NOW ON. Historical segments of a zone's rule history are often
# POSIX-inexpressible in one string (e.g. America/Vancouver switched to
# year-round UTC-7 on 2026-11-01 -- before that date it followed the
# old PST/PDT calendar, which the forward string cannot also cover).
START = datetime.now(timezone.utc)
END = START + timedelta(days=5 * 366)
# 1 h steps: a 6 h grid can entirely miss the 1 h windows of a wrong
# transition HOUR (an off-by-one /time suffix -- e.g. Lisbon's fall end
# at /3 instead of /2 was invisible on a 6 h grid).
STEP = timedelta(hours=1)

# Rules that are POSIX-inexpressible (approximations by design). Their
# disagreement vs the real calendar is REPORTED, not fatal, as long as no
# single run of disagreement exceeds the zone's bar (a badly wrong rule
# still fails). Israel's real rule is holiday-driven (the start is the
# Friday before the 2nd Friday of March -- with a Passover guard-rail,
# the end the first Sunday after Yom Kippur), so no fixed M-form can
# track it; the bar bounds how far off the approximation may drift.
# Chile sets its DST dates by annual decree: the M-form matches every
# year the checker has seen except a 7-day 2030 start exception.
APPROXIMATE = {"Asia/Jerusalem": 35, "America/Santiago": 10}


def parse_table():
    text = open(SRC, encoding="utf-8").read()
    pat = re.compile(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*(-?\d+),\s*(-?\d+)\s*\}')
    zones = []
    for m in pat.finditer(text):
        zones.append(
            {
                "name": m.group(1),
                "label": m.group(2),
                "posix": m.group(3),
                "lat_x10": int(m.group(4)),
                "lon_x10": int(m.group(5)),
            }
        )
    return zones


def posix_state(ts):
    l = time.localtime(ts)
    return l.tm_gmtoff, bool(l.tm_isdst)


def main():
    zones = parse_table()
    if not zones:
        print("FATAL: no zones parsed out of", SRC)
        return 1
    print(f"{len(zones)} zones parsed from clock_tz.c")

    names = [z["name"] for z in zones]
    labels = [z["label"] for z in zones]
    fails = []
    warns = []
    if len(set(names)) != len(names):
        fails.append("duplicate IANA names in the table")
    if len(set(labels)) != len(labels):
        fails.append("duplicate roller labels in the table")

    for z in zones:
        # Load the reference first: a bad IANA name is a hard error.
        try:
            ref = ZoneInfo(z["name"])
        except Exception as e:  # noqa: BLE001
            fails.append(f"{z['name']}: not in the system zoneinfo ({e})")
            continue
        if not (-900 <= z["lat_x10"] <= 900 and -1800 <= z["lon_x10"] <= 1800):
            fails.append(f"{z['name']}: reference city out of coordinate range")

        os.environ["TZ"] = z["posix"]
        time.tzset()

        prev = None
        run = None  # (offset_secs_wrong, days_in_run) for APPROX zones
        worst_run_days = 0
        total_bad_hours = 0
        n_bad = 0
        t = START
        while t < END:
            ts = t.timestamp()
            r = datetime.fromtimestamp(ts, ref)
            r_off, r_dst = int(r.utcoffset().total_seconds()), r.dst().total_seconds() != 0
            p_off, p_dst = posix_state(ts)
            bad = p_off != r_off or p_dst != r_dst
            if bad:
                n_bad += 1
                total_bad_hours += STEP.total_seconds() / 3600
                run = (run[0] + 1, run[1] + STEP.total_seconds() / 86400) if run else (1, STEP.total_seconds() / 86400)
                worst_run_days = max(worst_run_days, run[1])
            else:
                run = None
            t += STEP
        if n_bad:
            line = (
                f"{z['name']}: {n_bad} mismatched samples "
                f"({total_bad_hours / 24:.1f} h total, worst run {worst_run_days:.1f} d)"
            )
            if z["name"] in APPROXIMATE:
                bar = APPROXIMATE[z["name"]]
                if worst_run_days > bar:
                    fails.append(line + f" -- exceeds the {bar} d approximation bar")
                else:
                    warns.append(line + " (documented approximation)")
            else:
                fails.append(line)
        print(f"  {'OK ' if not n_bad else 'CHK'} {z['name']:<38} {z['posix']:<38} {z['label']}")

    print()
    for w in warns:
        print("WARN:", w)
    for f in fails:
        print("FAIL:", f)
    if fails:
        print(f"\n{len(fails)} zone(s) FAILED the POSIX-vs-zoneinfo check")
        return 1
    print(
        f"\nall {len(zones)} POSIX TZ strings verified "
        f"{START:%Y-%m-%d}..{END:%Y-%m-%d} (forward window)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
