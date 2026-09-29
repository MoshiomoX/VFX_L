# ============================================================
# BuildPlayerModel.py  (Blender 4.x / 5.x, run in background)
# Builds the player character from Quaternius CC0 packs into one FBX
# (VFX_L/Assets/Model/Quaternius_Ranger/Ranger.fbx):
#   - Universal Base Characters: Superhero_Male_FullBody (only the head + neck
#     are kept, the outfit covers the rest), eyes, eyebrows
#   - Modular Character Outfits - Fantasy (free tier): Male_Ranger outfit
#   - Hair_Beard (rigged to the head bone); beard + eyebrows are made white
#     (an old wizard look, 2026-09-28)
#   - Universal Animation Library 1 + 2: only the clips in CLIPS are kept and
#     exported as takes on the same armature (all four packs share
#     the same 65-bone skeleton, so no retargeting)
# Textures are shrunk to 1024 and embedded. The model faces +Z (PlayerFactory yaw offset 0).
#
# Usage:
#   blender -b --factory-startup -P Tools/BuildPlayerModel.py -- <packs dir> <out.fbx>
#   (<out.glb> also works, but the game's loader mis-rebuilds the GLB's bind offsets)
#   <packs dir> = folder holding the four extracted zips from quaternius.itch.io:
#     Universal Base Characters[Standard], Modular Character Outfits - Fantasy[Standard],
#     Universal Animation Library[Standard], Universal Animation Library 2[Standard]
#   Keep <packs dir> SHORT (e.g. %TEMP%\qp\ubc, \mco, \ual1, \ual2): the packs nest deep
#   and Windows' 260-char path limit makes Blender silently fail to load the outfit textures.
# ============================================================
import bpy, os, sys

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = os.path.abspath(argv[0])
OUT = os.path.abspath(argv[1])

CLIPS = [
    "Idle_Loop", "Walk_Loop", "Jog_Fwd_Loop", "Sprint_Loop",
    "Jump_Start", "Jump_Loop", "Jump_Land", "Roll",
    "Crouch_Idle_Loop", "Crouch_Fwd_Loop",
    "Spell_Simple_Enter", "Spell_Simple_Idle_Loop", "Spell_Simple_Shoot", "Spell_Simple_Exit",
    "Hit_Chest", "Hit_Head", "Hit_Knockback", "Death01",
    "Slide_Start", "Slide_Loop", "Slide_Exit",
    "NinjaJump_Start", "NinjaJump_Idle_Loop", "NinjaJump_Land",
]
KEEP_BODY_GROUPS = {"Head", "neck_01"}   # base body verts kept (dominant weight)


def find(name, must_contain=""):
    for d, _, fs in os.walk(SRC):
        if name in fs and must_contain in d:
            return os.path.join(d, name)
    raise FileNotFoundError(name + " / " + must_contain)


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def rebind(meshes, arm):
    """Parent skinned meshes to `arm` and point their armature modifier at it.
    The mesh's own transform (it came under another armature object) is baked into the
    vertices so every mesh node ends up with an identity local transform, like the base
    body's meshes. Otherwise the glTF skin's inverse bind matrices disagree with the node
    hierarchy and the game's loader rebuilds the offsets ([bind-check] worstDiff=2)."""
    for m in meshes:
        if m.type != 'MESH':
            continue
        mw = m.matrix_world.copy()
        m.parent = arm
        m.matrix_parent_inverse.identity()
        m.matrix_world = mw
        # bake the local transform into the mesh data
        local = m.matrix_basis.copy()
        m.data.transform(local)
        m.matrix_basis.identity()
        mods = [md for md in m.modifiers if md.type == 'ARMATURE']
        if not mods:
            mods = [m.modifiers.new("Armature", 'ARMATURE')]
        for md in mods:
            md.object = arm


bpy.ops.wm.read_factory_settings(use_empty=True)

# ---- base body (armature + head) ----
base = import_fbx(find("Superhero_Male_FullBody.fbx", "Unity"))
arm = next(o for o in base if o.type == 'ARMATURE')
arm.name = "Armature"
body = next(o for o in base if o.type == 'MESH' and o.name.startswith("SuperHero"))

