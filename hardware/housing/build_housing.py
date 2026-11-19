#!/usr/bin/env python3
"""Universal moon-phase housing — parametric builder (pure bpy).

ONE script builds the whole housing from ``params.json`` (the single source
of truth for every dimension):

  * ``housing_chassis`` — the common part: base disc, rubber feet, backstop
    strut, tilted (18 deg) faceplate, and the central collar recess that the
    four seat cartridges press into.
  * ``cart_s3`` / ``cart_p4`` / ``cart_knob`` / ``cart_moonv1`` — the four
    SWAPPABLE seat cartridges. Each is a collar ring (press fit into the
    chassis recess) + web + face ring + seat pocket + USB-C through-cut +
    (anti-flex) screw boss. One cartridge at a time.

ANTI-FLEX RULE (highest-priority mechanical rule, from the knob brown-out
field history — overtightened case screws flexed the PCB / pinched ground and
pressing the display against the housing reset it): exactly ONE M2.5 screw
per seat (the knob seat is press-fit only, zero screws), printed boss with a
clearance thread, max 0.5 N.m, landing on a vendor mounting hole on a
non-display, non-battery board corner. Never screw through or clamp on the
display.

MCP-agnostic: run headless with
    blender -b -P hardware/housing/build_housing.py -- --out stl/
or drive it through the Blender MCP add-on (execute_blender_code). The script
builds into the live scene (so the MCP can render it), then writes STLs.
Mesh coordinates are in MILLIMETRES (the slicer unit); scene units are set
to mm for display.

Exit contract (headless): 0 = audit clean, 1 = hard-fail list non-empty,
2 = build error. The audit list is also returned as ``result`` when run via
the MCP.
"""

import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
DEFAULT_PARAMS = HERE / "params.json"

# ---------------------------------------------------------------------------
# param load
# ---------------------------------------------------------------------------

def load_params(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# scene setup / primitives (all coordinates in mm)
# ---------------------------------------------------------------------------

def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.curves):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def set_scene_mm():
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.length_unit = "MILLIMETERS"


def _link(obj):
    bpy.context.scene.collection.objects.link(obj)
    return obj


def cyl(name, r, p0, p1, axis, verts=96):
    """Cylinder of radius r from point p0 to p1 (mm), axis = unit Vector."""
    p0, p1 = Vector(p0), Vector(p1)
    axis = Vector(axis).normalized()
    h = (p1 - p0).length
    mesh = bpy.data.meshes.new(name)
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=verts,
                           radius1=r, radius2=r, depth=h)
    bm.to_mesh(mesh)
    bm.free()
    obj = _link(bpy.data.objects.new(name, mesh))
    # align local +Z to axis, move so the cylinder spans p0..p1
    quat = axis.to_track_quat("Z", "Y")
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = quat
    obj.location = (p0 + p1) / 2
    return obj


def box(name, center, sx, sy, sz, rot_z_deg=0.0):
    mesh = bpy.data.meshes.new(name)
    import bmesh
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for vv in bm.verts:
        vv.co.x *= sx
        vv.co.y *= sy
        vv.co.z *= sz
    bm.to_mesh(mesh)
    bm.free()
    obj = _link(bpy.data.objects.new(name, mesh))
    obj.location = Vector(center)
    obj.rotation_euler = (0.0, 0.0, math.radians(rot_z_deg))
    return obj


def boolean(target, tool, op):
    mod = target.modifiers.new("bool_" + tool.name, "BOOLEAN")
    mod.operation = op
    mod.solver = "EXACT"
    mod.object = tool
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(tool, do_unlink=True)


def union(target, *tools):
    for t in tools:
        boolean(target, t, "UNION")


def subtract(target, *tools):
    for t in tools:
        boolean(target, t, "DIFFERENCE")


# ---------------------------------------------------------------------------
# chassis
# ---------------------------------------------------------------------------

