// ============================================================
// WaterSurface.h
// 水たまりの水面（2026-10-27。ユーザー：「水面を作って」→ トゥーンで良い、ただし
// 「他に何の影響も無いのに水がずっと動いているのはおかしい」）。
//   以前（2026-10-19）は水たまりのマスに不透明な青い六面体を置いただけ（ただの青い床）。
//   ここでは、何も無い時の水面は静か（鏡のよう）で、動くのは原因がある時だけ：
//     ・風：世界の風（草と同じ突風の波）が calmWind を越えた所だけさざ波が立ち、風下へ流れる。
//       普段の弱い風では突風の山が通る所にうっすら、雨・砂嵐の強い風で全体に。
//       竜巻・渦・突風・砂旋の風源の周りは強く、その風の向きへ流れる（風源の表 = 草と同じ物）
//     ・波紋：プレイヤーの足・着地・雨粒・水の上の爆発が押した所から輪が広がり、岸で跳ね返って静まる。
//       フィールド全体 1024² の波動方程式（WaterRippleCS。x = 高さ、y = 1 歩前の高さ。2 枚を交互）
//   動かない物：岸からの距離で浅い → 深いのトゥーンの色の段、岸の泡の線、太陽の硬いハイライト、空の映り込み。
//   天候：雨 = CS が乱数で雨粒を落とす、砂嵐 = 濁る（砂色へ寄って不透明に）。
//   メッシュ：水たまりのマス毎に 4×4 の四角（0.5m）。頂点 = (位置, 岸からの距離)。
//   水面の高さ = 水たまりで下げる前の起伏 + TerrainBuild::kPondSurface（底は起伏 - pondDepth）。
//   描画は半透明（前乗算アルファ）・深度は読むだけ・影は落とさない（受ける）。
//
// 使い方（シーン）：Initialize → 地形の後 Build(grid, m_TerrainMap, biome)（作り直し・ステージ選択の切替の度にも）
//   毎フレーム：Tick(dt)（止まっている間は呼ばない）、SetWind / SetWeather / SetSky、
//   AddRipple（足・着地。PlayerVFXSystem）、草の後・群れの重ね描きの前に Render
// ============================================================
#pragma once
#include "World/WindState.h"
#include "World/TerrainGenerator.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class Renderer;
class CameraBase;
class GridWorld;
class ComputeShader;
class VertexShader;
class PixelShader;
namespace MapData { struct Map; }
namespace TerrainBuild { class ReliefField; }

class WaterSurface
{
public:
    using Vector2 = DirectX::SimpleMath::Vector2;
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Vector4 = DirectX::SimpleMath::Vector4;

    struct Settings
    {
        bool  enabled = true;
        // ---- 色（線形。Build が面ごとに入れ直す）----
        Vector3 shallowColor = { 0.10f, 0.42f, 0.46f };   // 岸の近く
        Vector3 deepColor = { 0.03f, 0.14f, 0.30f };      // 真ん中
        Vector3 foamColor = { 0.92f, 0.96f, 1.00f };      // 岸の泡・波の山
        Vector3 turbidColor = { 0.46f, 0.36f, 0.20f };    // 砂嵐の濁り
        float shallowAlpha = 0.72f;   // 岸の近くの不透明さ（底が透けて見える。0.55 だと水の下の岸の壁が白い帯に見えた）
        float deepAlpha = 0.90f;
        float deepDist = 2.6f;        // 岸からこの距離 m で一番深い色（段は 3 つ）
        float foamWidth = 0.08f;      // 岸の泡の線の幅 m（2026-10-27 ユーザー「縁が太い」で 0.2 → 0.08）
        float skyReflect = 0.6f;      // 空の映り込み（斜めに見る所ほど）
        float specStrength = 0.9f;    // 太陽のハイライト（1.6 だと波の斜面の閃きが真っ白になった）
        // ---- 風のさざ波（原因 = 風。風が弱い所は静か）----
        float calmWind = 1.4f;        // m/s。これより弱い風の所は鏡（普段の風 1.2 + 突風 0.8 の山でうっすら）
        float fullWind = 5.0f;        // m/s。ここで一番立つ（雨・砂嵐・竜巻の周り）
        float waveNormal = 0.45f;     // さざ波の傾き
        float waveScale = 1.4f;       // さざ波の細かさ（1m あたりの数）
        float waveFlow = 0.35f;       // 風速の何割でさざ波が風下へ流れるか
        float windDarken = 0.15f;     // さざ波の立った所が空を映さず少し暗くなる割合
        // ---- 波紋（波動方程式）----
        float rippleSpeed = 1.1f;     // 波の速さ m/s
        float rippleDamping = 0.9f;   // 1/秒（大きいほど早く静まる）
        float rippleNormal = 5.0f;    // 波紋の高さの傾き → 法線の倍率
        float rippleCrest = 0.010f;   // 波の山がこの高さ（m）を越えると白い線
        // 波紋を起こす物（強さ = 押し下げる深さ m）
        float stepStrength = 0.07f, stepRadius = 0.40f;   // 水の中の一歩
        float wadeStrength = 0.010f;  // 水の中を進む時（速さ 1 m/s・1 秒あたり。航跡）
        float landStrength = 0.16f, landRadius = 0.8f;    // 水への着地
        float rainRate = 2.5f;        // 雨 1 の時の雨粒（1 m² 1 秒あたり）
        float rainStrength = 0.025f;
        float blastStrength = 0.20f;  // 水の上の爆発（kAreaShake の範囲が出た瞬間）
        float areaStrength = 0.05f;   // その他の範囲が出た瞬間（毒瓶・泥沼の着弾など）
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後：地図の記録の水たまり（map.pond）からメッシュ・波紋のマスクを作る。水たまりが無ければ何も描かない
    void Build(const GridWorld& grid, const MapData::Map& map, TerrainGenerator::Biome biome);
    bool HasWater() const { return m_VertexCount > 0; }

