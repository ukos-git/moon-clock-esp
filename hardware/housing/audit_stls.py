#!/usr/bin/env python3
"""STL dimension audit for the universal moon-phase housing.

Re-reads the exported STLs (build output of ``build_housing.py``) and checks
the critical dimensions against ``params.json`` — the gate that catches a
Blender unit bug (meters/1000) or a broken selection (each file must
contain exactly its own object at the right scale).

Measurement basis: the faceplate is TILTED (18 deg), so world-axis spans are
only projections. Every feature radius is measured IN-FACE: for a vertex p,
``d = p - C`` (C = faceplate center), the in-plane coordinates are
``(d.u, d.w)`` and the radial coordinate ``r = hypot(d.u, d.w)``; the
out-of-plane coordinate is ``n_off = d.n``. The STL exporter writes
world-space vertices in millimetres (verified against the evaluated scene
geometry), so the audit runs on raw STL values.

Checks, per seat (tolerance 0.05 mm on every dimension; every radius is an
in-plane radius, so targets are radii — NOT doubled):
  * collar radius: max in-plane radius over all vertices == 65.8;
  * face-ring radius / seat pocket radius: max / min in-plane radius of
    the face-top-plane vertices (n_off == +3.0, the ring top annulus);
  * anti-flex screw clearance hole (r 2.6): rim vertices at the boss-top
    plane (n_off == -pd + 3.0) within 0.05 of the screw-pick in-plane
    position (knob seat: press-fit, none);
  * USB-C channel: the POCKET WALL (r pr..fr, n_off 1 mm above the floor
    top .. 0.5 mm below the top — strictly inside the cut range) has no
    vertices at the cut angle (cut away) and plenty 90 deg away (control);
  * unit sanity: every axis span in (10 mm, 300 mm) — a x1000 or /1000
    scale error fails this.
For the chassis: base OD via the X span, backstop extent from the
parameterized box, feet below Z=0, total height cap.

Exit 0 = all pass, 1 = failure list non-empty. Stdlib only.
"""

import json
import math
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOL = 0.05          # mm, dimension tolerance
FACE_TOP = 3.0      # ring top plane, n-offset from C (build_housing.py)
COLLAR_R = 65.8     # collar outer radius (build_housing.py)
HOLE_R = 2.6        # M2.5 clearance hole radius (build_housing.py)
FACE_R = 66.0       # chassis faceplate radius (build_housing.py)
FACE_HALF = 1.5     # chassis faceplate half-thickness (build_housing.py)
WIN_DEG = 4.0       # angular window for the USB-C channel check
# (the channel walls land at asin(12.5/2/65.8) = 5.45 deg, so 4 deg is clear)


def read_stl(path):
    b = Path(path).read_bytes()
    n = struct.unpack("<I", b[80:84])[0]
    verts, off = [], 84
    for _ in range(n):
        c = struct.unpack("<9f", b[off + 12: off + 48])
        off += 50
        verts.extend((c[0:3], c[3:6], c[6:9]))
    return verts


def spans(verts):
    return [max(v[k] for v in verts) - min(v[k] for v in verts) for k in range(3)]


def check(fails, label, got, want, tol=TOL):
    if abs(got - want) > tol:
        fails.append(f"{label}: {got:.3f} != {want:.3f} (+/-{tol})")


def unit_sanity(fails, name, verts):
    for ax, s in zip("XYZ", spans(verts)):
        if not (10.0 < s < 300.0):
            fails.append(f"{name}: {ax} span {s:.3f} outside (10, 300) mm — unit bug?")