def face_frame(H):
    """Faceplate frame: center C, normal n (up-and-back), in-face axes u (x)
    and w (up). All board/seat placements live IN THIS PLANE — never in raw
    XY (a raw-XY offset misses the plane by sin(tilt) and lands the cut /
    boss ~1.8 mm off at the 45 deg P4 screw holes)."""
    tau = math.radians(H["tilt_deg"])
    n = Vector((0.0, -math.sin(tau), math.cos(tau)))
    u = Vector((1.0, 0.0, 0.0))
    w = n.cross(u)
    C = Vector((0.0, 5.0, 65.0))
    return C, n, u, w


def build_chassis(P):
    H = P["housing"]
    C, n, u, w = face_frame(H)
    rp, wp = H["base_od_mm"] / 2.0, H["base_wall_mm"]

    base = cyl("chassis_base", rp, (0, 0, 0), (0, 0, 10.0), (0, 0, 1), verts=128)
    feet = []
    for ang in (45.0, 135.0, 225.0, 315.0):
        a = math.radians(ang)
        fx, fy = 58.0 * math.cos(a), 58.0 * math.sin(a)
        feet.append(cyl("chassis_foot", H["feet_od_mm"] / 2.0,
                        (fx, fy, -H["feet_height_mm"]), (fx, fy, 0.0),
                        (0, 0, 1), verts=48))
    # backstop strut: a box that overlaps the base top AND the faceplate back
    # (verified against the tilted face plane; see the geometry notes in
    # docs/housing.rst). The box extends to y 74, beyond the base OD — by
    # design (the strut reaches up to the faceplate back at the tilt).
    B = H["backstop_box_mm"]
    backstop = box("chassis_backstop", B["center"], B["sx"], B["sy"], B["sz"])
    face = cyl("chassis_faceplate", 66.0,
               C - 1.5 * n, C + 1.5 * n, n, verts=128)
    union(base, *feet, backstop, face)
    # collar recess in the faceplate back: r 65.5, 5 mm deep
    recess = cyl("chassis_recess", 65.5, C - 6.6 * n, C - 1.4 * n, n, verts=128)
    subtract(base, recess)
    base.name = "housing_chassis"
    return base


# ---------------------------------------------------------------------------
# cartridges
# ---------------------------------------------------------------------------

