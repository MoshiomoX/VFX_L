// ============================================================
// MiasmaField.h
// 遺跡の瘴気（2026-11-03、ユーザーが「3D の体素の霧」を選んだ）。草原の草・砂漠の砂に当たる
// 「魔法で変えられる物」：地面を這う紫黒の霧。低い所に溜まり、風の魔法（突風・竜巻・渦）で吹き散らされ・
// 巻き上げられ、火で焼けて晴れ、しばらくすると地面からまた湧いてくる。
//
// 作り：カメラ（プレイヤー）の周りの 3D の濃さの箱（128 × 32 × 128、横 0.5m・縦 0.3m = 64m × 9.6m × 64m）。
//   縦は「地面からの高さ」（地形に沿う。段々の台地の上にも同じ厚さで乗る）。2 枚を交互に読み書き。
//   毎フレーム Shader/Miasma/MiasmaStepCS が 1 歩：
//     速度 = 世界の風 × windShare + 風源の表（WindSourceAir：渦・吸い込み・上昇・突風の押し）+ 沈む速さ
//            + 爆発（SWARM_AREA_SHAKE）の外向き + 火の上昇 + プレイヤーが押しのける
//     半ラグランジュ法で前の箱から運ぶ（箱が動いた分も同じ式で吸収する。箱の外から来る分 = 湧く量の目安）
//     湧く量の目安（低い所・斑の雑音・地面に近いほど濃い）へ regrow で戻り、目安より濃い分（巻き上がった柱）は decay で消える
//     火（レシピの heat > 0）の範囲では焼けて薄くなる。壁のマス（歩けないマス）の中は 0
//   描画（MiasmaFieldRender.cpp）：不透明な物・草・水の後、群れの重ね描き（警告の輪・体力バー）の前。
//     半分の解像度で光線を 40 歩進め（深度で止める、IGN のずらし）、濃さを段で切ってトゥーンの硬い縁に、
//     全解像度へ深度を見ながら（縁を保って）広げて HDR に重ねる。TAA が無いのでずらしの縞はぼかしで消す
//
// 使い方（シーン、CollisionTestSceneMiasma.cpp）：Initialize → 地形の後 Build(grid) → SetActive(遺跡か)
//   毎フレーム：Tick(dt)、SetWind / SetCenter（PushWind の中）、描画で Step(...) → Render(...)
// ============================================================
#pragma once
#include "World/WindState.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <cstdint>
#include <vector>

class GridWorld;
class ComputeShader;
class VertexShader;
class PixelShader;
class CameraBase;
class Graphics;

class MiasmaField
{
public:
    static constexpr int kNX = 128, kNY = 32, kNZ = 128;
    static constexpr float kCell = 0.5f;     // 横（m）
    static constexpr float kCellY = 0.3f;    // 縦（m、地面からの高さ）
    static constexpr float kTop = kNY * kCellY;   // 箱の天井（地面から m）
    static constexpr int kNoise = 48;        // 細かい雑音の図の一辺（繰り返す）
    static constexpr int kMaxTorches = 16;   // 霧に効く松明（プレイヤーに近い順）

    // 同じ作りで 2 種類（2026-11-03）：遺跡の瘴気（いつも地面に湧き、風で散る・火で焼ける）と
    // 砂漠の砂塵（普段は薄い砂の霞だけ。風源・砂嵐・爆発が砂を巻き上げると濃い砂の雲になり、ゆっくり落ちて消える）
    enum class Kind { Miasma = 0, Dust = 1 };

