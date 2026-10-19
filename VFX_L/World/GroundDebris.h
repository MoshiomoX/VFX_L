// ============================================================
// GroundDebris.h
// 地面の落ち葉・草の切れ端・小枝（2026-10-13。ユーザー：「フィールドに風が触れる物を置いた方が、
// 風の場（突風）との繋がりが見えるのでは」→ 推奨した「地面の軽い物の層」）。
//   風の場そのものは目に見えない。見えるのは風が動かした物。突風のエフェクトは今まで草を倒すだけだったので、
//   普段から地面に転がっている軽い物を用意し、突風の扇・爆発・強い天候の風で一斉に舞い上がらせる。
//
//   仕組み：カメラの周り window m の正方形の窓に、世界に固定した格子（side × side マス）の各マスに 1 個。
//   格子は窓にトーラス状に巻き付く（GrassCullCS と同じ考え）ので、カメラが動くと窓から出た列が反対側に
//   「置き直され」、地面に静止した状態で現れる（窓の端は縮めて描くので飛び出さない）。
//   毎フレーム DebrisUpdateCS が 1 歩進める：静止中は（世界の風が liftWind を越える / 突風の扇に入る /
//   爆発の範囲に入る）と舞い上がり、空中では重力 + 空気の速度（風・扇・爆風）へ drag で寄る
//   （葉はゆっくり落ちて風に流される）。草の高さテクスチャ（GrassRenderer::GetHeightSRV）に着地して静止。
//   描画は DebrisVS/PS：1 個 = 小さな板（2 三角形）、PS で葉 / 細い帯の形を切り抜き、モデルと同じ光（トゥーン・影・霧）。
//   種類と色は面で変える（草原 = 枯葉・草屑・小枝、砂漠 = 砂色の破片・枯枝、遺跡 = 灰・紙片）。
//
// 使い方（シーン）：Initialize → 地形の後 Build(grid, seed, biome, 草の高さ SRV)（作り直しの度にも）
//   毎フレーム：Tick(dt)（止まっている間は呼ばない）、SetWind、AddGust（WeaponSystem::GetGustFx の分）、
//   草の後に Render(renderer, camera, 範囲の SRV)
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

class GroundDebris
{
public:
    struct Settings
    {
        bool  enabled = true;
        int   side = 80;            // 格子の一辺のマス数（個数 = side²。変えると buffer を作り直す）
        float window = 36.0f;       // カメラの周りの窓の半分の幅 m（間隔 = 2 × window / side）
        float density = 0.8f;       // マスの何割に置くか
        float sizeMul = 1.2f;       // 大きさの倍率（1.0 だと 10m のカメラから見えにくい）
        float fadeWidth = 5.0f;     // 窓の端でこの幅かけて縮める m
        float maxSlope = 0.9f;      // これより急な地面（高さ / 水平）には置かない
        float liftWind = 2.4f;      // 静止中の物が舞い上がる風速 m/s（普段の風 1.2 + 0.8 では舞わず、雨・砂嵐で舞う）
        float gravity = 9.8f;
        float drag = 4.0f;          // 1/s（終端速度 = gravity / drag ≈ 2.5 m/s）
        float gustSpeed = 13.0f;    // 突風の扇の中の空気の速さ m/s（強さ 1 の時）
        float gustTime = 0.35f;     // 突風の扇が効く秒数（1 → 0 に減衰）
        float blastAccel = 60.0f;   // 爆発の範囲の中の外向きの加速度 m/s²
        DirectX::SimpleMath::Vector3 colorLeaf = { 0.64f, 0.50f, 0.20f };   // 枯葉（明るめ。暗いと草の影に沈む）
        DirectX::SimpleMath::Vector3 colorClip = { 0.56f, 0.64f, 0.24f };   // 草の切れ端
        DirectX::SimpleMath::Vector3 colorTwig = { 0.40f, 0.28f, 0.14f };   // 小枝
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）。height = GrassRenderer::GetHeightSRV（高さ格子と同じ解像度の R32F）
    void Build(const GridWorld& grid, uint32_t seed, TerrainGenerator::Biome biome, ID3D11ShaderResourceView* height);

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time) { m_Wind = wind; m_WindTime = time; }
    // 突風（WeaponSystem::GustFx と同じ形）：dir / length が 0 なら pos の周りの円、それ以外は pos から dir へ伸びる扇。
    // gustTime 秒かけて弱まりながら効く
    void AddGust(const DirectX::SimpleMath::Vector3& pos, float radius,
        const DirectX::SimpleMath::Vector2& dir = DirectX::SimpleMath::Vector2::Zero, float length = 0.0f);

    // 草の後（アウトラインの後）：1 歩進めて描く。areas / areaStates / areaEnds = SwarmSystem::GetAreaSRV / GetAreaStateSRV /
    // GetAreaEndSRV（爆発と、光線のカプセルの終点）
    // windSources = SwarmSystem::GetWindSourceSRV（砂旋の風源。2026-10-19。null = 無し）
    void Render(Renderer& renderer, CameraBase& camera,
        ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* areaEnds,
        ID3D11ShaderResourceView* windSources = nullptr);

    void DrawImGui();   // GroundDebrisPanel.cpp
    Settings& GetSettings() { return m_Settings; }
    bool Ready() const { return m_Ready && m_Settings.enabled; }
    int  Count() const { return m_BufSide * m_BufSide; }

    // HLSL の DebrisCB（Shader/Debris/DebrisCommon.hlsli）と同じ並び（352B）
    struct DebrisCB
    {
        DirectX::SimpleMath::Matrix viewProj;
        DirectX::SimpleMath::Vector3 camPos; float dt;
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector2 windDir; float windSpeed, windGust;
        float windWaveScale, windWaveSpeed, windTime, window;
        uint32_t side; float spacing, liftWind, gravity;
        float drag, density, fadeWidth, sizeMul;
        DirectX::SimpleMath::Vector3 colorLeaf; float blastAccel;
        DirectX::SimpleMath::Vector3 colorClip; float gustSpeed;
        DirectX::SimpleMath::Vector3 colorTwig; float time;
        DirectX::SimpleMath::Vector4 gustA[4];   // (root.x, root.z, dir.x, dir.z)
        DirectX::SimpleMath::Vector4 gustB[4];   // (length, radius, strength, 0)
        uint32_t gustCount, seed, reset; float maxSlope;
    };
    // HLSL の Debris と同じ並び（64B）
    struct Debris
    {
        DirectX::SimpleMath::Vector3 pos; float restT;
        DirectX::SimpleMath::Vector3 vel; uint32_t cell;
        DirectX::SimpleMath::Vector3 rot; float air;
        DirectX::SimpleMath::Vector3 angVel; uint32_t seed;
    };

private:
    static constexpr int kMaxGusts = 4;

    bool CreateBuffer(int side);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_UpdateCS;
    std::shared_ptr<VertexShader>  m_VS;
    std::shared_ptr<PixelShader>   m_PS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;   // GrassRenderer の物（参照を持つ）
    int m_BufSide = 0;
    bool m_Ready = false;       // Build 済み
    bool m_ResetPending = true; // 次の Step で全部置き直す

    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;
    uint32_t m_Seed = 1;
    float m_PendingDt = 0.0f;
    float m_Time = 0.0f;
    WindState m_Wind;
    float m_WindTime = 0.0f;

    struct GustSrc
    {
        DirectX::SimpleMath::Vector3 pos;
        float radius;
        DirectX::SimpleMath::Vector2 dir;
        float length;
        float age;      // 0 → gustTime で消える
    };
    std::vector<GustSrc> m_Gusts;

    Settings m_Settings;
};