# keep only the head / neck of the body
names = {g.index: g.name for g in body.vertex_groups}
drop = []
for v in body.data.vertices:
    best, bw = None, -1.0
    for g in v.groups:
        if g.weight > bw:
            best, bw = names.get(g.group), g.weight
    if best not in KEEP_BODY_GROUPS:
        drop.append(v.index)
bpy.context.view_layer.objects.active = body
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.select_all(action='DESELECT')
bpy.ops.object.mode_set(mode='OBJECT')
for i in drop:
    body.data.vertices[i].select = True
bpy.ops.object.mode_set(mode='EDIT')
bpy.ops.mesh.delete(type='VERT')
bpy.ops.object.mode_set(mode='OBJECT')
print("body: dropped %d verts, kept %d" % (len(drop), len(body.data.vertices)))

# ---- outfit ----
outfit = import_fbx(find("Male_Ranger.fbx", os.path.join("FBX (Unity)", "Outfits")))
rebind(outfit, arm)
for o in outfit:
    if o.type == 'ARMATURE':
        bpy.data.objects.remove(o, do_unlink=True)

# ---- beard (rigged to the head bone) ----
beard = import_fbx(find("Hair_Beard.fbx", os.path.join("Rigged to Head Bone", "FBX (Unity)")))
rebind(beard, arm)
for o in beard:
    if o.type == 'ARMATURE':
        bpy.data.objects.remove(o, do_unlink=True)
    elif o.type == 'MESH' and not o.vertex_groups.get("Head"):
        g = o.vertex_groups.new(name="Head")
        g.add([v.index for v in o.data.vertices], 1.0, 'REPLACE')

# ---- relink textures (the FBX files point at the author's machine) ----
for img in bpy.data.images:
    base_name = os.path.basename(img.filepath.replace("\\", "/")) if img.filepath else img.name
    base_name = base_name.split(".png")[0] + ".png" if ".png" in base_name else base_name
    try:
        img.filepath = find(base_name)
        img.reload()
    except FileNotFoundError:
        print("texture not found:", img.name, img.filepath)

# ---- white beard / eyebrows: a lightened grey copy of the hair texture ----
hair_img = next((i for i in bpy.data.images if i.name.startswith("T_Hair_1_BaseColor") and i.size[0] > 0), None)
if hair_img:
    white = hair_img.copy()
    white.name = "T_Hair_White"
    px = list(white.pixels)
    for i in range(0, len(px), 4):
        l = 0.299 * px[i] + 0.587 * px[i + 1] + 0.114 * px[i + 2]
        w = 1.0 - (1.0 - l) * 0.35          # lift toward white, keep a little texture
        px[i] = w * 0.97; px[i + 1] = w * 0.97; px[i + 2] = w
    white.pixels = px
    white.pack()
    for o in bpy.data.objects:
        if o.type != 'MESH' or not (o.name.startswith("Hair_Beard") or o.name.startswith("Eyebrows")):
            continue
        for slot in o.material_slots:
            if not slot.material or not slot.material.use_nodes:
                continue
            for n in slot.material.node_tree.nodes:
                if n.type == 'TEX_IMAGE' and n.image and n.image.name.startswith("T_Hair_1_BaseColor"):
                    n.image = white

# ---- animations from the two libraries ----
def import_actions(glb):
    before_objs = set(bpy.data.objects)
    before_acts = set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=glb)
    for a in bpy.data.actions:
        if a not in before_acts:
            a.use_fake_user = True
    for o in [o for o in bpy.data.objects if o not in before_objs]:
        bpy.data.objects.remove(o, do_unlink=True)

import_actions(find("UAL1_Standard.glb"))
import_actions(find("UAL2_Standard.glb"))

keep = set(CLIPS)
for a in list(bpy.data.actions):
    if a.name not in keep:
        bpy.data.actions.remove(a)
missing = [c for c in CLIPS if c not in bpy.data.actions]
print("clips kept:", len(bpy.data.actions), "missing:", missing)

