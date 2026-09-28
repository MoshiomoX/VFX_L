// ============================================================
// PrimitiveBuilder.h
// 基本形状のメッシュをプログラム生成する（テスト実体用）
// ※左手座標系（aiProcess_MakeLeftHanded 相当）に合わせた
//   時計回り（CW）の三角形巻き順で生成する。
// ============================================================
#pragma once
#include <memory>
#include <functional>
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

    // 六面体（8 頂点。台形柱・楔・斜坡。頂点順は CollisionMath::ConvexFromHexahedron と同じ）
    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& color = { 1, 1, 1, 1 });
    // 同じ六面体を 2 色で：上を向いた面（法線 y > 0.5。坂の上面も含む）は top、他は side
    //（地形の台地 = 上が草・横が岩）
    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& topColor, const Vector4& sideColor);

    // 細分化した水平面（高さ y、中心原点で sizeX × sizeZ、divX × divZ 分割）。
    // 頂点色を colorAt(x, z)（ローカル座標）で塗る。地面の色むら用
    std::shared_ptr<Model> CreateColoredGrid(ID3D11Device* device,
        float sizeX, float sizeZ, int divX, int divZ, float y,
        const std::function<Vector4(float x, float z)>& colorAt);

    // カプセル（radius + 円柱部の height。衝突体と同じ定義）
    std::shared_ptr<Model> CreateCapsule(ID3D11Device* device,
        float radius, float height,
        const Vector4& color = { 1, 1, 1, 1 },
        int segments = 16);

    // 双角錐（宝石）：赤道の sides 角形 + 上下の頂点。面毎に法線を分ける（面がはっきり見える）。
    // 赤道が原点、上の頂点 y = top、下の頂点 y = -bottom
    std::shared_ptr<Model> CreateBipyramid(ID3D11Device* device,
        float radius, float top, float bottom, int sides,
        const Vector4& color = { 1, 1, 1, 1 });
}