    // (x, z) が水たまりの上なら水面の高さを返す（足の水しぶき・波紋の判定）
    bool WaterAt(float x, float z, float& surfaceY) const;

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time, const Vector4& wave2, float wave2Weight)
    {
        m_Wind = wind; m_WindTime = time; m_Wave2 = wave2; m_Wave2Weight = wave2Weight;
    }
    // rain = 雨の強さ 0〜1（雨粒の波紋）、turbid = 砂嵐の強さ 0〜1（濁り）
    void SetWeather(float rain, float turbid) { m_Rain = rain; m_Turbid = turbid; }
    void SetSky(const Vector3& zenith, const Vector3& horizon) { m_SkyZenith = zenith; m_SkyHorizon = horizon; }
    // 次の波の 1 歩でここを押し下げる（strength m、radius m）。1 フレームに kMaxImpulses 個まで（超えたら弱い物を捨てる）
    void AddRipple(const Vector3& pos, float radius, float strength);

    // 草の後：波紋を進めて描く。areas / areaStates = SwarmSystem::GetAreaSRV / GetAreaStateSRV（範囲が出た瞬間に波紋）、
    // windSources = SwarmSystem::GetWindSourceSRV（null = 無し）
    void Render(Renderer& renderer, CameraBase& camera,
        ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* windSources);

    void DrawImGui();   // WaterSurfacePanel.cpp
    Settings& GetSettings() { return m_Settings; }
    const Settings& GetSettings() const { return m_Settings; }
    bool Ready() const { return m_Settings.enabled && HasWater() && m_VS && m_PS; }

    // TEMP-TEST: 波紋の読み戻し（Map で GPU を待つ。自動テストだけ）。maxAbs = 一番大きい |高さ| m、
    // moving = |高さ| が rippleCrest × 0.25 を越えている texel の数
    bool DebugReadRipples(float& maxAbs, uint32_t& moving) const;
    uint32_t PondCells() const { return m_PondCells; }

    static constexpr int kRippleSize = 1024;   // 波紋のテクスチャの辺（200m のフィールドで 0.2m / texel）
    static constexpr int kMaxImpulses = 16;

    // HLSL の WaterCB（Shader/Water/WaterCommon.hlsli）と同じ並び（288B）
    struct WaterCB
    {
        DirectX::SimpleMath::Matrix viewProj;
        Vector3 camPos; float time;
        Vector2 mapOrigin, mapSize;
        Vector2 windDir; float windSpeed, windGust;
        float windScale, windWaveSpeed, windTime, wave2Weight;
        Vector4 wave2;
        Vector4 shallow;    // rgb, a = 不透明さ
        Vector4 deep;
        Vector4 foam;       // rgb, a = 岸の泡の幅 m
        Vector4 turbid;     // rgb, a = 濁りの量
        float deepDist, calmWind, fullWind, waveNormal;
        float waveScale, waveFlow, rippleNormal, rippleCrest;
        float skyReflect, specStrength, windDarken, hasSources;
        Vector4 skyZenith;
        Vector4 skyHorizon;
    };
    // HLSL の WaterRippleCB（Shader/Water/WaterRippleCS.hlsl）と同じ並び（336B）
    struct RippleCB
    {
        Vector2 mapOrigin, mapSize;
        float courant2, keep, restore, texel;
        uint32_t step, sources; float rainProb, rainCell;
        float rainStrength, rainRadius, blastStrength, areaStrength;
        uint32_t impulseCount; float _pad[3];
        Vector4 impulse[kMaxImpulses];   // x, z, radius, strength
    };

private:
    bool CreateRippleTargets();   // WaterRipples.cpp
    void BuildMask(const TerrainBuild::ReliefField& rf);   // WaterRipples.cpp
    void StepRipples(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates);   // WaterRipples.cpp
    void ClearRipples();          // WaterRipples.cpp

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<VertexShader>  m_VS;
    std::shared_ptr<PixelShader>   m_PS;
    std::shared_ptr<ComputeShader> m_RippleCS;

    // 水面のメッシュ（float4 = 位置 + 岸からの距離。3 頂点で 1 三角形、索引無し）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VertBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_VertSRV;
    uint32_t m_VertexCount = 0;
    uint32_t m_PondCells = 0;

    // 波紋（R16G16F × 2 を交互）と水のマスク（R8。1 = 水たまり）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_RippleTex[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_RippleSRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_RippleUAV[2];
    int m_Cur = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_MaskTex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_MaskSRV;
    mutable Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Staging;   // DebugReadRipples 用

    // CPU の問い合わせ（WaterAt）：起伏の面と水たまりのマス
    std::shared_ptr<TerrainBuild::ReliefField> m_Relief;

    Vector2 m_MapOrigin, m_MapSize;
    float m_PendingDt = 0.0f;
    float m_SimAccum = 0.0f;
    float m_Time = 0.0f;
    uint32_t m_Step = 0;
    WindState m_Wind;
    float m_WindTime = 0.0f;
    Vector4 m_Wave2 = Vector4::Zero;
    float m_Wave2Weight = 0.0f;
    float m_Rain = 0.0f, m_Turbid = 0.0f;
    Vector3 m_SkyZenith = { 0.09f, 0.30f, 0.85f };
    Vector3 m_SkyHorizon = { 0.51f, 0.71f, 0.91f };
    std::vector<Vector4> m_Impulses;   // 次の 1 歩で使う（x, z, radius, strength）

    Settings m_Settings;
};