# the exporter looks at the NLA to know which actions belong to the armature
arm.animation_data_create()
for a in bpy.data.actions:
    tr = arm.animation_data.nla_tracks.new()
    tr.name = a.name
    st = tr.strips.new(a.name, int(a.frame_range[0]), a)
    tr.mute = True
arm.animation_data.action = None

# ---- report what each material samples (missing textures show up here) ----
for m in bpy.data.materials:
    if not m.use_nodes:
        print("  material", m.name, "(no nodes)")
        continue
    imgs = [(n.image.name, n.image.has_data, tuple(n.image.size)) for n in m.node_tree.nodes if n.type == 'TEX_IMAGE' and n.image]
    print("  material", m.name, imgs)

# ---- leftovers from the animation files (bone display shapes etc.) ----
for o in list(bpy.data.objects):
    if o.type == 'MESH' and (o.parent is None or o.name.startswith("Icosphere")):
        print("  removing stray object", o.name)
        bpy.data.objects.remove(o, do_unlink=True)

# ---- shrink the textures (the packs ship 4096^2 PNGs; the player is a few hundred pixels
#      tall on screen, so 1024 is plenty and keeps the FBX small enough for the repo) ----
TEX_MAX = 1024
tex_dir = os.path.join(os.path.dirname(OUT), "tex")
os.makedirs(tex_dir, exist_ok=True)
for img in list(bpy.data.images):
    if img.users == 0 or not img.has_data or img.size[0] == 0:
        continue
    w, h = img.size
    if max(w, h) > TEX_MAX:
        k = TEX_MAX / max(w, h)
        img.scale(max(1, int(w * k)), max(1, int(h * k)))
    # write a fresh file-backed copy (packed images such as the white hair can't be re-saved
    # in place) and point every material at it; pixels are copied raw, so normal maps stay linear
    path = os.path.join(tex_dir, bpy.path.clean_name(img.name) + ".png")
    small = bpy.data.images.new(img.name + "_s", img.size[0], img.size[1], alpha=True)
    small.pixels = img.pixels[:]
    small.filepath_raw = path
    small.file_format = 'PNG'
    small.save()
    small.colorspace_settings.name = img.colorspace_settings.name
    img.user_remap(small)
    print("  texture %s %dx%d -> %dx%d" % (img.name, w, h, small.size[0], small.size[1]))

# ---- export ----
# FBX: the game's loader is proven on Blender FBX (KayKit). The GLB export is self-consistent
# (world * inverseBind = I) but the game's [bind-check] saw worstDiff=2 on it and rebuilt the
# offsets, which broke the arms, so FBX is the default.
os.makedirs(os.path.dirname(OUT), exist_ok=True)
if OUT.lower().endswith(".glb"):
    bpy.ops.export_scene.gltf(
        filepath=OUT, export_format='GLB', use_selection=False,
        export_yup=True, export_apply=False, export_skins=True,
        export_animations=True, export_animation_mode='NLA_TRACKS',
        export_force_sampling=True, export_image_format='JPEG', export_jpeg_quality=90,
    )
else:
    # the NLA tracks are only for glTF; FBX takes come from "all actions"
    arm.animation_data.nla_tracks.clear() if hasattr(arm.animation_data.nla_tracks, "clear") else None
    for tr in list(arm.animation_data.nla_tracks):
        arm.animation_data.nla_tracks.remove(tr)
    bpy.ops.export_scene.fbx(
        filepath=OUT, use_selection=False, object_types={'ARMATURE', 'MESH'},
        add_leaf_bones=False, armature_nodetype='NULL',
        apply_unit_scale=True, apply_scale_options='FBX_SCALE_ALL',   # metres, no x100 on the root
        bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False,
        bake_anim_force_startend_keying=True, bake_anim_simplify_factor=1.0,   # Blender default; 0 doubles the file
        path_mode='COPY', embed_textures=True,
    )
print("exported", OUT, os.path.getsize(OUT))
for o in bpy.data.objects:
    if o.type == 'MESH':
        print("  mesh", o.name, len(o.data.vertices), "verts")