    struct Settings
    {
        bool  enabled = true;
        // ---- 動き ----
        float base = 1.25f;         // 湧く量の目安の濃さ（地面の所。単位は描画の extinction に掛かる）
        float layerH = 1.3f;        // 湧く霧の厚さ（m）。これより上は 0 へ
        float noiseScale = 0.08f;   // 斑の細かさ（1/m）
        float lowBias = 1.5f;       // 周りより低い所（窪み・部屋の床）ほど濃く
        float regrow = 0.12f;       // 晴れた所へ戻る速さ（1/秒）= 約 8 秒で戻る
        float decay = 0.45f;        // 目安より濃い所（巻き上がった柱）が消える速さ（1/秒）
        float settle = 0.0f;        // 重い霧が沈む速さ（m/s）。0.25 だと上の空の空気を毎フレーム引き下ろして地面の層が空になった（11-03）
        float windShare = 0.6f;     // 世界の風の何割で流れるか
        float sourceMul = 1.0f;     // 範囲の半径 × これ が霧に効く半径
        float burnRate = 3.0f;      // 火（heat 1）の範囲で焼ける速さ（1/秒）
        float blastPush = 14.0f;    // 爆発の範囲が外へ押す速さ（m/s、真ん中）
        float blastClear = 4.0f;    // 爆発の範囲の中で消える速さ（1/秒）
        float playerRadius = 1.4f;  // プレイヤーが押しのける半径（m）
        float playerPush = 2.5f;    // m/s
        float wallHeight = 4.0f;    // 壁のマスはこの高さまで霧が入らない
        // ---- 砂漠の砂塵（Kind::Dust、2026-11-03）：巻き上げられた時だけ濃くなる。瘴気では 0 ----
        float liftRate = 0.0f;      // 砂の面の「舞う強さ」（SandField の状態の w）1 で 1 秒に足す濃さ
        float blastDust = 0.0f;     // 爆発の範囲が 1 秒に足す濃さ（真ん中、低い所）
        float liftHeight = 1.5f;    // 足すのはこの高さまで（m）
        // ---- 見た目 ----
        float extinction = 1.4f;    // 濃さ 1 が 1m あたりどれだけ遮るか
        int   steps = 40;           // 光線の歩数
        float maxDist = 60.0f;      // カメラからこれより遠くは歩かない（m）
        float edgeFade = 6.0f;      // 箱の端の何 m で薄く（端で霧が切れて見えない）
        float bands = 3.0f;         // トゥーンの段の数
        float bandSoft = 0.08f;     // 段の境の柔らかさ（0 = 硬い）
        DirectX::SimpleMath::Vector3 colorLow = { 0.025f, 0.006f, 0.04f };   // 地面の近く（線形）
        DirectX::SimpleMath::Vector3 colorHigh = { 0.16f, 0.05f, 0.22f };   // 上の方（巻き上がった柱）
        float topBlend = 3.0f;      // colorLow → colorHigh へ移る高さ（m）
        float glow = 0.02f;         // 段の縁を少し明るく（紫の光）。0.12 は縁が光ってスライムの粘液に見えた（11-03 ユーザー）
        float upsampleDepth = 0.05f;   // 広げる時に隣を混ぜる深度の差（その画素の深度に対する割合）
        // ---- 細かい雑音で縁を崩す（2026-11-03、ユーザー：「スライムの粘液みたい」）----
        // 体積雲の作り：模擬の滑らかな濃さを、繰り返す 3D の雑音（値の雑音 + 反転 Worley）で削る。薄い縁は糸状に崩れ、濃い芯は残る
        bool  detail = true;
        float detailScale = 0.16f;  // 大きい方の雑音の細かさ（1/m。雑音の図 1 枚 = 約 6m）
        float detailScale2 = 0.55f; // 小さい方（約 1.8m）
        float detail2Mix = 0.35f;   // 小さい方の混ぜる割合
        float erode = 0.55f;        // 削る強さ（0 = 削らない）
        float densRef = 0.35f;      // この濃さ以上は芯（削られても残る）
        float detailRise = 0.35f;   // 雑音が上へ流れる速さ（m/s。湧いて立ち上る感じ）
        // ---- 松明（2026-11-05、遺跡）：周りの瘴気を焼いて晴らし（床の石が見えて点光源の暖かい光が落ちる）、霧を暖かく照らす ----
        bool  torches = true;
        float torchRadius = 2.8f;       // 晴らす半径（m、床の上の水平距離）
        float torchBurn = 2.0f;         // 焼ける速さ（1/秒、真ん中）。戻る速さ regrow と釣り合って穴の薄さが決まる
        float torchLift = 0.6f;         // 熱で昇る速さ（m/s）。穴の縁の霧が少し立ち上る
        float torchGlow = 1.2f;         // 松明の近くの霧が暖かく光る強さ
        float torchGlowRadius = 5.5f;   // 光が届く距離（m、松明からの 3D の距離）
        DirectX::SimpleMath::Vector3 torchColor = { 1.0f, 0.55f, 0.22f };   // 線形
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）：壁のマスの図を作り、次の Step で湧く量の目安に満たす
    void Build(const GridWorld& grid);
    void SetActive(bool on) { m_Active = on; }   // 遺跡（瘴気）と砂漠（砂塵）だけ（VFXL_MIASMA=1 で他の面でも）
    // 種類を変える（Build の前）。それぞれのパラメータは別々に覚えている（パネルで変えた値は面を替えても残る）
    void SetKind(Kind kind);
    Kind GetKind() const { return m_Kind; }

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time);
    // 箱の中心（プレイヤー）。地面の高さの範囲（光線の縦の範囲）も grid から測る
    void SetCenter(const DirectX::SimpleMath::Vector3& player, const GridWorld& grid);
    // 次の Step でこの円の霧を消す（パネル・自動テスト）。clear 0..1
    void Clear(const DirectX::SimpleMath::Vector3& pos, float radius);
    // 松明の位置（毎フレーム、SetCenter の後）。箱の近くの物から kMaxTorches 本、time で揺らぐ。空なら無し
    void SetTorches(const std::vector<DirectX::SimpleMath::Vector3>& torches, float time);

