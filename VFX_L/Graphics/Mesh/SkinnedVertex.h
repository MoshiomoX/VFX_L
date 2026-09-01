#pragma once
#include <SimpleMath.h>
#include <cstdint>

static constexpr int MAX_BONE_INFLUENCE = 4;

// CS route: この頂点は StructuredBuffer に入れて CS から読む（InputLayout不要）
// ※ HLSL側の struct と「型・順番・サイズ(80byte)」を厳密に一致させること
struct SkinnedVertex
{
    DirectX::SimpleMath::Vector3 position;  float _pad0;       // 16
    DirectX::SimpleMath::Vector3 normal;    float _pad1;       // 16
    DirectX::SimpleMath::Vector2 uv;        float _pad2[2];    // 16
    uint32_t boneIndices[MAX_BONE_INFLUENCE];                  // 16
    float    boneWeights[MAX_BONE_INFLUENCE];                  // 16  → 計80byte

    void AddBone(uint32_t boneIndex, float weight)
    {
        if (weight <= 0.0f) return;
        for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
            if (boneWeights[i] == 0.0f) { boneIndices[i] = boneIndex; boneWeights[i] = weight; return; }

        int minIdx = 0; // 4本埋まってたら最弱を置換
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