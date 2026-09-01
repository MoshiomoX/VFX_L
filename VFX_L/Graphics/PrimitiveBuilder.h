// ============================================================
// PrimitiveBuilder.h
// 基本形状のメッシュをプログラム生成する（テスト実体用）
// ※左手座標系（aiProcess_MakeLeftHanded 相当）に合わせた
//   時計回り（CW）の三角形巻き順で生成する。
// ============================================================
#pragma once
#include <memory>
#include <d3d11.h>
#include <SimpleMath.h>

class Model;

namespace PrimitiveBuilder
{
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;

    // 立方体（halfExtents は AABB と同じ定義）
    std::shared_ptr<Model> CreateBox(ID3D11Device* device,
        const Vector3& halfExtents,
        const Vector4& color = { 1, 1, 1, 1 });

    // 球
    std::shared_ptr<Model> CreateSphere(ID3D11Device* device,
        float radius,
        const Vector4& color = { 1, 1, 1, 1 },
        int segments = 16);

    // カプセル（radius + 円柱部の height。衝突体と同じ定義）
    std::shared_ptr<Model> CreateCapsule(ID3D11Device* device,
        float radius, float height,
        const Vector4& color = { 1, 1, 1, 1 },
        int segments = 16);
}