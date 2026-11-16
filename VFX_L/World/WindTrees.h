// ============================================================
// WindTrees.h
// 風で揺れる木・茂み（草原）。モデル素材の木を使わず、TreeBuilder で形を作る。
//
//   Build : 地形の後、草原の木・枯れ木・茂みのモデルの実体を探し、モデルを隠して（visible = false）
//           同じ場所・同じ高さでプロシージャルの木を作る。衝突の箱・塞ぐマスは地形のまま（幹の 0.6m の柱）。
//           全部の木の幹・枝を 1 つの頂点 / インデックスバッファ、葉を 1 つの StructuredBuffer にまとめる。
//   Step  : シャドウマップの前に 1 回。TreeSwayCS が木ごとに樹冠の空気（天気の風 + 風源を風の格子で
//           置き換えた物 + 突風の扇）から梢の傾きをばねで動かす。爆発は外へ押して揺さぶる。
//   描画  : 幹・枝はアウトラインの前（線が付く）、葉は草と一緒にアウトラインの後（葉 1 枚ずつに線が付かない）。
//           影の段でも両方描く（葉は形で切り抜く）。
//
// 使い方（シーン）：Initialize → 地形の後 Build(registry, biome)（StaticPropRenderer::Build より前）
//   毎フレーム Tick(dt)・SetWind、描画の最初に Step、影の段で RenderDepth、不透明で RenderBranches、草の後 RenderLeaves
// ============================================================
#pragma once
#include "World/WindState.h"
#include "World/TerrainGenerator.h"
#include "World/TreeBuilder.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class Renderer;
class CameraBase;
class Registry;
class ComputeShader;
class VertexShader;
class PixelShader;

class WindTrees
{
public:
    struct Settings
    {
        bool  enabled = true;
        // 形（変えたら Rebuild）
        float leafDensity = 1.0f;
        float leafSize = 1.0f;
        float treeHeightMul = 1.25f;   // 元のモデルの高さに対する木の高さ（茂みは元のまま）
        // 揺れ（TreeSwayCS）
        float leanPerWind2 = 0.004f;   // 風速² 1 あたりの梢の傾き（高さに対する割合）
        float maxLean = 0.2f;          // 梢の傾きの上限（高さに対する割合）
        float freq = 0.45f;            // 高さ 5m の木の固有振動数 Hz
        float damping = 0.2f;          // 減衰比
        float windRef = 4.0f;          // windAmt = 1 になる風速 m/s
        float turbulence = 1.5f;       // 風速に比例した目標のゆらぎ
        float blastAccel = 60.0f;      // 爆発の範囲の中の外向きの加速度 m/s²
        float shakeDecay = 2.5f;       // 揺さぶりの消える速さ 1/s
        float gustSpeed = 13.0f;       // 突風の扇の中の風速 m/s（GroundDebris と同じ）
        float shrubFlex = 0.6f;        // 茂みの傾きやすさ（木 = 1）
        // 見た目（TreeCB）
        float branchIdle = 0.02f;      // 風無しの枝の揺れ m
        float branchWind = 0.1f;       // windAmt 1 あたりの枝の揺れ m
        float flutterIdle = 0.12f;     // 葉のはためき rad
        float flutterWind = 0.45f;
        float shakeAmp = 0.12f;
        float crownMix = 0.7f;         // 葉の陰影の法線：1 = 樹冠の球
        float innerDark = 0.65f;       // 樹冠の奥の明るさ
        float leafWidth = 0.55f;       // 葉の幅 / 長さ
        int shadowThin = 2;            // 影の段 c は葉を shadowThin^c 枚おきに描く（1 = 間引かない）
        DirectX::SimpleMath::Vector3 barkColor = { 0.36f, 0.25f, 0.16f };
        DirectX::SimpleMath::Vector3 bareColor = { 0.40f, 0.35f, 0.29f };
        DirectX::SimpleMath::Vector3 leafColor = { 0.14f, 0.46f, 0.04f };
        DirectX::SimpleMath::Vector3 leafColor2 = { 0.36f, 0.60f, 0.05f };
    };

