// ============================================================
// DandelionField.h
// タンポポの綿毛（2026-10-22、ユーザー「風に影響される物を足すともっと面白い」→ 四択から「転がり草 / タンポポの綿毛」と
// 「マントが局所の風を受ける」。こちらは草原のタンポポ）。
//   草原の草の上に、白い綿毛の玉を付けた茎が群生（値ノイズの斑）で立っている。局所の風源（竜巻・渦・突風・砂旋）が
//   通る / 強い天候の風 / 草の踏み跡（プレイヤー・爆発・光線・突風の扇）で玉がほどけ、綿毛が風に乗って飛んでいく。
//   ほどけた茎は regrowDelay 秒後に玉が膨らみ直す。
//   置き方は地面の落ち葉（GroundDebris）と同じ：カメラの周りの窓に世界に固定した格子をトーラス状に巻き付け、
//   1 マス 1 本（斑の外・草の無い所・崖は隠す）。綿毛は 1 本に seedsPer 個（1 本のスレッドが自分の綿毛も動かす）。
//   描画は DandelionVS/PS：茎の帯・玉の円盤・綿毛の小さな板（頂点バッファ無し、DrawInstanced 1 回）、
//   不透明 + 切り抜き、モデルと同じ光（トゥーン・影・霧）。草の後（アウトラインの後）に描く。
//   面は草原だけ（Build の biome で決める）。一時停止中は Tick が来ない = 止まる
// 使い方（シーン）：Initialize → 地形の後 Build（作り直しの度にも）、毎フレーム Tick / SetWind、草の後に Render
// ============================================================
#pragma once
#include "World/WindState.h"
#include "World/TerrainGenerator.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <cstdint>

class Renderer;
class CameraBase;
class GridWorld;
class ComputeShader;
class VertexShader;
class PixelShader;

class DandelionField
{
public:
    struct Settings
    {
        // 2026-10-23 ユーザー：「合う粒子が無くて見た目が悪い、今はタンポポ要らない」→ 既定は切（コードは残す）。
        // Dandelions パネルの Enabled か、起動時の VFXL_DANDELIONS=1（TEMP-TEST。自動テスト windtoys は自分で入れる）で入る
        bool  enabled = false;
        int   side = 64;            // 格子の一辺（本数 = side²。変えると buffer を作り直す）
        int   seedsPer = 8;          // 1 本の綿毛の数（変えると buffer を作り直す）
        float window = 26.0f;        // カメラの周りの窓の半分の幅 m
        float density = 0.75f;       // 斑の中のマスの何割に生やすか
        float patchScale = 9.0f;     // 斑の大きさ m（14m / 32% だと開始地点の周りに 1 本も無いことがあった）
        float patchCover = 0.45f;    // 草地のうち斑になる割合
        float fadeWidth = 4.0f;      // 窓の端で縮める幅 m
        float maxSlope = 0.9f;
        float stemMin = 0.55f;       // 茎の高さ m（草 0.28〜0.55m の上へ玉が出る高さ）
        float stemMax = 0.85f;
        float puffRadius = 0.11f;    // 玉の半径 m
        float seedSize = 0.07f;      // 飛ぶ綿毛の半分の大きさ m（0.045 だと 22m のカメラで点にしか見えなかった）
        float sizeMul = 1.35f;       // 全体の大きさ（14m の高いカメラから見える大きさ）
        float releaseWind = 3.2f;    // 天候の風がこれを越えるとほどけ始める m/s（普段の 1.2〜2.0 ではほどけない、雨・砂嵐でほどける）
        float releaseLocal = 2.0f;   // 局所の風源の速さがこれを越えるとほどける m/s
        float releaseTrample = 0.45f;// 草の踏み跡の強さがこれを越えるとほどける（爆発・光線・突風の扇・雑魚の足 0.65）
        float playerRadius = 1.3f;   // プレイヤーの体からこの距離の玉はほどける（草の踏み跡 0.7m より広く。走り抜けると一筋が飛ぶ）
        float regrowDelay = 18.0f;   // ほどけてから膨らみ始めるまで 秒
        float regrowTime = 3.0f;     // 膨らみ切るまで 秒
        float seedDrag = 2.2f;       // 1/s（空気へ寄る速さ）
        float seedGravity = 0.7f;    // m/s²（終端の落下 = 0.3 m/s）
        float seedLifeMin = 4.0f;    // 秒
        float seedLifeMax = 8.0f;
        float seedLift = 1.6f;       // 局所の風源の吹き上げに掛ける倍率（竜巻で舞い上がる）
        float bendGain = 0.06f;      // 空気の速さ 1 m/s あたりの茎の傾き（茎 1m あたりの横ずれ m）
        DirectX::SimpleMath::Vector3 stemColor = { 0.20f, 0.42f, 0.08f };   // 線形
        DirectX::SimpleMath::Vector3 puffColor = { 0.95f, 0.95f, 0.92f };
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）。height / ground = GrassRenderer::GetHeightSRV / GetGroundSRV（ground の a = 草を生やすか）
    void Build(const GridWorld& grid, uint32_t seed, TerrainGenerator::Biome biome,
        ID3D11ShaderResourceView* height, ID3D11ShaderResourceView* ground);

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time) { m_Wind = wind; m_WindTime = time; }

