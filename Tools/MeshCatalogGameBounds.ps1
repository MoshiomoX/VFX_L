# ============================================================
# MeshCatalogGameBounds.ps1
# In-game size of every model in a folder, for VFX_L/Assets/VFX/Mesh/README.md (2026-10-01).
#   powershell -File Tools/MeshCatalogGameBounds.ps1 -MeshDir <dir> -Out <out.tsv>
# Loads each .fbx / .obj with the game's own assimp DLL (x64/Release) and the same
# import flags / properties as AssimpFlags.h, plus PreTransformVertices so node
# transforms are baked in like Model::ProcessNode does. The box is what
# Model::GetBoundsMin/Max report: game units (1 = 1 m), Y up, scale 1.
# Files the game cannot read (e.g. FBX 6.1) come out as "ERR <assimp message>".
# ============================================================
param([string]$MeshDir, [string]$Out, [string]$DllDir = "$PSScriptRoot\..\x64\Release")
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class MeshCatalogAssimp {
  const string D = "assimp-vc143-mt.dll";
  [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)] static extern IntPtr LoadLibraryW(string p);
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern IntPtr aiCreatePropertyStore();
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern void aiReleasePropertyStore(IntPtr p);
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern void aiSetImportPropertyInteger(IntPtr store, string name, int value);
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern IntPtr aiImportFileExWithProperties(string file, uint flags, IntPtr fs, IntPtr props);
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern void aiReleaseImport(IntPtr scene);
  [DllImport(D, CallingConvention = CallingConvention.Cdecl)] static extern IntPtr aiGetErrorString();
  // Triangulate | FlipUVs | CalcTangentSpace | GenNormals | MakeLeftHanded | LimitBoneWeights (= Res::kModelImportFlags)
  // + PreTransformVertices
  const uint Flags = 0x8 | 0x800000 | 0x1 | 0x20 | 0x4 | 0x200 | 0x100;

  // The DLL is not on PATH; load it by full path first so DllImport finds it
  public static string Init(string full) {
    IntPtr h = LoadLibraryW(full);
    return h == IntPtr.Zero ? ("LoadLibrary failed " + Marshal.GetLastWin32Error()) : "ok";
  }

  public static string Bounds(string path) {
    IntPtr st = aiCreatePropertyStore();
    aiSetImportPropertyInteger(st, "PP_LBW_MAX_WEIGHTS", 4);
    aiSetImportPropertyInteger(st, "IMPORT_FBX_PRESERVE_PIVOTS", 0);
    IntPtr sc = aiImportFileExWithProperties(path, Flags, IntPtr.Zero, st);
    aiReleasePropertyStore(st);
    if (sc == IntPtr.Zero) return "ERR " + Marshal.PtrToStringAnsi(aiGetErrorString());
    // x64 aiScene: mFlags @0, mRootNode @8, mNumMeshes @16, mMeshes @24
    // x64 aiMesh:  mPrimitiveTypes @0, mNumVertices @4, mNumFaces @8, mVertices @16
    int nMesh = Marshal.ReadInt32(sc, 16);
    IntPtr meshes = Marshal.ReadIntPtr(sc, 24);
    float[] lo = { float.MaxValue, float.MaxValue, float.MaxValue }, hi = { float.MinValue, float.MinValue, float.MinValue };
    long verts = 0;
    for (int m = 0; m < nMesh; m++) {
      IntPtr mesh = Marshal.ReadIntPtr(meshes, m * 8);
      int nv = Marshal.ReadInt32(mesh, 4);
      IntPtr v = Marshal.ReadIntPtr(mesh, 16);
      float[] buf = new float[nv * 3];
      Marshal.Copy(v, buf, 0, nv * 3);
      for (int i = 0; i < nv; i++) for (int k = 0; k < 3; k++) { float x = buf[i * 3 + k]; if (x < lo[k]) lo[k] = x; if (x > hi[k]) hi[k] = x; }
      verts += nv;
    }
    aiReleaseImport(sc);
    var c = System.Globalization.CultureInfo.InvariantCulture;
    return string.Format(c, "{0:0.###}\t{1:0.###}\t{2:0.###}\t{3}", hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], verts);
  }
}
'@
"assimp " + [MeshCatalogAssimp]::Init((Resolve-Path (Join-Path $DllDir "assimp-vc143-mt.dll")).Path)
$lines = @("file`tgx`tgy`tgz`tverts")
foreach ($f in Get-ChildItem $MeshDir -File | Where-Object { $_.Extension -match '^\.(fbx|obj)$' } | Sort-Object Name) {
  $lines += "$($f.Name)`t" + [MeshCatalogAssimp]::Bounds($f.FullName)
}
[IO.File]::WriteAllLines($Out, $lines, [Text.UTF8Encoding]::new($false))
"$($lines.Count - 1) files"