    // 描画の中、群れの Flush の後に 1 歩。areas / areaStates = SwarmSystem、recipeHeat = GrassFireField::GetHeatSRV、
    // windSources = SwarmSystem::GetWindSourceSRV、height = GrassRenderer::GetHeightSRV（GridWorld の高さ）
    void Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* recipeHeat,
        ID3D11ShaderResourceView* windSources, ID3D11ShaderResourceView* height, ID3D11ShaderResourceView* sandState = nullptr);
    // 不透明な物の後。深度を読むので DSV を外して描き、元に戻す
    void Render(ID3D11DeviceContext* context, Graphics& graphics, CameraBase* camera, ID3D11ShaderResourceView* height);

    void DrawImGui();
    Settings& GetSettings() { return m_Settings; }
    bool Active() const { return m_Active && m_Settings.enabled && m_Ready; }

    // TEMP-TEST: (x, z) の柱の濃さ（地面から縦に足した m 単位）と霧が届いている一番上（地面から m）。Map で GPU を待つ（自動テストだけ）
    bool DebugReadColumn(const DirectX::SimpleMath::Vector3& pos, float& sum, float& topH, float* cells = nullptr) const;
    // TEMP-TEST: 高さ y のセルの横の切り口を PGM（128 × 128、0..maxV を 0..255）に書く
    bool DebugDumpSlice(const char* path, int y, float maxV) const;
    int DebugTorchCount() const { return m_TorchCount; }   // TEMP-TEST: 霧に渡している松明の数

    // HLSL の MiasmaStepCB と同じ並び（432B）
    struct StepCB
    {
        DirectX::SimpleMath::Vector2 origin, prevOrigin;
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector2 wind; float dt, reset;
        DirectX::SimpleMath::Vector2 noiseOffset; float noiseScale, lowBias;
        float base, layerH, regrow, decay;
        float settle, windShare, sourceMul, burnRate;
        float blastPush, blastClear, wallHeight, playerPush;
        DirectX::SimpleMath::Vector3 player; float playerRadius;
        DirectX::SimpleMath::Vector4 clear;   // xz, 半径, 1 = 有効
        float liftRate, blastDust, liftHeight, pad;
        float torchCount, torchRadius, torchBurn, torchLift;
        DirectX::SimpleMath::Vector4 torch[kMaxTorches];   // xyz = 位置、w = 揺らぎ（0.7..1）
    };
    // HLSL の MiasmaMarchCB（496B）
    struct MarchCB
    {
        DirectX::SimpleMath::Matrix invViewProj;
        DirectX::SimpleMath::Vector3 camPos; float maxDist;
        DirectX::SimpleMath::Vector2 origin, mapOrigin;
        DirectX::SimpleMath::Vector2 mapSize, fullSize;
        float yMin, yMax, extinction, steps;
        DirectX::SimpleMath::Vector3 colorLow; float bands;
        DirectX::SimpleMath::Vector3 colorHigh; float bandSoft;
        float topBlend, glow, edgeFade, pad;
        DirectX::SimpleMath::Vector3 detailOffset; float detailScale;
        float erode, densRef, detailScale2, detail2Mix;
        DirectX::SimpleMath::Vector3 torchColor; float torchCount;
        float torchGlow, torchGlowRadius, tpad0, tpad1;
        DirectX::SimpleMath::Vector4 torch[kMaxTorches];
    };
    // HLSL の MiasmaCompositeCB（32B）
    struct CompositeCB
    {
        float depthA, depthB, tolerance, pad;
        DirectX::SimpleMath::Vector2 halfSize, fullSize;
    };

