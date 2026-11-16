// ============================================================
// TreeBuilder.h
// 木・茂みの形をプログラムで作る（モデル素材を使わない）。
//   幹・枝 = 先細りの筒のメッシュ、葉 = 枝先に房で付く小さな板の一覧。
//   風で揺らすため、頂点と葉ごとに「木ごと傾く量（mainW）」「枝の揺れ（branchW）」「枝の位相（phase）」を持つ。
// 結果は呼んだ側の配列に足していく（全部の木を 1 つのバッファにまとめて描く）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include <cstdint>

namespace TreeBuilder
{
    enum class Kind : uint8_t
    {
        Broadleaf,   // 広葉樹：幹 → 大枝 → 小枝 + 葉
        Bare,        // 枯れ木：葉なし、小枝を多めに
        Shrub,       // 茂み：幹なし、地面から何本も伸びる茎 + 葉
    };

    constexpr uint32_t kBareBit = 0x01000000u;   // Vertex::tree の枯れ木の印（= TREE_BARE_BIT）

    // HLSL の TreeVertex（Shader/Tree/TreeCommon.hlsli）と同じ並び（48B）
    struct Vertex
    {
        DirectX::SimpleMath::Vector3 pos; uint32_t tree;   // tree：bit 0-23 木の番号、bit 24 枯れ木
        DirectX::SimpleMath::Vector3 normal; float mainW;
        float branchW; float phase; float barkV; float tip;   // barkV = 枝に沿った長さ m、tip = 枝先ほど 1
    };

    // HLSL の TreeLeaf と同じ並び（64B）
    struct Leaf
    {
        DirectX::SimpleMath::Vector3 pos; uint32_t tree;       // pos = 葉の付け根
        DirectX::SimpleMath::Vector3 tip; float size;          // tip = 葉の伸びる向き（単位）、size = 長さ m
        DirectX::SimpleMath::Vector3 crownN; float shade;      // crownN = 樹冠を球と見た法線、shade = 樹冠の奥ほど 0
        float mainW, branchW, phase; uint32_t seed;
    };

    struct Params
    {
        Kind kind = Kind::Broadleaf;
        DirectX::SimpleMath::Vector3 root;   // 根元（地面）
        float height = 5.0f;                 // 全体の高さ m
        float yawDeg = 0.0f;
        uint32_t seed = 1;
        uint32_t treeIndex = 0;              // 頂点・葉に書く木の番号（揺れの状態の添字）
        float leafDensity = 1.0f;            // 葉の数の倍率
        float leafSize = 1.0f;               // 葉の大きさの倍率
    };

    struct Result
    {
        DirectX::SimpleMath::Vector3 crownCenter;   // 樹冠の中心（ワールド）
        float crownRadius = 0.0f;                   // 水平の半径 m
        int vertexCount = 0, leafCount = 0;
    };

    // 1 本分を verts / indices / leaves の後ろに足す
    Result Build(const Params& p, std::vector<Vertex>& verts, std::vector<uint32_t>& indices, std::vector<Leaf>& leaves);
}
