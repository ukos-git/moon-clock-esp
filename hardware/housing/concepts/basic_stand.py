#!/usr/bin/env python3
"""Basic desk stand for the S3 AMOLED 1.75 (466) — concept, one print.

The simplest possible form of the housing idea: ONE round base, ONE tilted
back arm, and a full 360-degree seat ring the board sits in. Friction fit,
no screws (the anti-flex single-screw upgrade belongs to the universal
housing, not the first print). The board in the renders is a PLACEHOLDER —
it is not part of the print.

Geometry (mm, all coordinates world; the board face plane is tilted TILT
degrees back from vertical, normal n up-and-forward):
  * base   : disc r 55 x 12 + 4 rubber feet, desk plane z=0 (feet -1.5..0)
  * back   : disc r 32 x 6 behind the board (0.45 mm clearance behind the
             board back — the board leans against the ring, not the back)
  * ring   : annulus 24.68..30 (board r 24.48 + 0.20 tol), 14 mm along n —
             catches the 11.5 mm board edge, 1.25 mm per side
  * arm    : box from the base top to the back disc midplane, x-width 24
  * USB-C  : 13.5 x 5 through-notch in the ring bottom (the S3's USB-C is
             on the bottom edge, pointing radially out-down)

MCP-agnostic:  flatpak run org.blender.Blender -b -P <this file>
Writes (flatpak cannot see /tmp — keep outputs under the project tree):
  concepts/basic_stand/stl/basic_stand.stl
  concepts/basic_stand/renders/render_{hero,front,rear}.png
"""

import json
import math
from pathlib import Path

import bpy
import bmesh
from mathutils import Matrix, Vector

HERE = Path(__file__).resolve().parent
OUT = HERE / "basic_stand"

# ---------------------------------------------------------------------------
# params (S3 AMOLED 1.75 — from hardware/housing/params.json "s3")
# ---------------------------------------------------------------------------

BOARD_R = 24.48          # board OD 48.96 / 2 (vendor)
BOARD_T = 11.5           # board edge thickness (stack 10.40 + PCB)
TILT = 20.0              # degrees back from vertical (15-20 design band)
RING_IN = BOARD_R + 0.20 # 24.68 — board + side tolerance
RING_OUT = 30.0          # wall 5.3 mm (printable, stable)
RING_HALF = 7.0          # ring half-height along n (14 > board 11.5)
BACK_R = 32.0            # back disc radius (overhangs the ring)
BACK_T = 6.0
BACK_GAP = 0.45          # clearance behind the board back
BASE_R = 55.0
BASE_H = 12.0
C = Vector((0.0, 6.0, 70.0))   # board center in the ring
FOOT_R = 7.0
FOOT_H = 1.5


def frame():
    tau = math.radians(TILT)
    n = Vector((0.0, -math.sin(tau), math.cos(tau)))  # face normal (up-fwd)
    u = Vector((1.0, 0.0, 0.0))
    w = n.cross(u)                                   # in-face up
    return n, u, w


def cyl(name, r, p0, p1, axis, verts=96):
    p0, p1, axis = Vector(p0), Vector(p1), Vector(axis).normalized()
    h = (p1 - p0).length
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False,
                          segments=verts, radius1=r, radius2=r, depth=h)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = axis.to_track_quat("Z", "Y")
    obj.location = (p0 + p1) / 2
    return obj


def box(name, center, sx, sy, sz, basis=None):
    """Axis-aligned box (basis = 3 orthonormal columns mapping local x/y/z)."""
    mesh = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    for v in bm.verts:
        v.co.x *= sx
        v.co.y *= sy
        v.co.z *= sz
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    if basis is not None:
        x, y, z = basis
        obj.matrix_world = Matrix.Translation(Vector(center)) @ Matrix((
            (x.x, y.x, z.x),
            (x.y, y.y, z.y),
            (x.z, y.z, z.z),
        )).to_4x4()
    else:
        obj.location = Vector(center)
    return obj