def build_cartridge(P, key):
    S = P["seats"][key]
    H = P["housing"]
    C, n, u, w = face_frame(H)

    pd = S["pocket_depth_mm"]
    pr = S["pocket_r_mm"]
    fr = S.get("face_ring_od_mm", pr * 2.0 + 16.0) / 2.0  # face disc radius
    tol = P["print"]["tol_side_mm"]

    # pocket floor disc + wall annulus (one cylinder, pocket cut below)
    ring = cyl(f"cart_{key}_ring", fr,
               C - pd * n, C + 3.0 * n, n, verts=128)
    pocket = cyl(f"cart_{key}_pocket", pr,
                 C - (pd - 1.49) * n, C + 3.1 * n, n, verts=128)
    subtract(ring, pocket)

    # collar ring (press fit into the chassis recess r 65.5)
    collar = cyl(f"cart_{key}_collar", 65.8, C - 5.9 * n, C - 0.7 * n, n, verts=128)
    collar_bore = cyl(f"cart_{key}_collar_bore", 61.0,
                      C - 6.0 * n, C - 0.6 * n, n, verts=128)
    subtract(collar, collar_bore)
    union(ring, collar)

    # web annulus: collar -> ring, sitting just behind the faceplate back
    web = cyl(f"cart_{key}_web", 61.0,
              C - (pd - 1.5) * n, C - 1.6 * n, n, verts=128)
    web_bore = cyl(f"cart_{key}_web_bore", fr,
                   C - (pd - 1.4) * n, C - 1.5 * n, n, verts=128)
    subtract(web, web_bore)
    union(ring, web)

    # USB-C through-cut through the pocket wall / web / collar at the seat
    # angle. The connector protrudes RADIAL-OUT from the board edge, so the
    # channel must run from just inside the pocket bore through every wall
    # to the outside — NOT at the board-side radial coordinate, which sits
    # inside the empty bore. d/tang are IN-FACE axes (a raw-XY radial vector
    # misses the tilted plane and the cut carves air in front of the part).
    if S.get("usbc"):
        a = math.radians(S["usbc"]["angle_deg"])
        d = u * math.cos(a) + w * math.sin(a)          # in-face radial
        tang = u * (-math.sin(a)) + w * math.cos(a)    # in-face tangential
        usbc_cut_w = H["usb_c_cut_w_mm"]
        # The channel only needs to clear the POCKET WALL (pr..fr) — the web
        # and press-fit collar (r > fr) must stay a complete ring. The board
        # seats display-face at the pocket front (n_off +3.0), so center the
        # slot on the board's own thickness. (radial_mm in params is the
        # connector's board-side position — an A2-measured detail of whether
        # the physical plug reaches this slot, not of the slot geometry.)
        # The board seats on the pocket floor: back at n_off -(pd-1.49)
        # (the floor top), display face at -(pd-1.49)+board_stack. The
        # channel must clear the wall (pr..fr) across the whole board edge
        # height. Extend 0.5 mm past the top and 0.5 mm into the floor so no
        # cut face is coplanar with a wall face — the EXACT solver leaves a
        # coplanar top edge uncut, which reads back as a phantom "intact
        # wall" ring at the cut angle.
        floor_top = -(pd - 1.49)
        cut_lo = floor_top - 0.5
        cut_hi = 3.0 + 0.5
        cut_inner = pr - 1.0
        cut_outer = fr + 1.0
        cut = box(f"cart_{key}_usbc",
                  C + n * ((cut_lo + cut_hi) / 2.0)
                  + d * ((cut_inner + cut_outer) / 2.0),
                  cut_outer - cut_inner, usbc_cut_w, cut_hi - cut_lo)
        # orient the box: local x -> radial, local y -> tangential, local z -> n
        cut.rotation_euler = (0.0, 0.0, 0.0)
        m = _rotm(d, tang, n)
        cut.matrix_world = _matrix_at(cut.location, m)
        subtract(ring, cut)

    # anti-flex screw boss + clearance hole (the knob seat is press-fit: none)
    if S.get("screw_pick"):
        sx, sy = S["screw_pick"]
        boss_r = 3.0
        hole_r = 2.6  # M2.5 clearance
        # the board sits in the face plane: the boss lands at the screw's
        # IN-FACE position, not at a raw-XY offset (off by sy*(1-cos(tilt))
        # otherwise — ~1.8 mm at the P4's 45 deg holes)
        base_pt = C - (pd - 1.5) * n + u * sx + w * sy  # floor back plane
        boss = cyl(f"cart_{key}_boss", boss_r,
                   base_pt - 1.0 * n, base_pt + 1.5 * n, n, verts=48)
        union(ring, boss)
        hole = cyl(f"cart_{key}_hole", hole_r,
                   base_pt - 1.2 * n, base_pt + 1.7 * n, n, verts=48)
        subtract(ring, hole)

    ring.name = f"cart_{key}"
    return ring


def _rotm(d, t, n):
    from mathutils import Matrix
    M = Matrix((
        (d.x, t.x, n.x),
        (d.y, t.y, n.y),
        (d.z, t.z, n.z),
    ))
    return M


def _matrix_at(loc, rot):
    from mathutils import Matrix
    return Matrix.Translation(Vector(loc)) @ rot.to_4x4()


# ---------------------------------------------------------------------------
# audit
# ---------------------------------------------------------------------------

