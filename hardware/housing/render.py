#!/usr/bin/env python3
"""Headless renders of the universal moon-phase housing -> renders/.

MCP-agnostic companion to ``build_housing.py``: it imports the builder (whose
else-branch builds the 5-object scene + re-exports the STLs), then adds
materials, non-falloff suns + a world, a tracking camera, and renders a small
product set to ``hardware/housing/renders/`` (gitignored build output).

Lighting note: the mesh coordinates are in millimetres, but Cycles treats 1
BU = 1 m for physical falloff, so a "134 mm" part is a 134 m object to the
lighting rig. SUN lamps have no distance falloff and a WORLD gives uniform
ambient — both are scale-safe, so the rig needs no per-scale tuning.

Exit 0 = all renders written, 2 = build error.
"""

import importlib.util
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
RENDERS = HERE / "renders"


# ---------------------------------------------------------------------------
# build the scene (build_housing's else-branch runs build())
# ---------------------------------------------------------------------------

def build_scene():
    spec = importlib.util.spec_from_file_location(
        "bh", str(HERE / "build_housing.py"))
    bh = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bh)
    return bpy.data.objects


def _obj(objects, name):
    return objects[name]


# ---------------------------------------------------------------------------
# materials
# ---------------------------------------------------------------------------

def _mat(name, color, rough=0.55):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    return m


def assign_materials(objects):
    m_chassis = _mat("m_chassis", (0.72, 0.69, 0.65))
    m_cart = _mat("m_cart", (0.30, 0.30, 0.33))
    for key, m in ((None, m_chassis),
                   ("s3", m_cart), ("p4", m_cart),
                   ("knob", m_cart), ("moonv1", m_cart)):
        name = "housing_chassis" if key is None else f"cart_{key}"
        o = _obj(objects, name)
        o.data.materials.clear()
        o.data.materials.append(m)


# ---------------------------------------------------------------------------
# lighting + world (scale-safe: no falloff)
# ---------------------------------------------------------------------------

def setup_lighting():
    # world: soft neutral
    w = bpy.context.scene.world
    w.use_nodes = True
    bg = w.node_tree.nodes.get("Background")
    bg.inputs[0].default_value = (0.06, 0.06, 0.07, 1.0)
    bg.inputs[1].default_value = 1.0

    def sun(name, power, rot):
        o = bpy.data.objects.new(name, bpy.data.lights.new(name, "SUN"))
        o.data.energy = power
        o.rotation_euler = rot
        bpy.context.scene.collection.objects.link(o)
        return o

    # key: front-upper-left; fill: right; rim: back-upper
    sun("sun_key", 3.0, (math.radians(45), 0.0, math.radians(-35)))
    sun("sun_fill", 1.1, (math.radians(55), 0.0, math.radians(125)))
    sun("sun_rim", 1.4, (math.radians(-30), 0.0, math.radians(160)))


# ---------------------------------------------------------------------------
# camera
# ---------------------------------------------------------------------------

def _cam(name, target, dist, elev_deg, azim_deg, lens=50.0):
    t = Vector(target)
    az, el = math.radians(azim_deg), math.radians(elev_deg)
    d = Vector((math.cos(el) * math.cos(az),
                math.cos(el) * math.sin(az),
                math.sin(el)))
    cam_data = bpy.data.cameras.new(name)
    cam = bpy.data.objects.new(name, cam_data)
    bpy.context.scene.collection.objects.link(cam)
    cam.location = t + d * dist
    cam.data.lens = lens
    cam.data.sensor_width = 36.0
    cam.data.clip_end = 5000.0
    emp = bpy.data.objects.new(name + "_tgt", None)
    bpy.context.scene.collection.objects.link(emp)
    emp.location = t
    con = cam.constraints.new("TRACK_TO")
    con.target = emp
    con.track_axis = "TRACK_NEGATIVE_Z"
    con.up_axis = "UP_Y"
    return cam


# ---------------------------------------------------------------------------
# render set
# ---------------------------------------------------------------------------

def _set_visible(objects, visible):
    for o in objects:
        if o.type == "MESH":
            o.hide_render = o.name not in visible


def render_set(objects):
    R = RENDERS
    R.mkdir(exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    try:
        scene.cycles.device = "CPU"          # headless flatpak: no GPU driver
        scene.cycles.samples = 48
        scene.cycles.use_adaptive_sampling = True
    except Exception:
        pass
    scene.render.resolution_x = 1400
    scene.render.resolution_y = 1400
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.camera = None

    shots = [
        # (name, visible objects, camera kwargs)
        ("render_chassis", ["housing_chassis"],
         dict(target=(0, 3, 43), dist=230, elev_deg=24, azim_deg=-58)),
        ("render_hero_s3", ["housing_chassis", "cart_s3"],
         dict(target=(0, 3, 43), dist=230, elev_deg=24, azim_deg=-58)),
        ("render_hero_p4", ["housing_chassis", "cart_p4"],
         dict(target=(0, 3, 43), dist=245, elev_deg=24, azim_deg=-58)),
        ("render_cart_s3", ["cart_s3"],
         dict(target=(0, 6, 61), dist=175, elev_deg=20, azim_deg=-58)),
        ("render_cart_p4", ["cart_p4"],
         dict(target=(0, 6, 61), dist=180, elev_deg=20, azim_deg=-58)),
        ("render_cart_knob", ["cart_knob"],
         dict(target=(0, 6, 61), dist=175, elev_deg=20, azim_deg=-58)),
        ("render_cart_moonv1", ["cart_moonv1"],
         dict(target=(0, 6, 61), dist=175, elev_deg=20, azim_deg=-58)),
    ]

    written = []
    for name, visible, ck in shots:
        cam = _cam("cam_" + name, **ck)
        scene.camera = cam
        _set_visible(objects, visible)
        scene.render.filepath = str(R / (name + ".png"))
        bpy.ops.render.render(write_still=True)
        written.append(name + ".png")
        # clean up per-shot camera so the next shot is independent
        # (_cam names the objects "cam_<shot>" / "cam_<shot>_tgt")
        bpy.data.objects.remove(cam, do_unlink=True)
        bpy.data.objects.remove(bpy.data.objects["cam_" + name + "_tgt"],
                                do_unlink=True)

    return written


def main(argv):
    try:
        objects = build_scene()
        assign_materials(objects)
        setup_lighting()
        written = render_set(objects)
    except Exception as e:  # noqa: BLE001
        import traceback
        traceback.print_exc()
        print("RENDER ERROR:", e, file=sys.stderr)
        return 2
    print("WROTE:", ", ".join(written), "->", RENDERS)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []))