def boolean(target, tool, op):
    mod = target.modifiers.new("b_" + tool.name, "BOOLEAN")
    mod.operation = op
    mod.solver = "EXACT"
    mod.object = tool
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(tool, do_unlink=True)


def build_stand():
    n, u, w = frame()
    parts = []
    # base + feet
    base = cyl("stand_base", BASE_R, (0, 8, 0), (0, 8, BASE_H), n, verts=128)
    for ang in (45.0, 135.0, 225.0, 315.0):
        a = math.radians(ang)
        fx, fy = 42.0 * math.cos(a), 8.0 + 42.0 * math.sin(a)  # around base
        parts.append(cyl("stand_foot", FOOT_R, (fx, fy, -FOOT_H), (fx, fy, 0.0),
                         (0, 0, 1), verts=48))
    # seat ring (annulus along n)
    ring = cyl("stand_ring", RING_OUT, C - RING_HALF * n, C + RING_HALF * n, n,
               verts=128)
    boolean(ring, cyl("stand_ring_bore", RING_IN,
                      C - (RING_HALF + 1) * n, C + (RING_HALF + 1) * n, n,
                      verts=128), "DIFFERENCE")
    # back disc (overlaps the ring's back 0.8 mm; 0.45 behind the board)
    back_c = C - (BOARD_T / 2.0 + BACK_GAP + BACK_T / 2.0) * n
    back = cyl("stand_back", BACK_R, back_c - BACK_T / 2.0 * n,
               back_c + BACK_T / 2.0 * n, n, verts=128)
    # USB-C notch in the ring bottom (in-face down = -w)
    cut_c = C - w * ((RING_IN + RING_OUT) / 2.0)
    notch = box("stand_notch", cut_c, 8.0, 13.5, 5.0,
                basis=(-w, u, n))
    boolean(ring, notch, "DIFFERENCE")
    # arm: base top -> back disc midplane (nearly vertical, slight lean)
    a0 = Vector((0.0, 14.0, 6.0))                    # inside the base
    a1 = back_c                                      # on the back midplane
    a = (a1 - a0).normalized()
    bx = u
    by = a.cross(bx).normalized()
    box_len = (a1 - a0).length + 4.0
    arm = box("stand_arm", a0 + (a1 - a0) / 2.0, 24.0, 18.0, box_len,
              basis=(bx, by, a))
    # union everything into one watertight part
    for p in parts:
        boolean(base, p, "UNION")
    boolean(base, ring, "UNION")
    boolean(base, back, "UNION")
    boolean(base, arm, "UNION")
    base.name = "basic_stand"
    return base, (n, u, w)


def build_board_placeholder(n, u, w):
    """Render-only: the S3 board sitting in the ring (not part of the print)."""
    board = cyl("ph_board", BOARD_R, C - BOARD_T / 2.0 * n,
                C + BOARD_T / 2.0 * n, n, verts=128)
    disp = cyl("ph_display", 21.9, C + 4.0 * n, C + (BOARD_T / 2.0 + 0.1) * n,
               n, verts=128)
    usbc = box("ph_usbc", C - w * (BOARD_R + 1.6), 3.6, 9.0, 11.35,
               basis=(-w, u, n))
    return board, disp, usbc


# ---------------------------------------------------------------------------
# presentation: materials, lights, cameras, render, STL
# ---------------------------------------------------------------------------

def _mat(name, color, rough):
    m = bpy.data.materials.new(name)
    m.use_nodes = True          # default Principled setup; just retune it
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = (*color, 1.0)
    b.inputs["Roughness"].default_value = rough
    return m


def materials(stand, board, disp, usbc):
    for o, c, r in ((stand, (0.74, 0.71, 0.66), 0.6),
                    (board, (0.10, 0.10, 0.12), 0.4),
                    (disp, (0.015, 0.015, 0.02), 0.12),
                    (usbc, (0.28, 0.28, 0.30), 0.5)):
        o.data.materials.clear()
        o.data.materials.append(_mat("m_" + o.name, c, r))