def audit(P):
    """Hard-fail list: empty == clean. Dimension + keep-out checks."""
    fails = []
    H = P["housing"]
    for key in ("s3", "p4", "knob", "moonv1"):
        S = P["seats"][key]
        pr, pd = S["pocket_r_mm"], S["pocket_depth_mm"]
        od = S["board_od_mm"] if key != "knob" else S["case_od_mm"]
        want_r = od / 2.0
        if abs(pr - (want_r + P["print"]["tol_side_mm"])) > 0.05:
            fails.append(f"{key}: pocket_r {pr} != board/2 {want_r} + tol")
        # screw landing keep-outs: never closer than 2 mm to the pocket edge;
        # never inside the display circle ON A SELF-DESIGNED BOARD (moonv1),
        # where we control the layout. On vendor boards (s3/p4) the holes are
        # the manufacturer's back-side mounting points — the display is a
        # separate front module, and a back-side screw at the 0.5 N.m limit
        # into a vendor hole is safe even if the hole sits inside the display
        # circle (the S3's 3 holes do).
        disp_r = (S.get("display_od_mm") or S.get("va_od_mm") or 0.0) / 2.0
        if S.get("screw_pick"):
            sx, sy = S["screw_pick"]
            rr = math.hypot(sx, sy)
            if S.get("screw_holes_source") != "vendor" and rr < disp_r + 1.0:
                fails.append(f"{key}: screw at r {rr:.2f} inside display keep-out r {disp_r + 1.0:.2f} (self-designed board)")
            if rr > pr - 2.0:
                fails.append(f"{key}: screw at r {rr:.2f} within 2 mm of pocket edge {pr}")
            # the screw hole must be one of the vendor holes
            if S.get("screw_holes") and [round(sx, 2), round(sy, 2)] not in [
                    [round(h[0], 2), round(h[1], 2)] for h in S["screw_holes"]]:
                fails.append(f"{key}: screw landing {S['screw_pick']} is not a vendor hole")
        # pocket depth must clear the board stack
        stack = S.get("board_stack_mm", 0.0)
        if pd < stack + 0.19:
            fails.append(f"{key}: pocket_depth {pd} < stack {stack} + clearance")
    # chassis fits the P4 board + wall
    if H["base_od_mm"] < 126.0 + 2 * H["base_wall_mm"]:
        fails.append(f"chassis: base OD {H['base_od_mm']} < P4 board 126 + 2*wall")
    # height cap
    if H["height_max_mm"] < 100:
        fails.append("chassis: height_max too small for the design")
    return fails


# ---------------------------------------------------------------------------
# STL export
# ---------------------------------------------------------------------------

def export_stls(out_dir):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    paths = []
    for obj in bpy.data.objects:
        if obj.type == "MESH":
            p = out_dir / (obj.name + ".stl")
            bpy.ops.object.select_all(action="DESELECT")
            obj.select_set(True)
            bpy.context.view_layer.objects.active = obj
            bpy.ops.wm.stl_export(filepath=str(p), export_selected_objects=True)
            paths.append(str(p))
    return paths


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def build(params_path=DEFAULT_PARAMS, out_dir=None):
    P = load_params(params_path)
    clear_scene()
    set_scene_mm()
    chassis = build_chassis(P)
    carts = [build_cartridge(P, k) for k in ("s3", "p4", "knob", "moonv1")]
    fails = audit(P)
    stls = export_stls(out_dir or (HERE / "stl"))
    result = {
        "objects": [chassis.name] + [c.name for c in carts],
        "audit_hard_fail": fails,
        "stl": stls,
        "params": str(params_path),
    }
    return P, result


def main(argv):
    out_dir = None
    params = DEFAULT_PARAMS
    i = 0
    while i < len(argv):
        if argv[i] == "--out":
            out_dir = argv[i + 1]; i += 2
        elif argv[i] == "--params":
            params = Path(argv[i + 1]); i += 2
        else:
            i += 1
    try:
        P, result = build(params, out_dir)
        # the CLI path also snapshots the scene as the committed
        # moon_housing.blend (the MCP path keeps the live scene instead)
        bpy.ops.wm.save_as_mainfile(filepath=str(HERE / "moon_housing.blend"))
    except Exception as e:  # noqa: BLE001
        import traceback
        traceback.print_exc()
        print("BUILD ERROR:", e, file=sys.stderr)
        return 2
    print(json.dumps(result, indent=2))
    if result["audit_hard_fail"]:
        print("AUDIT HARD-FAIL:", file=sys.stderr)
        for f in result["audit_hard_fail"]:
            print("  -", f, file=sys.stderr)
        return 1
    return 0


# When run via the MCP (exec'd, not __main__), ``build()`` is importable;
# ``__result__`` keeps the MCP path happy too.
if __name__ == "__main__":
    sys.exit(main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []))
else:
    _P, __result__ = build()  # noqa: F841
