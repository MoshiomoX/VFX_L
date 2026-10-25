// ============================================================
// SkinnedModelCloth.cpp
// SkinnedModel::SplitClothChain（2026-10-23、ユーザー「マントを複数の鎖にして」）。
//   Shadowkin のマントは Cloak1〜8 の 1 本鎖で、頂点は鎖の段だけで重みが分かれている（左右の区別が無い）。
//   これだと揺らしても板 1 枚が前後に振れるだけで、横に振れたり脚に片側だけ押されたりしない。
//   FBX は触らず、読み込み時に骨を足して重みを付け直す：
//     1. 鎖に重みを持つ頂点の左右の広がり ±W を測る（体の左右 = 鎖の向きと「上背 → 鎖の根」から）
//     2. 段ごとに列の骨を作る：元の骨を左右へ -W〜+W の等間隔にずらした物（真ん中の列は元の骨のまま）。
//        親 = 同じ段の元の骨なので、揺らさない時（ClothChain が無い・無効）は元の鎖と一緒に動く
//     3. 頂点の鎖の重みを左右の位置で隣の 2 列に線形に分ける。背骨（anchor）の重みは鎖の根の近くだけ残し、
//        その分を鎖へ回す。4 本を超えたら強い 4 本を残して正規化
// ============================================================
#include "Graphics/Model/SkinnedModel.h"

using namespace DirectX::SimpleMath;