    // 草の後：1 歩進めて描く。trample = GrassRenderer::GetTrampleSRV（今フレームの踏み跡）、
    // windSources = SwarmSystem::GetWindSourceSRV（null = 無し）、player = プレイヤーの体の中心（null = 居ない）
    void Render(Renderer& renderer, CameraBase& camera, ID3D11ShaderResourceView* trample, ID3D11ShaderResourceView* windSources,
        const DirectX::SimpleMath::Vector3* player);

    void DrawImGui();   // DandelionFieldPanel.cpp
    Settings& GetSettings() { return m_Settings; }
    bool Ready() const { return m_Ready && m_Settings.enabled && m_Active; }
    // 自動テスト用（TEMP-TEST）：全部の花を読み戻して、玉の付いた本数・ほどけた本数・飛んでいる綿毛の数を数える（GPU を待つ）
    // nearCenter / nearRadius / nearCount = その円の中に立っている（隠れていない）花の数
    bool DebugStats(int& full, int& bare, int& flying,
        const DirectX::SimpleMath::Vector3* nearCenter = nullptr, float nearRadius = 0.0f, int* nearCount = nullptr);

    // HLSL の DandelionCB（Shader/Dandelion/DandelionCommon.hlsli）と同じ並び（304B）
    struct DandelionCB
    {
        DirectX::SimpleMath::Matrix viewProj;
        DirectX::SimpleMath::Vector3 camPos; float dt;
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector2 windDir; float windSpeed, windGust;
        float windWaveScale, windWaveSpeed, windTime, window;
        uint32_t side; float spacing; uint32_t seedsPer; float time;
        float density, patchScale, patchCover, fadeWidth;
        float stemMin, stemMax, puffRadius, seedSize;
        float releaseWind, releaseLocal, releaseTrample, regrowDelay;
        float regrowTime, seedDrag, seedGravity, seedLifeMin;
        float seedLifeMax, seedLift, bendGain, maxSlope;
        DirectX::SimpleMath::Vector3 stemColor; uint32_t seed;
        DirectX::SimpleMath::Vector3 puffColor; uint32_t reset;
        DirectX::SimpleMath::Vector3 camRight; float sizeMul;
        DirectX::SimpleMath::Vector3 camUp; float pad;
        DirectX::SimpleMath::Vector3 playerPos; float playerRadius;
    };
    // HLSL の Flower（48B）/ Seed（32B）と同じ並び
    struct Flower
    {
        DirectX::SimpleMath::Vector3 pos; float puff;
        DirectX::SimpleMath::Vector2 bend; float regrow; uint32_t cell;
        uint32_t seed; float pad[3];
    };
    struct Seed
    {
        DirectX::SimpleMath::Vector3 pos; float age;
        DirectX::SimpleMath::Vector3 vel; float life;
    };

private:
    bool CreateBuffers(int side, int seedsPer);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_UpdateCS;
    std::shared_ptr<VertexShader>  m_VS;
    std::shared_ptr<PixelShader>   m_PS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FlowerBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_FlowerUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_FlowerSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SeedBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SeedUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SeedSRV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;   // GrassRenderer の物（参照を持つ）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_GroundSRV;
    int m_BufSide = 0;
    int m_BufSeeds = 0;
    bool m_Ready = false;
    bool m_Active = false;        // この面に生やすか（草原だけ）
    bool m_ResetPending = true;

    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;
    uint32_t m_Seed = 1;
    float m_PendingDt = 0.0f;
    float m_Time = 0.0f;
    WindState m_Wind;
    float m_WindTime = 0.0f;
    Settings m_Settings;
};
