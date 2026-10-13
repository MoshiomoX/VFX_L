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
//         風の向き・速さ・波は世界の風 WindState（WeatherSystem が作る。2026-10-12）。ここにあるのは「葉がどれだけ曲がるか」だけ
//   燃え跡（2026-10-12、GrassFireField）: 温度・燃料のテクスチャを SetFire で受け取り、葉が燃料の減りで黒く短くなり、
//         熱い所の先が橙に光る。渡されなければ以前のまま
//
// 使い方（シーン）:
//   Initialize → 地形生成の後 Build(grid, grassMask, seed)（作り直しの度にも）
//   UpdateGameplay で Update(dt, プレイヤーの位置, 接地, 滑り中)、SetWind(世界の風)
//   不透明物（地形・置物）の後に Render
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include "World/TerrainGenerator.h"
#include "World/WindState.h"
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
        // 風：世界の風の強さ 1 の時、強い突風での先のずれ / 高さ（向き・速さ・波は WindState）
        float windStrength = 0.35f;
        // 燃え跡（GrassFireField の温度 / 燃料から）
        float fireShrink = 0.65f;           // 燃料が尽きた所の葉の縮み（高さ × (1 - これ)）
        float fireGlow = 2.0f;              // 熱い所の先の橙の明るさ（HDR）
        // 踏み跡
        float trampleRadius = 0.7f;         // 歩いている時の足元の円 m
        float trampleSlideRadius = 1.1f;    // 滑り中
        float trampleRecover = 2.5f;        // 起き上がる速さ（秒。減衰の時定数）
        float trampleBend = 0.9f;           // 踏まれた所の先のずれ / 高さ
        // GPU の群れからの押し倒し（2026-10-13、ユーザー：「全部の特効の環境への干渉を強く」）
        float blastTrample = 1.3f;          // 揺れる範囲（爆発）の半径 × これ の円を外向きに倒す（0 = しない）
        float beamTrample = 2.0f;           // 光線のカプセルの半径 × これ の帯を横へ倒す（0 = しない）
        float enemyTrample = 0.65f;         // 雑魚の足元の押し倒しの強さ（0 = しない。プレイヤーは 1）
        float enemyRadius = 0.45f;          // 雑魚の足元の円 m（エリート 2.2 倍・ボス 3 倍・重装 1.3 倍）
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の生成後（作り直しの度にも）: 高さ場・地面の色・生やすマスをテクスチャへ、踏み跡を消す。
    // grassMask は格子のマス毎（TerrainGenerator::Generate の outGrassMask）。空なら全部に生やす
    void Build(const GridWorld& grid, const std::vector<uint8_t>& grassMask, uint32_t seed,
        TerrainGenerator::Biome biome = TerrainGenerator::Biome::Grassland);   // biome: 床の色（GroundColor）

    // 毎フレームの gameplay 更新から（一時停止中は呼ばない）
    void Update(float dt, const DirectX::SimpleMath::Vector3& player, bool grounded, bool sliding);
    // 世界の風（WeatherSystem::Wind）。毎フレーム渡す
    void SetWind(const WindState& wind) { m_Wind = wind; }
    // 燃え跡：温度（x）・燃料（y）のテクスチャ（GrassFireField::GetSRV）と、燃えていると見なす温度。null = 無し
    void SetFire(ID3D11ShaderResourceView* fire, float ignite) { m_FireSRV = fire; m_FireIgnite = ignite; }
    // 突風（2026-10-12）：次の描画で pos の周り radius の草を外向きに一斉に倒す（踏み跡と同じ仕組み。踏み跡の回復で起き上がる）
    // 突風：このフレームに 1 回、踏み跡の図へ押し倒しの型を押す。dir / length が 0 なら pos を中心に外向きの円盤、
    // それ以外は pos から dir へ length m 伸びる扇（根元は細く、先で半径 radius）で、草は風下へ倒れる（2026-10-13）
    void AddGust(const DirectX::SimpleMath::Vector3& pos, float radius,
        const DirectX::SimpleMath::Vector2& dir = DirectX::SimpleMath::Vector2::Zero, float length = 0.0f)
    {
        m_GustPos = pos; m_GustRadius = (std::max)(m_GustRadius, radius); m_GustDir = dir; m_GustLength = length;
    }

    // GPU の群れ（SwarmSystem）の池（2026-10-13）：揺れる範囲（爆発）と光線のカプセルは踏み跡に押し型を押し、
    // 雑魚の足元は GrassStampCS が押す。Render の前に毎フレーム渡す（Flush の後、SRV として読むだけ）。null = その分はしない
    void SetSwarmSources(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* areaEnds,
        ID3D11ShaderResourceView* enemies, ID3D11ShaderResourceView* enemyStates, ID3D11ShaderResourceView* enemyExtra)
    {
        m_AreaSRV = areas; m_AreaStateSRV = areaStates; m_AreaEndSRV = areaEnds;
        m_EnemySRV = enemies; m_EnemyStateSRV = enemyStates; m_EnemyExtraSRV = enemyExtra;
    }

    // 不透明物の後: 踏み跡の更新 → 生やして間引く CS → 描画
    void Render(Renderer& renderer, CameraBase& camera);

    void DrawImGui();

    // TEMP-TEST（自動テスト用）: 踏み跡のテクスチャを読み戻し、centerXZ の周り radius m の中で強さ ≥ minStrength の texel を数える。
    // Map で GPU を待つので毎フレームは呼ばない
    bool DebugTrampleStats(const DirectX::SimpleMath::Vector2& centerXZ, float radius, float minStrength,
        int& count, float& maxStrength);

    Settings& GetSettings() { return m_Settings; }
    uint32_t LastBladeCount() const { return m_LastCount; }
    float Time() const { return m_Time; }   // 風の波の時計（粒子と波を揃える）
    // 地形のテクスチャ（Build）。燃え跡の計算（GrassFireField）が同じ物を読む
    ID3D11ShaderResourceView* GetHeightSRV() const { return m_HeightSRV.Get(); }
    ID3D11ShaderResourceView* GetGroundSRV() const { return m_GroundSRV.Get(); }   // a = 生やすか（= 燃える物があるか）

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
        float fireIgnite, fireShrink, fireGlow, fireOn;   // 燃え跡（fireOn = 0 なら読まない）
        DirectX::SimpleMath::Vector3 gustPos; float gustRadius;   // 突風（0 = 無し）
        DirectX::SimpleMath::Vector2 gustDir; float gustLength; float gustPad;   // 前へ吹く突風（length 0 = 円盤）
        DirectX::SimpleMath::Vector4 trampleExtra;   // 爆発の半径倍率 / 光線の帯の半幅倍率 / 雑魚の足の強さ / 半径（2026-10-13）
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
    static constexpr uint32_t kMaxEnemyThreads = 4096; // = Swarm::kMaxEnemies（GrassStampCS は池の大きさを自分で見て余りは返る）

    bool CreateBuffers();

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_CullCS;
    std::shared_ptr<ComputeShader> m_TrampleCS;
    std::shared_ptr<ComputeShader> m_StampCS;    // 雑魚の足元（GrassStampCS）
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
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_TrampleStaging;   // DebugTrampleStats（TEMP-TEST）

    // Update から
    float m_Time = 0.0f;
    float m_PendingDt = 0.0f;
    DirectX::SimpleMath::Vector3 m_Player;
    float m_TrampleNow = 0.0f;   // 今フレームの踏む円の半径（浮いている時 0）
    WindState m_Wind;
    ID3D11ShaderResourceView* m_FireSRV = nullptr;   // 非所有（GrassFireField の物。毎フレーム SetFire で渡される）
    float m_FireIgnite = 0.4f;
    DirectX::SimpleMath::Vector3 m_GustPos;
    float m_GustRadius = 0.0f;   // 次の Render で 1 回だけ押して 0 に戻す
    DirectX::SimpleMath::Vector2 m_GustDir;
    float m_GustLength = 0.0f;
    // 群れの池（非所有。毎フレーム SetSwarmSources で渡される。null = 無し）
    ID3D11ShaderResourceView* m_AreaSRV = nullptr;
    ID3D11ShaderResourceView* m_AreaStateSRV = nullptr;
    ID3D11ShaderResourceView* m_AreaEndSRV = nullptr;
    ID3D11ShaderResourceView* m_EnemySRV = nullptr;
    ID3D11ShaderResourceView* m_EnemyStateSRV = nullptr;
    ID3D11ShaderResourceView* m_EnemyExtraSRV = nullptr;

    Settings m_Settings;
};