def main():
    params_path = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "params.json"
    stl_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE / "stl"
    P = json.loads(params_path.read_text())
    H = P["housing"]
    tau = math.radians(H["tilt_deg"])
    C = (0.0, 5.0, 65.0)
    u = (1.0, 0.0, 0.0)
    w = (0.0, math.cos(tau), math.sin(tau))          # in-face up
    n = (0.0, -math.sin(tau), math.cos(tau))         # face normal
    fails = []

    def inp(p):
        """In-plane (pu, pw, r, ang) and n_off of vertex p relative to C."""
        d = (p[0] - C[0], p[1] - C[1], p[2] - C[2])
        no = d[0] * n[0] + d[1] * n[1] + d[2] * n[2]
        pu = d[0] * u[0] + d[1] * u[1] + d[2] * u[2]
        pw = d[0] * w[0] + d[1] * w[1] + d[2] * w[2]
        return no, pu, pw, math.hypot(pu, pw), math.atan2(pw, pu)

    # -- chassis ---------------------------------------------------------
    v = read_stl(stl_dir / "housing_chassis.stl")
    unit_sanity(fails, "chassis", v)
    check(fails, "chassis base OD (X span)", spans(v)[0], H["base_od_mm"])
    B = H["backstop_box_mm"]
    check(fails, "chassis backstop +Y extent",
          max(p[1] for p in v), B["center"][1] + B["sy"] / 2.0)
    check(fails, "chassis feet -Z", min(p[2] for p in v), -H["feet_height_mm"])
    zmax_want = C[2] + FACE_HALF * math.cos(tau) + FACE_R * math.sin(tau)
    check(fails, "chassis height (face rim)", max(p[2] for p in v), zmax_want)
    if max(p[2] for p in v) > H["height_max_mm"]:
        fails.append(f"chassis: height {max(p[2] for p in v):.3f} > cap {H['height_max_mm']}")

    for key in ("s3", "p4", "knob", "moonv1"):
        S = P["seats"][key]
        pr, pd = S["pocket_r_mm"], S["pocket_depth_mm"]
        fr = S.get("face_ring_od_mm", pr * 2 + 16) / 2.0
        v = read_stl(stl_dir / f"cart_{key}.stl")
        unit_sanity(fails, key, v)
        r_all = [inp(p)[3] for p in v]
        check(fails, f"{key}: collar radius (in-plane max r)", max(r_all), COLLAR_R)

        # face-top annulus (n_off == FACE_TOP): max r = face radius,
        # min r = pocket radius
        top_r = [inp(p)[3] for p in v if abs(inp(p)[0] - FACE_TOP) < 0.02]
        if not top_r:
            fails.append(f"{key}: no vertices at the face-top plane n_off {FACE_TOP}")
            continue
        check(fails, f"{key}: face radius (top plane)", max(top_r), fr)
        check(fails, f"{key}: pocket radius (top plane)", min(top_r), pr)

        # USB-C channel through the POCKET WALL (pr..fr) at the board height:
        # cut away at the seat angle, intact 90 deg away (control). The cut
        # direction d = u cos a0 + w sin a0 is in-face, so its face-plane
        # angle is exactly a0 (no tilt projection needed).
        if S.get("usbc"):
            a = math.radians(S["usbc"]["angle_deg"])
            # The cut spans n_off [-(pd-1.49)-0.5, 3.0+0.5] (build
            # build_housing.py: full board edge height, 0.5 past the top and
            # 0.5 into the floor). Check the band strictly INSIDE that range
            # — 1 mm above the floor top to 0.5 mm below the top — where a
            # clean cut leaves no vertices; a coplanar-edge phantom or an
            # uncut wall fails.
            h_lo = -(pd - 1.49) + 1.0
            h_hi = 3.0 - 0.5

            def wall_count(center):
                c = 0
                for p in v:
                    no, pu, pw, r, ang = inp(p)
                    if not (h_lo - 0.1 <= no <= h_hi + 0.1):
                        continue
                    if not (pr - 0.5 < r < fr + 0.5):
                        continue
                    d = abs(math.atan2(math.sin(ang - center),
                                       math.cos(ang - center)))
                    if d < math.radians(WIN_DEG):
                        c += 1
                return c

            if wall_count(a) > 0:
                fails.append(f"{key}: USB-C channel missing — pocket wall intact at the cut angle")
            if wall_count(a + math.radians(90)) == 0:
                fails.append(f"{key}: pocket wall missing 90 deg from the cut (control failed)")

        # anti-flex clearance hole at the screw pick (knob seat: none)
        if S.get("screw_pick"):
            sx, sy = S["screw_pick"]
            rim_noff = -pd + 3.0   # boss top plane (build_housing.py)
            c = sum(
                1 for p in v
                if abs(inp(p)[0] - rim_noff) < 0.03
                and abs(math.hypot(inp(p)[1] - sx, inp(p)[2] - sy) - HOLE_R) < 0.05)
            if c < 8:
                fails.append(f"{key}: screw clearance hole (r {HOLE_R}) missing "
                             f"at n_off {rim_noff:.2f} ({c} rim verts)")

    if fails:
        print("STL AUDIT FAIL:")
        for f in fails:
            print("  -", f)
        return 1
    print("STL AUDIT PASS: chassis base OD, per-seat collar/face/pocket diameters, "
          "screw holes, USB-C channels all within 0.05 mm")
    return 0


if __name__ == "__main__":
    sys.exit(main())
