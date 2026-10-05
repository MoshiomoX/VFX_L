// ============================================================
// PrimitiveBuilder.h
// 基本形状のメッシュをプログラム生成する（テスト実体用）
// ※左手座標系（aiProcess_MakeLeftHanded 相当）に合わせた
//   時計回り（CW）の三角形巻き順で生成する。
// ============================================================
#pragma once
#include <memory>
#include <functional>
#include <vector>
#include <d3d11.h>
#include <SimpleMath.h>
#include "Graphics/Mesh/Vertex3D.h"

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

    // 六面体（8 頂点。台形柱・楔・斜面。頂点順は CollisionMath::ConvexFromHexahedron と同じ）
    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& color = { 1, 1, 1, 1 });
    // 同じ六面体を 2 色で：上を向いた面（法線 y > 0.5。坂の上面も含む）は top、他は side
    //（地形の台地 = 上が草・横が岩）
    std::shared_ptr<Model> CreateHexahedron(ID3D11Device* device, const Vector3 v[8],
        const Vector4& topColor, const Vector4& sideColor);

    // 六面体をまとめて 1 つのモデルにする（動かない地形を 1 回の draw にまとめる用。
    // 1 個ずつだと DrawMesh の状態の積み直しが数十回、影の 3 段でその 3 倍になる）。
    // Append に世界座標の 8 頂点を渡し、最後に Build。見た目は CreateHexahedron と同じ
    // 頂点の uv は (地形のテクスチャの層 + 1, 0)（TerrainSurface。0 = 法線と高さで自動。2026-10-03）
    class HexahedronBatch
    {
    public:
        void Append(const Vector3 v[8], const Vector4& topColor, const Vector4& sideColor);
        std::shared_ptr<Model> Build(ID3D11Device* device) const;
        bool Empty() const { return m_Indices.empty(); }
        // 次の Append からの上面 / 側面の層（TerrainSurface::Layer）。-1 = 自動
        void SetLayers(int topLayer, int sideLayer) { m_TopLayer = topLayer; m_SideLayer = sideLayer; }
    private:
        std::vector<VERTEX_3D> m_Verts;
        std::vector<unsigned int> m_Indices;
        int m_TopLayer = -1, m_SideLayer = -1;
    };

    // 細分化した水平面（高さ y、中心原点で sizeX × sizeZ、divX × divZ 分割）。
    // 頂点色を colorAt(x, z)（ローカル座標）で塗る。地面の色むら用
    std::shared_ptr<Model> CreateColoredGrid(ID3D11Device* device,
        float sizeX, float sizeZ, int divX, int divZ, float y,
        const std::function<Vector4(float x, float z)>& colorAt);

    // 段々の地面（2026-10-02、フィールドの三層）。cellsX × cellsZ マス（一辺 cellSize、中心原点）の
    // マス毎の高さ levelAt(gx, gz) の水平面と、高さの違う隣のマスとの境の縦の壁（低い側を向く）。
    // 同じ高さのマス同士は頂点を共有する（色が滑らかに繋がる）。上面は topColorAt(x, z, 高さ)、
    // 壁は bandHeight 毎の帯に分けて帯の中ほどの wallColorAt(x, y, z) で塗る（地層の縞）。
    // 地形のテクスチャの層（2026-10-03、TerrainSurface。頂点の uv = (層 + 1, 0)）: topLayerAt(gx, gz) = マスの上面、
    // wallLayerAt(高い側の gx, gz, 低い側の gx, gz) = 境の壁。-1 / 無し = 法線と高さで自動。
    // 起伏（2026-10-04）: heightAt(gx, gz, x, z) を渡すと、上面はマスを subdiv × subdiv に割った頂点毎の高さ
    // （そのマスの式で測る。同じ level のマス同士は同じ式 = 境の頂点を共有）、法線は同じ式の傾き、
    // 境の壁は両側の式の高さの間に張る。levelAt はマスの組（区域）を分ける値として使う
    std::shared_ptr<Model> CreateSteppedGrid(ID3D11Device* device,
        int cellsX, int cellsZ, float cellSize,
        const std::function<float(int gx, int gz)>& levelAt,
        const std::function<Vector4(float x, float z, float level)>& topColorAt,
        const std::function<Vector4(float x, float y, float z)>& wallColorAt,
        float bandHeight = 2.0f,
        const std::function<int(int gx, int gz)>& topLayerAt = nullptr,
        const std::function<int(int hiGx, int hiGz, int loGx, int loGz)>& wallLayerAt = nullptr,
        const std::function<float(int gx, int gz, float x, float z)>& heightAt = nullptr,
        int subdiv = 1);

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