# ============================================================
# MeshCatalogFbx.py  (Blender 5.x, run in background; only Blender's bundled Python is used)
# A small reader for binary FBX, for VFX_L/Assets/VFX/Mesh/README.md (2026-10-01).
#
#   blender -b --factory-startup -P Tools/MeshCatalogFbx.py -- units <mesh dir> <out.tsv>
#     FBX version and GlobalSettings UnitScaleFactor of every .fbx (1 = cm, 2.54 = inch,
#     0.1 = mm, 10 = dm, 100 = m). FBX 6.1 files have no UnitScaleFactor.
#
#   blender -b --factory-startup -P Tools/MeshCatalogFbx.py -- convert <mesh dir> <out dir> name:idx ...
#     Binary FBX 6.1 files that neither Blender (needs 7100+) nor the game's assimp
#     ("FBX-DOM unsupported, old format version") can read: takes Vertices /
#     PolygonVertexIndex / LayerElementUV straight from the Model nodes and writes
#     <out>/<name>.obj (Y up, raw FBX coordinates, node transforms ignored) plus
#     <out>/stats_old.tsv. Render the objs with MeshCatalogRender.py afterwards.
# ============================================================
import bpy, os, sys, struct, zlib

def read_node(b, pos, ver):
    if ver >= 7500:
        end, nprop, plen = struct.unpack_from("<QQQ", b, pos); pos += 24
    else:
        end, nprop, plen = struct.unpack_from("<III", b, pos); pos += 12
    nlen = b[pos]; pos += 1
    if end == 0:
        return None, pos
    name = b[pos:pos + nlen].decode("latin1"); pos += nlen
    props = []
    for _ in range(nprop):
        t = chr(b[pos]); pos += 1
        if t == "Y": props.append(struct.unpack_from("<h", b, pos)[0]); pos += 2
        elif t == "C": props.append(b[pos]); pos += 1
        elif t == "I": props.append(struct.unpack_from("<i", b, pos)[0]); pos += 4
        elif t == "F": props.append(struct.unpack_from("<f", b, pos)[0]); pos += 4
        elif t == "D": props.append(struct.unpack_from("<d", b, pos)[0]); pos += 8
        elif t == "L": props.append(struct.unpack_from("<q", b, pos)[0]); pos += 8
        elif t in "fdlib":
            n, enc, clen = struct.unpack_from("<III", b, pos); pos += 12
            raw = b[pos:pos + clen]; pos += clen
            if enc == 1: raw = zlib.decompress(raw)
            fmt = {"f": "f", "d": "d", "l": "q", "i": "i", "b": "B"}[t]
            props.append(list(struct.unpack_from("<%d%s" % (n, fmt), raw, 0)))
        elif t in "SR":
            n = struct.unpack_from("<I", b, pos)[0]; pos += 4
            props.append(b[pos:pos + n]); pos += n
        else:
            raise ValueError("prop type " + t)
    children = []
    while pos < end:
        c, pos = read_node(b, pos, ver)
        if c is None: break
        children.append(c)
    return (name, props, children), end

def parse(path):
    b = open(path, "rb").read()
    ver = struct.unpack_from("<I", b, 23)[0]
    pos = 27; roots = []
    while pos < len(b) - 13:
        n, pos = read_node(b, pos, ver)
        if n is None: break
        roots.append(n)
    return ver, roots

def find_all(nodes, name, out):
    for n in nodes:
        if n[0] == name: out.append(n)
        find_all(n[2], name, out)
    return out

def arr(n):
    # 7.x stores one array property; 6.1 stores every number as its own property
    p = n[1]
    return p[0] if len(p) == 1 and isinstance(p[0], list) else p

def child(n, name):
    for c in n[2]:
        if c[0] == name: return c
    return None

