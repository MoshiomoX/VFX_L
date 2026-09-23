#pragma once
#include <SimpleMath.h>
#include <cstdint>

static constexpr int MAX_BONE_INFLUENCE = 4;

// CS 経路: StructuredBuffer に入れて SkinningCS が読む（InputLayout 不要）
// ※HLSL 側（SkinningCS.hlsl の SkinnedVertex）と型・順番・サイズを厳密一致させること
struct SkinnedVertex
{
    DirectX::SimpleMath::Vector3 position;  float _pad0;       // 16
    DirectX::SimpleMath::Vector3 normal;    float _pad1;       // 16
    DirectX::SimpleMath::Vector3 tangent;   float _pad3;       // 16  bind pose の接線
    DirectX::SimpleMath::Vector2 uv;        float _pad2[2];    // 16
    uint32_t boneIndices[MAX_BONE_INFLUENCE];                  // 16
    float    boneWeights[MAX_BONE_INFLUENCE];                  // 16  → 計 96byte

    void AddBone(uint32_t boneIndex, float weight)
    {
        if (weight <= 0.0f) return;
        for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
            if (boneWeights[i] == 0.0f) { boneIndices[i] = boneIndex; boneWeights[i] = weight; return; }

        int minIdx = 0; // 4本埋まっていたら最弱を置き換える
        for (int i = 1; i < MAX_BONE_INFLUENCE; ++i)
            if (boneWeights[i] < boneWeights[minIdx]) minIdx = i;
        if (weight > boneWeights[minIdx]) { boneIndices[minIdx] = boneIndex; boneWeights[minIdx] = weight; }
    }

    void NormalizeWeights()
    {
        float sum = boneWeights[0] + boneWeights[1] + boneWeights[2] + boneWeights[3];
        if (sum > 0.0f) { float inv = 1.0f / sum; for (int i = 0; i < 4; ++i) boneWeights[i] *= inv; }
        else { boneIndices[0] = 0; boneWeights[0] = 1.0f; } // どの骨にも属さない頂点の保険
    }
};
static_assert(sizeof(SkinnedVertex) == 96, "SkinnedVertex layout mismatch with SkinningCS.hlsl");