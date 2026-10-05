// ============================================================
// GrassRenderer.h
// 野原の草（GPU で生やすローポリの葉）。風になびき、プレイヤーが踏むと倒れて、ゆっくり起き上がる。
//
//   配置: 世界に固定した格子（spacing m 毎）の各マスに 1 本、マスの中でハッシュでずらす
//         （カメラが動いても同じ場所に同じ葉 = 泳がない）。毎フレーム、カメラの周り maxDistance m の
//         窓だけを GrassCullCS が調べ、残った葉を append buffer に書き、DrawInstancedIndirect で 1 回描く。
//   間引き: fullDensityDistance m から先は距離で間引き、残った葉を太くする。
//           間引かれかけの葉は先に縮めるので、境目で葉が急に消えない。
//           土の坂道・外周・登れない台地（Build の grassMask）と、崖の面（高さ場が急な所）には生やさない。
//   葉の形: 1 枚 3 三角形（根元の四角が半分の高さで 0.6 倍に細り、先は三角）。両面。
//           色 = 地面の色（地形生成の GroundColor をテクスチャにした物）x 根元は暗く・先は明るく黄色寄り。
//           光は Lighting.hlsli（太陽の影・霧も効く）。草は影を落とさない。
//   動き: 風（風向きに流れる波 + 小さな揺れ）+ 踏み跡。踏み跡はフィールド全体を覆うテクスチャ（GrassTrampleCS）。
//         毎フレーム古い値を減衰させ、プレイヤーの足元の円（滑り中は大きい）で外向きに押す。
//         一時停止中は Update が呼ばれないので、風も踏み跡の回復も止まる。
//
// 使い方（シーン）:
//   Initialize → 地形生成の後 Build(grid, grassMask, seed)（作り直しの度にも）
//   UpdateGameplay で Update(dt, プレイヤーの位置, 接地, 滑り中)
//   不透明物（地形・置物）の後に Render
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include "World/TerrainGenerator.h"
#include <memory>
#include <vector>
#include <cstdint>

class Renderer;
class CameraBase;
class GridWorld;
class ComputeShader;
class VertexShader;
class PixelShader;

class GrassRenderer
{
public:
    struct Settings
    {
        bool  enabled = true;
        // 2026-09-28 ユーザー: 細かい葉をたくさん → 大きめの葉を少し疎らに、広く
        float spacing = 0.25f;              // 格子の間隔 m（近くは 1 / spacing^2 本 / m2）
        float maxDistance = 70.0f;          // カメラからこれより遠くは生やさない
        float fullDensityDistance = 18.0f;  // ここまでは全部。ここから maxDistance で farDensity まで間引く
        float farDensity = 0.2f;
        float heightMin = 0.28f;            // m
        float heightMax = 0.55f;
        float widthMin = 0.07f;             // 根元の幅 m
        float widthMax = 0.12f;
        float curlMax = 0.35f;              // 元々の曲がり（先のずれ / 高さ）
        float maxSlope = 1.0f;              // これより急な地面（高さ / 水平）には生やさない（45 度）
        DirectX::SimpleMath::Vector3 rootColor = { 0.55f, 0.60f, 0.50f };   // 地面の色に掛ける（根元）
        DirectX::SimpleMath::Vector3 tipColor = { 1.25f, 1.20f, 0.85f };    // （先）
        // 風
        float windStrength = 0.35f;         // 強い突風の時の先のずれ / 高さ
        float windSpeed = 1.6f;             // 波の速さ（rad/s）
        float windScale = 0.06f;            // 波の細かさ（1m あたりの波の数）
        float windYawDeg = 30.0f;           // 風が吹いていく向き（xz、度）
        // 踏み跡
        float trampleRadius = 0.7f;         // 歩いている時の足元の円 m
        float trampleSlideRadius = 1.1f;    // 滑り中
        float trampleRecover = 2.5f;        // 起き上がる速さ（秒。減衰の時定数）
        float trampleBend = 0.9f;           // 踏まれた所の先のずれ / 高さ
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の生成後（作り直しの度にも）: 高さ場・地面の色・生やすマスをテクスチャへ、踏み跡を消す。
    // grassMask は格子のマス毎（TerrainGenerator::Generate の outGrassMask）。空なら全部に生やす
    void Build(const GridWorld& grid, const std::vector<uint8_t>& grassMask, uint32_t seed,
        TerrainGenerator::Biome biome = TerrainGenerator::Biome::Grassland);   // biome: 床の色（GroundColor）

    // 毎フレームの gameplay 更新から（一時停止中は呼ばない）
    void Update(float dt, const DirectX::SimpleMath::Vector3& player, bool grounded, bool sliding);

    // 不透明物の後: 踏み跡の更新 → 生やして間引く CS → 描画
    void Render(Renderer& renderer, CameraBase& camera);

    void DrawImGui();

    Settings& GetSettings() { return m_Settings; }
    uint32_t LastBladeCount() const { return m_LastCount; }

    // HLSL の GrassCB（Shader/Grass/GrassCommon.hlsli）と同じ並び
    struct GrassCB
    {
        DirectX::SimpleMath::Matrix viewProj;
        DirectX::SimpleMath::Vector4 frustum[6];
        DirectX::SimpleMath::Vector3 camPos; float time;
        DirectX::SimpleMath::Vector2 mapOrigin; DirectX::SimpleMath::Vector2 mapSize;
        int32_t cellMin[2]; uint32_t cellCount[2];
        float spacing, maxDist, fullDist, farDensity;
        float heightMin, heightMax, widthMin, widthMax;
        float curlMax, maxSlope, windStrength, windSpeed;
        DirectX::SimpleMath::Vector2 windDir; float windScale, trampleBend;
        DirectX::SimpleMath::Vector3 rootColor; float trampleRadius;
        DirectX::SimpleMath::Vector3 tipColor; float trampleDecay;
        DirectX::SimpleMath::Vector3 player; uint32_t maxBlades;
    };
    // HLSL の GrassBlade と同じ並び
    struct GrassBlade
    {
        DirectX::SimpleMath::Vector3 pos; float height;
        float yaw, width, curl, shade;
    };

private:
    static constexpr uint32_t kMaxBlades = 262144;   // 8MB。近くを見下ろしても収まる（普段は数万本）
    static constexpr int kTrampleSize = 512;          // 踏み跡のテクスチャの辺（200m のフィールドで 0.39m / texel）

    bool CreateBuffers();

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_CullCS;
    std::shared_ptr<ComputeShader> m_TrampleCS;
    std::shared_ptr<VertexShader>  m_VS;
    std::shared_ptr<PixelShader>   m_PS;

    // 地形（Build）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;   // R32F、高さ格子と同じ解像度
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_GroundSRV;   // RGBA32F、rgb = 地面の色, a = 生やすか
    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;

    // 葉（毎フレーム CS が書く）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BladeBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_BladeUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_BladeSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Args;                     // { 9, 本数, 0, 0 }
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CountStaging[2];          // 本数の読み戻し（1 フレーム遅れ）
    int m_Frame = 0;
    uint32_t m_LastCount = 0;

    // 踏み跡（R16G16F の 2 枚を交互に読み書き）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_TrampleTex[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TrampleSRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TrampleUAV[2];
    int m_TrampleCur = 0;

    // Update から
    float m_Time = 0.0f;
    float m_PendingDt = 0.0f;
    DirectX::SimpleMath::Vector3 m_Player;
    float m_TrampleNow = 0.0f;   // 今フレームの踏む円の半径（浮いている時 0）

    Settings m_Settings;
};
