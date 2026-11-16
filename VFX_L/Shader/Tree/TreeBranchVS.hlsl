// ============================================================
// TreeBranchVS.hlsl
// 幹・枝の筒（全部の木を 1 つのバッファに入れ、DrawIndexed 1 回。入力レイアウトなし：
// インデックスの値が SV_VertexID に入るので StructuredBuffer から読む）。
// 木の揺れの状態で TreeDeform して描く。影の段でも同じ VS（g_ViewProj が光源の行列）
// ============================================================
#include "TreeCommon.hlsli"

StructuredBuffer<TreeVertex> g_Verts : register(t0);
StructuredBuffer<TreeState> g_States : register(t1);

struct BranchOut
{
    float4 pos : SV_POSITION;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 bark : TEXCOORD2;   // x = 枝に沿った長さ、y = 枝先ほど 1
    nointerpolation uint tree : TEXCOORD3;
};

BranchOut main(uint vid : SV_VertexID)
{
    TreeVertex v = g_Verts[vid];
    TreeState t = g_States[v.tree & TREE_INDEX_MASK];
    BranchOut o;
    float3 world = TreeDeform(v.pos, t, v.mainW, v.branchW, v.phase);
    o.world = world;
    o.normal = v.normal;
    o.bark = float2(v.barkV, v.tip);
    o.tree = v.tree;
    o.pos = mul(float4(world, 1.0), g_ViewProj);
    return o;
}
