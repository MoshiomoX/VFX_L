// ============================================================
// StaticPropRenderer.h
// 動かない置物（野原の木・岩・茂み・草）をモデル毎にまとめて instanced で描く。
//
//   Build : ModelComponent::batched の実体を集め、world 行列と包囲球を 1 回だけ作る
//           （置いた後に Transform を動かしても追従しない。地形の作り直しで Build し直す）
//   Render: 毎フレーム、視錐台と距離（包囲球の半径に比例）で間引き、見える分だけを
//           動的 StructuredBuffer に詰めて、モデルの submesh 毎に DrawIndexedInstanced 1 回。
//           材質（PS・テクスチャ）はモデルのものをそのまま使い、VS だけ StaticPropVS に差し替える
//
// 1 個ずつ DrawMesh すると 1 回毎に状態を全部積み直すので、数百個で CPU が詰まる
// （2026-09-27 計測: 野原の置物 760 個で Debug 9.6 ms / Release 0.9 ms）。
// ============================================================
#pragma once
#include <memory>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>

class Model;
class Renderer;
class Registry;
class VertexShader;
class Material;

class StaticPropRenderer
{
public:
    struct Settings
    {
        bool  enabled = true;
        bool  frustumCull = true;
        // 包囲球の半径 1m あたり何 m 先まで描くか（小さい物ほど近くで消える）。0 = 距離で間引かない。
        // 既定は 0: instanced なら描く数でほぼ差が出ない（2026-09-27 Debug: 視錐台だけの 157 個と
        // 全 760 個が同じ 8.7 ms）のに、60 にすると中距離の草や小石が消えて見えた
        float distPerRadius = 0.0f;
        float maxDistance = 0.0f;       // 大きさに関係無い上限 m。0 = 無し
    };
    struct Stats
    {
        int registered = 0;   // Build で集めた数
        int drawn = 0;        // 今フレーム描いた数
        int models = 0;       // モデルの種類
        int drawCalls = 0;    // 今フレームの DrawIndexedInstanced 回数
    };

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    void Build(Registry& reg);
    void Clear();

    // シーンの不透明描画の中で呼ぶ（RenderSystem と同じ状態のまま）
    void Render(Renderer& renderer);

    // シャドウマップへ深度だけ（ShadowMap の段ごと）。光源の view * proj の視錐台で間引く
    // （近い面は見ない：シャドウマップの手前、光源側にある物も影を落とす）。Stats は変えない
    void RenderDepth(ID3D11DeviceContext* ctx, const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj);

    void DrawImGui();

    Settings& GetSettings() { return m_Settings; }
    const Stats& GetStats() const { return m_Stats; }

private:
    struct Instance
    {
        DirectX::SimpleMath::Matrix world;
        DirectX::SimpleMath::Vector3 center;   // 包囲球（ワールド）
        float radius = 0.0f;
    };
    struct Group
    {
        std::shared_ptr<Model> model;
        int first = 0, count = 0;           // m_Instances の範囲
        int drawStart = 0, drawCount = 0;   // 今フレームの instance buffer の範囲
        float minRadius = 0.0f, maxRadius = 0.0f;
    };

    bool EnsureCapacity(int count);

    ID3D11Device* m_Device = nullptr;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<Material> m_FallbackMaterial;   // 材質の無い submesh 用（既定の VS/PS + 白）

    std::vector<Instance> m_Instances;   // モデル毎に連続
    std::vector<Group> m_Groups;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_InstanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_InstanceSRV;
    int m_Capacity = 0;

    Settings m_Settings;
    Stats m_Stats;
};