namespace
{
    float SmoothStep01(float e0, float e1, float x)
    {
        const float t = std::clamp((x - e0) / (std::max)(e1 - e0, 1e-6f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}

std::string SkinnedModel::ClothColumnBoneName(const std::string& chainBone, int column)
{
    return chainBone + "_col" + std::to_string(column);
}

int SkinnedModel::SplitClothChain(const std::string& anchorBone, const std::vector<std::string>& chain,
    int columns, float anchorKeepEnd)
{
    if (columns < 3 || (columns % 2) == 0 || chain.size() < 3) return 0;
    if (m_Skeleton.FindBoneIndex(ClothColumnBoneName(chain[0], 0)) >= 0) return 0;   // 分け済み（キャッシュ済みのモデル）

    const int anchor = m_Skeleton.FindBoneIndex(anchorBone);
    const size_t n = chain.size();
    std::vector<int> idx(n, -1);
    for (size_t k = 0; k < n; ++k) idx[k] = m_Skeleton.FindBoneIndex(chain[k]);
    if (anchor < 0 || idx[0] < 0)
    {
        std::cout << "[ClothSplit] bones not found: " << anchorBone << " / " << chain[0] << std::endl;
        return 0;
    }

    // 重みを持つ段（submesh に offset がある骨）。終点（Cloak8_end）は重みが無い
    auto hasOffset = [&](int bone)
    {
        for (const auto& sub : m_SubMeshes)
            if (sub.boneOffsets.count(bone)) return true;
        return false;
    };
    const int boneCount0 = m_Skeleton.GetBoneCount();
    std::vector<int> level(boneCount0, -1);
    int levels = 0;
    for (size_t k = 0; k < n; ++k)
    {
        if (idx[k] < 0 || !hasOffset(idx[k])) continue;
        level[idx[k]] = (int)k;
        levels = (int)k + 1;
    }
    if (levels < 2) return 0;

    // 体の向き（モデル空間）。ClothChainSystem の Init と同じ作り方：上 = 鎖の先 → 根、後ろ = 上背 → 鎖の根
    std::vector<Matrix> G(levels);
    for (int k = 0; k < levels; ++k) G[k] = GetSkinBindGlobal(idx[k]);
    const Vector3 rootPos = G[0].Translation();
    Vector3 up = rootPos - G[levels - 1].Translation();
    Vector3 back = rootPos - GetSkinBindGlobal(anchor).Translation();
    up.Normalize();
    back -= up * back.Dot(up);
    if (back.LengthSquared() < 1e-10f) return 0;
    back.Normalize();
    Vector3 right = up.Cross(back);
    right.Normalize();

    auto chainWeight = [&](const SkinnedVertex& v)
    {
        float w = 0.0f;
        for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
            if (v.boneWeights[i] > 0.0f && (int)v.boneIndices[i] < boneCount0 && level[v.boneIndices[i]] >= 0) w += v.boneWeights[i];
        return w;
    };

    // ---- 1. 左右の広がり ----
    float W = 0.0f;
    for (const auto& sub : m_SubMeshes)
        for (const auto& v : sub.vertices)
        {
            if (chainWeight(v) <= 1e-4f) continue;
            const Vector3 p = Vector3::Transform(v.position, sub.nodeGlobal);
            W = (std::max)(W, std::abs((p - rootPos).Dot(right)));
        }
    if (W <= 1e-6f) return 0;

    // ---- 2. 列の骨 ----
    const int center = columns / 2;
    std::vector<std::vector<int>> col(columns, std::vector<int>(levels, -1));
    int added = 0;
    for (int c = 0; c < columns; ++c)
    {
        const float x = -W + 2.0f * W * (float)c / (float)(columns - 1);
        for (int k = 0; k < levels; ++k)
        {
            if (c == center) { col[c][k] = idx[k]; continue; }
            if (idx[k] < 0 || level[idx[k]] < 0) continue;
            Matrix gv = G[k];
            gv.Translation(G[k].Translation() + right * x);
            const int vi = m_Skeleton.AddBone(ClothColumnBoneName(chain[k], c));
            Bone& b = m_Skeleton.GetBone(vi);
            b.parentIndex = idx[k];
            b.localBindTransform = gv * G[k].Invert();   // global = local * parentGlobal
            b.offsetMatrix = gv.Invert();
            // スキニングした時の global = Invert(offset) * nodeGlobal になるように
            for (auto& sub : m_SubMeshes)
                if (sub.boneOffsets.count(idx[k])) sub.boneOffsets[vi] = sub.nodeGlobal * gv.Invert();
            col[c][k] = vi;
            ++added;
        }
    }

    // ---- 3. 重みの付け直し ----
    int verts = 0;
    double anchorBefore = 0.0, anchorAfter = 0.0;
    for (auto& sub : m_SubMeshes)
        for (auto& v : sub.vertices)
        {
            const float wc = chainWeight(v);
            if (wc <= 1e-4f) continue;

            struct Inf { uint32_t bone; float w; };
            std::vector<Inf> out;
            std::vector<std::pair<int, float>> links;   // (段, 重み)
            float wa = 0.0f, num = 0.0f;
            for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
            {
                const float w = v.boneWeights[i];
                if (w <= 0.0f) continue;
                const uint32_t bone = v.boneIndices[i];
                if ((int)bone == anchor) wa += w;
                else if ((int)bone < boneCount0 && level[bone] >= 0) { links.push_back({ level[bone], w }); num += level[bone] * w; }
                else out.push_back({ bone, w });
            }
            // 鎖の上での位置（根 0 → 先 1）。根の近くだけ背骨の重みを残す（肩に貼り付いたまま）
            const float u = num / wc / (float)(levels - 1);
            const float wa2 = wa * (1.0f - SmoothStep01(0.0f, anchorKeepEnd, u));
            if (wa2 > 0.0f) out.push_back({ (uint32_t)anchor, wa2 });
            const float scale = (wc + wa - wa2) / wc;

            const Vector3 p = Vector3::Transform(v.position, sub.nodeGlobal);
            const float f = std::clamp(((p - rootPos).Dot(right) + W) / (2.0f * W) * (float)(columns - 1), 0.0f, (float)(columns - 1));
            const int c0 = (std::min)((int)f, columns - 2);
            const float t = f - (float)c0;
            for (const auto& [k, w] : links)
            {
                if (col[c0][k] >= 0) out.push_back({ (uint32_t)col[c0][k], w * scale * (1.0f - t) });
                if (col[c0 + 1][k] >= 0) out.push_back({ (uint32_t)col[c0 + 1][k], w * scale * t });
            }

            for (int i = 0; i < MAX_BONE_INFLUENCE; ++i) { v.boneIndices[i] = 0; v.boneWeights[i] = 0.0f; }
            for (const auto& inf : out) v.AddBone(inf.bone, inf.w);   // 強い 4 本が残る
            v.NormalizeWeights();

            float after = 0.0f;
            for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
                if ((int)v.boneIndices[i] == anchor) after += v.boneWeights[i];
            anchorBefore += wa;
            anchorAfter += after;
            ++verts;
        }

    std::cout << "[ClothSplit] " << chain[0] << ": " << columns << " columns, +" << added << " bones, "
        << verts << " verts, width " << (2.0f * W) << ", anchor weight avg "
        << (verts ? anchorBefore / verts : 0.0) << " -> " << (verts ? anchorAfter / verts : 0.0) << std::endl;
    RebuildSampleCache();   // 骨が増えた（SampleLocal の表を骨の数に合わせる）
    return added;
}