def lights():
    scene = bpy.context.scene
    if scene.world is None:
        scene.world = bpy.data.worlds.new("World")
    scene.world.use_nodes = True
    bg = scene.world.node_tree.nodes.get("Background")
    if bg is None:
        bg = scene.world.node_tree.nodes.new("ShaderNodeBackground")
    bg.inputs[0].default_value = (0.05, 0.05, 0.06, 1.0)
    for name, power, rot in (("sun_key", 3.2, (0.79, 0.0, -0.61)),
                             ("sun_fill", 1.1, (0.96, 0.0, 2.18)),
                             ("sun_rim", 1.4, (-0.52, 0.0, 2.79))):
        o = bpy.data.objects.new(name, bpy.data.lights.new(name, "SUN"))
        o.data.energy = power
        o.rotation_euler = rot
        scene.collection.objects.link(o)


def cam(name, target, dist, elev_deg, azim_deg):
    t = Vector(target)
    az, el = math.radians(azim_deg), math.radians(elev_deg)
    d = Vector((math.cos(el) * math.cos(az),
                math.cos(el) * math.sin(az),
                math.sin(el)))
    cd = bpy.data.cameras.new(name)
    c = bpy.data.objects.new(name, cd)
    bpy.context.scene.collection.objects.link(c)
    c.location = t + d * dist
    cd.lens = 50.0
    cd.clip_end = 5000.0
    emp = bpy.data.objects.new(name + "_tgt", None)
    bpy.context.scene.collection.objects.link(emp)
    emp.location = t
    con = c.constraints.new("TRACK_TO")
    con.target = emp
    con.track_axis = "TRACK_NEGATIVE_Z"
    con.up_axis = "UP_Y"
    return c, emp


def render_set(shots):
    R = OUT / "renders"
    R.mkdir(parents=True, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 64
    scene.cycles.use_adaptive_sampling = True
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1400
    for name, kw in shots:
        c, emp = cam("cam_" + name, **kw)
        scene.camera = c
        scene.render.filepath = str(R / (name + ".png"))
        bpy.ops.render.render(write_still=True)
        bpy.data.objects.remove(c, do_unlink=True)
        bpy.data.objects.remove(emp, do_unlink=True)
    return [s[0] + ".png" for s in shots]


def export_stl(stand):
    S = OUT / "stl"
    S.mkdir(parents=True, exist_ok=True)
    p = S / "basic_stand.stl"
    bpy.ops.object.select_all(action="DESELECT")
    stand.select_set(True)
    bpy.context.view_layer.objects.active = stand
    bpy.ops.wm.stl_export(filepath=str(p), export_selected_objects=True)
    return str(p)


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "METRIC"
    scene.unit_settings.length_unit = "MILLIMETERS"

    stand, (n, u, w) = build_stand()
    board, disp, usbc = build_board_placeholder(n, u, w)
    materials(stand, board, disp, usbc)
    lights()

    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    ev = stand.evaluated_get(dg)
    me = ev.to_mesh()
    bm = bmesh.new()
    bm.from_mesh(me)
    vol_mm3 = abs(bm.calc_volume())
    bm.free()
    vol_cm3 = vol_mm3 / 1000.0
    xs = [ (stand.matrix_world @ v.co) for v in me.vertices ]
    spans = [max(p[k] for p in xs) - min(p[k] for p in xs) for k in range(3)]
    ev.to_mesh_clear()

    stl = export_stl(stand)
    shots = [
        ("render_hero", dict(target=(0, 0, 46), dist=210,
                             elev_deg=16, azim_deg=-55)),
        ("render_front", dict(target=(0, -6, 52), dist=185,
                              elev_deg=6, azim_deg=180)),
        ("render_rear", dict(target=(0, 10, 40), dist=215,
                             elev_deg=18, azim_deg=28)),
    ]
    written = render_set(shots)
    print(json.dumps({
        "stl": stl,
        "renders": written,
        "volume_cm3": round(vol_cm3, 1),
        "petg_grams_1.24": round(vol_cm3 * 1.24, 0),
        "spans_mm": [round(s, 1) for s in spans],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