private:
    bool EnsureTargets(ID3D11Device* device, UINT width, UINT height);
    bool BuildNoise();

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_StepCS;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_MarchPS;
    std::shared_ptr<PixelShader> m_CompositePS;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Blend;   // 乗算済みアルファ

    Microsoft::WRL::ComPtr<ID3D11Texture3D> m_Vol[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_VolSRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_VolUAV[2];
    int m_Cur = 0;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SolidSRV;   // 歩けないマス（R8、GridWorld のマス）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_NoiseSRV;   // 繰り返す 3D の雑音（R8、kNoise³）
    DirectX::SimpleMath::Vector3 m_DetailOffset;                  // 雑音を風と上昇で流す（毎歩足す）
    mutable Microsoft::WRL::ComPtr<ID3D11Texture3D> m_Staging;     // 1 × 32 × 1（自動テスト）

    // 半分の解像度の光線の結果（rgb = 乗算済みの色、a = 1 − 透過）
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_HalfTex;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_HalfRTV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HalfSRV;
    UINT m_HalfW = 0, m_HalfH = 0;

    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;
    DirectX::SimpleMath::Vector2 m_Origin, m_PrevOrigin;   // 箱の南西の角（xz、セルに揃える）
    bool  m_HasOrigin = false;
    float m_YMin = 0.0f, m_YMax = kTop;   // 箱の中の地面の低い所 / 高い所 + 天井
    bool  m_Ready = false;
    bool  m_Active = false;
    bool  m_NeedReset = true;
    float m_PendingDt = 0.0f;
    DirectX::SimpleMath::Vector2 m_Wind;
    DirectX::SimpleMath::Vector2 m_NoiseOffset;   // 斑を世界の風で流す（運ばれる霧と目安がずれないように）
    DirectX::SimpleMath::Vector3 m_Player;
    DirectX::SimpleMath::Vector4 m_Clear;
    bool  m_ClearOn = false;
    DirectX::SimpleMath::Vector4 m_Torch[kMaxTorches];
    int   m_TorchCount = 0;

    Settings m_Settings;
    Kind m_Kind = Kind::Miasma;
    Settings m_Saved[2];   // 種類ごとの値（今の種類の分は m_Settings にある）
    static Settings DustPreset();
};
