// ============================================
// EmitMesh.hlsli
// メッシュ表面発射 — 三角形面積で加重サンプリング
// meshVertexOffset / meshVertexCount を使用
// 外部からmeshVertices SRVが必要
// ============================================

#ifndef EMIT_MESH_HLSLI
#define EMIT_MESH_HLSLI

void EmitMesh(GPUEmitter e, inout uint seed,
              StructuredBuffer<EmitMeshVertex> meshVertices,
              out float3 pos, out float3 vel)
{
    // ランダムに頂点を選択（将来的にarea加重に改良）
    int idx = e.meshVertexOffset + (int) (Random(seed) * e.meshVertexCount) % e.meshVertexCount;
    EmitMeshVertex v = meshVertices[idx];

    pos = e.position + v.position;

    // 速度は法線方向
    float3 dir = normalize(v.normal);
    float speed = RandomRange(seed, e.speedRange.x, e.speedRange.y);
    vel = dir * speed;
}

#endif