def build_meshes(roots):
    res = []
    # 6.1 keeps the geometry right under Objects/Model ("Mesh"); 7.x under Geometry
    cands = [n for n in find_all(roots, "Model", []) + find_all(roots, "Geometry", []) if child(n, "Vertices")]
    for n in cands:
        v = arr(child(n, "Vertices"))
        idx = [int(k) for k in arr(child(n, "PolygonVertexIndex"))]
        verts = [tuple(v[i:i + 3]) for i in range(0, len(v), 3)]
        faces, cur = [], []
        for k in idx:
            if k < 0: cur.append(~k); faces.append(cur); cur = []
            else: cur.append(k)
        uv = None
        le = child(n, "LayerElementUV")
        if le and child(le, "UV"):
            uvv = arr(child(le, "UV"))
            uvs = [tuple(uvv[i:i + 2]) for i in range(0, len(uvv), 2)]
            uidx = [int(k) for k in arr(child(le, "UVIndex"))] if child(le, "UVIndex") else None
            mapping = child(le, "MappingInformationType")[1][0].decode() if child(le, "MappingInformationType") else ""
            if mapping == "ByPolygonVertex":
                uv = [uvs[i] for i in uidx] if uidx else uvs
            elif mapping in ("ByVertice", "ByVertex", "ByControlPoint"):
                uv = [uvs[k if k >= 0 else ~k] for k in idx]
        res.append((verts, faces, uv))
    return res

def mode_units(mesh_dir, out):
    w = open(out, "w", encoding="utf-8")
    w.write("file\tfbxver\tunitScale\n")
    for f in sorted(os.listdir(mesh_dir)):
        if not f.lower().endswith(".fbx"): continue
        try:
            head = open(os.path.join(mesh_dir, f), "rb").read(23)
            if not head.startswith(b"Kaydara FBX Binary"):
                w.write(f"{f}\tascii\t\n"); continue
            ver, roots = parse(os.path.join(mesh_dir, f))
            usf = ""
            for gs in find_all(roots, "GlobalSettings", []):
                for p70 in gs[2]:
                    for p in p70[2]:
                        if p[0] == "P" and p[1] and p[1][0] == b"UnitScaleFactor":
                            usf = p[1][-1]
            w.write(f"{f}\t{ver}\t{usf}\n")
        except Exception as ex:
            w.write(f"{f}\tERR {ex}\t\n")
    w.close()

def mode_convert(mesh_dir, out_dir, items):
    os.makedirs(out_dir, exist_ok=True)
    stats = open(os.path.join(out_dir, "stats_old.tsv"), "w", encoding="utf-8")
    for name, idx in items:
        i = int(idx)
        path = os.path.join(mesh_dir, name + ".FBX")
        bpy.ops.wm.read_factory_settings(use_empty=True)
        sc = bpy.context.scene
        try:
            ver, roots = parse(path)
            meshes = build_meshes(roots)
        except Exception as ex:
            stats.write(f"{i}\t{name}.FBX\tPARSE_FAIL {ex}\t\t\t\t\t\t\t\t\n"); continue
        if not meshes:
            stats.write(f"{i}\t{name}.FBX\tNO_MESH (fbx {ver})\t\t\t\t\t\t\t\t\n"); continue
        nv = nf = 0; has_uv = True
        for k, (verts, faces, uv) in enumerate(meshes):
            me = bpy.data.meshes.new(f"m{k}")
            me.from_pydata(verts, [], faces)
            if uv:
                layer = me.uv_layers.new()
                for li, l in enumerate(layer.data):
                    if li < len(uv): l.uv = uv[li]
            else:
                has_uv = False
            me.update()
            ob = bpy.data.objects.new(f"o{k}", me)
            sc.collection.objects.link(ob)
            nv += len(verts); nf += len(faces)
        bpy.ops.object.select_all(action="SELECT")
        bpy.ops.wm.obj_export(filepath=os.path.join(out_dir, name + ".obj"), export_selected_objects=True,
                              export_materials=False, forward_axis="NEGATIVE_Z", up_axis="Y")
        stats.write(f"{i}\t{name}.FBX\tok (fbx {ver}, own parser)\t{nv}\t{nf}\t\t\t\t{'uv' if has_uv else 'NO_UV'}\t{len(meshes)}\t\n")
        stats.flush()
    stats.close()

argv = sys.argv[sys.argv.index("--") + 1:]
if argv[0] == "units":
    mode_units(argv[1], argv[2])
elif argv[0] == "convert":
    mode_convert(argv[1], argv[2], [a.split(":") for a in argv[3:]])
else:
    raise SystemExit("usage: -- units <mesh dir> <out.tsv> | convert <mesh dir> <out dir> name:idx ...")
print("FBX_DONE")
