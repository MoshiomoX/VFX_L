# ============================================================
# MeshCatalogRender.py  (Blender 5.x, run in background)
# Thumbnails for VFX_L/Assets/VFX/Mesh/README.md (2026-10-01).
#   blender -b --factory-startup -P Tools/MeshCatalogRender.py -- <mesh dir> <out dir>
# Every .fbx / .obj is normalised to a largest side of 2 and rendered twice
# with Workbench: <idx>_a.png (3/4 view, 25 deg up) and <idx>_b.png (top).
# <out>/stats.tsv gets verts, faces, size in Blender metres (FBX unit scale
# applied), UV presence, object count and the texture names the file refers to.
# FBX 6.1 files fail here (Blender needs 7100+); see MeshCatalogOldFbx.py.
# ============================================================
import bpy, os, sys, mathutils

argv = sys.argv[sys.argv.index("--") + 1:]
mesh_dir, out_dir = argv[0], argv[1]
os.makedirs(out_dir, exist_ok=True)

files = sorted(f for f in os.listdir(mesh_dir) if f.lower().endswith((".fbx", ".obj")))
stats = open(os.path.join(out_dir, "stats.tsv"), "w", encoding="utf-8")
stats.write("idx\tfile\tstatus\tverts\tfaces\tdx\tdy\tdz\tuv\tobjects\ttextures\n")

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = 240
    sc.render.resolution_y = 240
    sc.render.film_transparent = False
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "SINGLE"
    sh.single_color = (0.55, 0.78, 1.0)
    sh.show_backface_culling = False
    sh.show_cavity = True
    sh.show_object_outline = True
    sh.object_outline_color = (0.0, 0.0, 0.0)
    w = bpy.data.worlds.new("W")
    w.color = (0.13, 0.13, 0.15)
    sc.world = w
    return sc

def look_at(cam, target):
    d = target - cam.location
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()

for i, f in enumerate(files):
    path = os.path.join(mesh_dir, f)
    sc = reset()
    try:
        if f.lower().endswith(".obj"):
            bpy.ops.wm.obj_import(filepath=path)
        else:
            bpy.ops.import_scene.fbx(filepath=path)
    except Exception as ex:
        stats.write(f"{i}\t{f}\tFAIL {str(ex).splitlines()[0][:80]}\t\t\t\t\t\t\t\t\n")
        stats.flush()
        continue

    meshes = [o for o in sc.objects if o.type == "MESH"]
    if not meshes:
        stats.write(f"{i}\t{f}\tNO_MESH\t\t\t\t\t\t\t\t\n")
        continue
    bpy.context.view_layer.update()

    lo = mathutils.Vector((1e9, 1e9, 1e9)); hi = -lo
    verts = faces = 0
    uv = True
    texs = set()
    for o in meshes:
        verts += len(o.data.vertices); faces += len(o.data.polygons)
        if not o.data.uv_layers: uv = False
        for c in o.bound_box:
            p = o.matrix_world @ mathutils.Vector(c)
            lo = mathutils.Vector(map(min, lo, p)); hi = mathutils.Vector(map(max, hi, p))
        for slot in o.material_slots:
            m = slot.material
            if m and m.use_nodes:
                for n in m.node_tree.nodes:
                    if n.type == "TEX_IMAGE" and n.image:
                        texs.add(os.path.basename(n.image.filepath) or n.image.name)
    size = hi - lo
    center = (hi + lo) * 0.5
    s = 2.0 / max(max(size), 1e-6)

    # Normalise: parent everything to an empty, centre it, largest side = 2
    root = bpy.data.objects.new("root", None)
    sc.collection.objects.link(root)
    for o in sc.objects:
        if o is not root and o.parent is None:
            o.parent = root
    root.location = -center * s
    root.scale = (s, s, s)
    bpy.context.view_layer.update()

    cam_data = bpy.data.cameras.new("C")
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 3.0
    cam = bpy.data.objects.new("C", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam

    # Blender is Z-up: 3/4 view and straight down
    for tag, loc in (("a", (5.0, -6.5, 3.6)), ("b", (0.0, -0.001, 9.0))):
        cam.location = mathutils.Vector(loc)
        look_at(cam, mathutils.Vector((0, 0, 0)))
        sc.render.filepath = os.path.join(out_dir, f"{i:03d}_{tag}.png")
        bpy.ops.render.render(write_still=True)

    stats.write(f"{i}\t{f}\tok\t{verts}\t{faces}\t{size.x:.2f}\t{size.y:.2f}\t{size.z:.2f}\t{'uv' if uv else 'NO_UV'}\t{len(meshes)}\t{';'.join(sorted(texs))}\n")
    stats.flush()

stats.close()
print("RENDER_DONE", len(files))
