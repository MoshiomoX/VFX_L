// ============================================================
// TreeCommon.hlsli
// 風で揺れる木・茂み（World/WindTrees）の共通部分。
//   TreeCB     描画用の cbuffer（b0）。影の段では g_ViewProj に光源の行列が入る
//   TreeVertex 幹・枝の筒の頂点（World/TreeBuilder が作る）
//   TreeLeaf   葉 1 枚（枝先の房）
//   TreeState  木 1 本の揺れの状態（TreeSwayCS が毎フレーム書く）
//   TreeDeform 頂点を「木ごと傾く + 枝の揺れ」で動かす
// C++ 側の対応：WindTrees::TreeCB、TreeBuilder::Vertex / Leaf、WindTrees::State
// ============================================================
#ifndef TREE_COMMON_HLSLI
#define TREE_COMMON_HLSLI

#ifndef TREE_SWAY_CS   // 揺れの CS は自分の cbuffer を b0 に置く
cbuffer TreeCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    float g_Time;
    float3 g_BarkColor;
    float g_BranchIdle;      // 風が無い時の枝の揺れ m
    float3 g_LeafColor;
    float g_BranchWind;      // 風の強さ 1 あたりの枝の揺れ m
    float3 g_LeafColor2;     // 木ごとのばらつきの先の色
    float g_FlutterIdle;     // 葉のはためき rad（風無し）
    float3 g_BareColor;      // 枯れ木の幹
    float g_FlutterWind;     // 風の強さ 1 あたりの葉のはためき rad
    float g_CrownMix;        // 葉の陰影：0 = 葉の板の向き、1 = 樹冠の球の法線
    float g_InnerDark;       // 樹冠の奥の明るさ（0..1）
    float g_LeafWidth;       // 葉の幅 / 長さ
    float g_ShakeAmp;        // 爆風で揺さぶられた時の枝の震え m
    uint g_LeafStride;       // 葉を何枚おきに描くか（遠いシャドウマップのカスケードで間引く）
    float g_LeafScale;       // 間引いた分だけ葉を大きく
    uint g_LeafCount;
    float g_TreePad;
};
#endif

#define TREE_INDEX_MASK 0x00FFFFFFu
#define TREE_BARE_BIT 0x01000000u    // TreeVertex.tree：枯れ木の幹（色を変える）

// 48 バイト
struct TreeVertex
{
    float3 pos;
    uint tree;       // bit 0-23 木の番号、bit 24 枯れ木
    float3 normal;
    float mainW;     // 木ごと傾く量（根元 0、梢 1）
    float branchW;   // 枝の揺れの量（枝先ほど大きい）
    float phase;     // 大枝ごとの位相
    float barkV;     // 枝に沿った長さ m
    float tip;       // 枝先ほど 1（幹は 0）
};

// 64 バイト
struct TreeLeaf
{
    float3 pos;      // 付け根
    uint tree;
    float3 tipDir;   // 葉の伸びる向き（単位）
    float size;      // 長さ m
    float3 crownN;   // 樹冠を球と見た法線（陰影用）
    float shade;     // 樹冠の奥ほど 0
    float mainW;
    float branchW;
    float phase;
    uint seed;       // 下位 24 bit 乱数、上位 8 bit 木ごとの色のばらつき
};

// 64 バイト
struct TreeState
{
    float3 root;
    float height;
    float2 lean;     // 梢の水平のずれ m
    float2 leanVel;
    float2 air;      // 樹冠での空気の速さ xz（なまらせた物）
    float windAmt;   // 風の強さ（0 = 無風、1 = 基準の風速）
    float flex;      // 風での傾きやすさ（茂みは小さめ）
    float3 crown;    // 樹冠の中心（ワールド）
    float shake;     // 爆風の揺さぶり 0..1（だんだん消える）
};

#include "../Common/Hash.hlsli"

float TreeRand(inout uint s)
{
    s = HashU32(s + 0x9e3779b9u);
    return (float) (s & 0xFFFFFFu) / 16777216.0;
}

#ifndef TREE_SWAY_CS
// 木ごと傾く（梢が lean だけずれる弧に近い形）+ 大枝の揺れ + 爆風の震え
float3 TreeDeform(float3 p, TreeState t, float mainW, float branchW, float phase)
{
    float3 lean = float3(t.lean.x, 0.0, t.lean.y);
    float3 off = lean * mainW;
    off.y -= 0.5 * dot(t.lean, t.lean) / max(t.height, 0.5) * mainW;   // 傾いた分だけ下がる

    float airLen = length(t.air);
    float2 wd = airLen > 1e-3 ? t.air / airLen : float2(1.0, 0.0);
    float s1 = sin(g_Time * (1.5 + 0.3 * sin(phase * 3.1)) + phase);
    float s2 = sin(g_Time * 2.6 + phase * 1.9);
    float amp = branchW * (g_BranchIdle + g_BranchWind * t.windAmt);
    off += float3(wd.x, 0.0, wd.y) * ((0.55 * s1 + 0.45) * amp)
         + float3(-wd.y, 0.0, wd.x) * (0.45 * s2 * amp)
         + float3(0.0, 0.3 * s2 * amp, 0.0);
    off += float3(sin(g_Time * 19.0 + phase), 0.4 * sin(g_Time * 23.0 + phase * 2.0), cos(g_Time * 17.0 + phase))
         * (t.shake * branchW * g_ShakeAmp);
    return p + off;
}
#endif

#endif