    struct Stats
    {
        int trees = 0, bare = 0, shrubs = 0;
        int vertices = 0, indices = 0, leaves = 0;
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）。草原以外では何も作らない
    void Build(Registry& reg, TerrainGenerator::Biome biome);
    void Clear();
    void SetReplaceModels(bool on) { m_ReplaceModels = on; }   // false = Build しない（元のモデルのまま）
    void Rebuild();  // 形の設定を変えた時：前回 Build で拾った場所に作り直す

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time) { m_Wind = wind; m_WindTime = time; }

    // 描画の最初（シャドウマップの前）に 1 回。gustA / gustB = GroundDebris::PackGusts の結果
    void Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* windSources,
        const DirectX::SimpleMath::Vector4* gustA, const DirectX::SimpleMath::Vector4* gustB, uint32_t gustCount);

    // cascade = シャドウマップの段（遠い段ほど葉を間引く）
    void RenderDepth(ID3D11DeviceContext* ctx, const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj, int cascade);
    void RenderBranches(Renderer& renderer, CameraBase& camera);
    void RenderLeaves(Renderer& renderer, CameraBase& camera);

    void DrawImGui();   // WindTreesPanel.cpp
    Settings& GetSettings() { return m_Settings; }
    const Stats& GetStats() const { return m_Stats; }
    bool Ready() const { return m_Ready && m_Settings.enabled; }
    int TreeCount() const { return (int)m_Sites.size(); }
    // 自動テスト用：木 i の根元と高さ（無ければ false）
    bool TreeSite(int i, DirectX::SimpleMath::Vector3& root, float& height) const;

    // HLSL の TreeCB（Shader/Tree/TreeCommon.hlsli）と同じ並び（176B）
    struct TreeCB
    {
        DirectX::SimpleMath::Matrix viewProj;
        DirectX::SimpleMath::Vector3 camPos; float time;
        DirectX::SimpleMath::Vector3 barkColor; float branchIdle;
        DirectX::SimpleMath::Vector3 leafColor; float branchWind;
        DirectX::SimpleMath::Vector3 leafColor2; float flutterIdle;
        DirectX::SimpleMath::Vector3 bareColor; float flutterWind;
        float crownMix, innerDark, leafWidth, shakeAmp;
        uint32_t leafStride; float leafScale; uint32_t leafCount; float pad;
    };
    // HLSL の SwayCB（Shader/Tree/TreeSwayCS.hlsl）と同じ並び（208B）
    struct SwayCB
    {
        float dt, time; uint32_t treeCount, gustCount;
        DirectX::SimpleMath::Vector2 windDir; float windSpeed, windGust;
        float windWaveScale, windWaveSpeed, windTime, gustSpeed;
        float leanPerWind2, maxLean, freq, damping;
        float windRef, turbulence, blastAccel, shakeDecay;
        DirectX::SimpleMath::Vector4 gustA[4];
        DirectX::SimpleMath::Vector4 gustB[4];
    };
    // HLSL の TreeState と同じ並び（64B）
    struct State
    {
        DirectX::SimpleMath::Vector3 root; float height;
        DirectX::SimpleMath::Vector2 lean, leanVel;
        DirectX::SimpleMath::Vector2 air; float windAmt, flex;
        DirectX::SimpleMath::Vector3 crown; float shake;
    };

private:
    struct Site
    {
        TreeBuilder::Kind kind;
        DirectX::SimpleMath::Vector3 root;
        float height, yawDeg;
        uint32_t seed;
    };

    void BuildGeometry();
    void FillCB(TreeCB& cb, const DirectX::SimpleMath::Matrix& viewProj, const DirectX::SimpleMath::Vector3& camPos) const;

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_SwayCS;
    std::shared_ptr<VertexShader> m_BranchVS, m_LeafVS;
    std::shared_ptr<PixelShader> m_BranchPS, m_LeafPS, m_LeafShadowPS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertBuf, m_IndexBuf, m_LeafBuf, m_StateBuf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_VertSRV, m_LeafSRV, m_StateSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_StateUAV;

    std::vector<Site> m_Sites;
    bool m_Ready = false;
    bool m_ReplaceModels = true;
    Stats m_Stats;
    Settings m_Settings;

    float m_PendingDt = 0.0f;
    float m_Time = 0.0f;
    WindState m_Wind;
    float m_WindTime = 0.0f;
};
