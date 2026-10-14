// ============================================================
// WeatherSystem.h
// 戦闘の 1 面の天候（2026-10-06、ユーザー：ゲームが平坦。まず天気から）。
//   ・時刻：1 面の進み（m_RunTime）に合わせて 昼 → 夕 → 夜 と照明一式（SceneLighting::Preset）を補間する。
//     毎局同じ（昼から始まり、制限時間で夕暮れ、最終ウェーブは夜）= 光で残り時間が分かる
//   ・天候の出来事：面毎に 1 種（草原 = 雨、砂漠 = 砂嵐、遺跡 = 濃霧 + 雷）。局の途中で 1〜2 回、
//     ゆっくり始まって 60〜90 秒続いて消える。照明（太陽・環境光・霧）を弱め、粒子（カメラに付いて回る）、
//     草の風、環境音（ループ）、雷（一瞬の白と音）
//   ・見やすさ：夜・雨の間は経験球と雑魚の体力バーを少し明るくする（ユーザー要望）
// 照明は毎フレーム SceneLighting::ApplyPreset で上書きする（Lighting パネルの手調整は効かなくなる。driveLighting で切れる）
// ============================================================
//   ・風（2026-10-12）：世界の風 WindState を作る。面の基本の風 + 出来事で強く（雨 ×2.4、砂嵐 ×3.2）+ 向きがゆっくり揺れる。
//     草・マント・粒子はシーン経由でこれを読む（以前は草の設定を直接書き換えていた）
#pragma once
#include "Graphics/Light/SceneLighting.h"
#include "World/WindState.h"
#include <SimpleMath.h>
#include <string>

struct StageDef;
class AreaVFXPlayer;
class SwarmSystem;
struct VFXContext;

class WeatherSystem
{
public:
    using Vector3 = DirectX::SimpleMath::Vector3;

    enum class Kind { None = 0, Rain, Sandstorm, Storm };   // Storm = 濃霧 + 雷（雨なし。遺跡）

    void Init(const StageDef& stage, float stageTime, SceneLighting& lighting, AreaVFXPlayer& vfx, const VFXContext* ctx,
        SwarmSystem* swarm);
    void Shutdown();
    // runTime = 遊んでいる時間（止まっている間は進まない）。dt = 実時間（粒子・フェードは止まっている間も動く）
    void Update(float dt, float runTime, const Vector3& camPos, const Vector3& camForward);

    // 今の天候の強さ 0〜1 と、夜の度合い 0〜1（HUD などが見たければ）
    float Intensity() const { return m_Intensity; }
    float Night() const { return m_Night; }
    Kind  CurrentKind() const { return m_Kind; }
    bool  Active() const { return m_State != State::Idle; }
    // 今フレームの世界の風（Update の後に読む）。雨の強さ（0〜1、雨の面だけ）は草の火を消す側が見る
    const WindState& Wind() const { return m_Wind; }
    float RainIntensity() const { return m_Kind == Kind::Rain ? m_Intensity : 0.0f; }

    void TriggerNow(float duration = -1.0f);   // 出来事を今始める（パネル・自動テスト）
    void StopNow();
    void DrawImGui();

    // ---- 調整 ----
    bool  driveLighting = true;     // 照明を時刻 + 天候で上書きする
    bool  eventsEnabled = true;     // 出来事を自動で起こす
    float firstEventMin = 90.0f, firstEventMax = 150.0f;   // 最初の出来事までの秒（遊んでいる時間）
    float repeatMin = 150.0f, repeatMax = 240.0f;          // 次の出来事まで
    float durationMin = 60.0f, durationMax = 90.0f;
    float fadeIn = 10.0f, fadeOut = 12.0f;
    float duskStart = 0.6f, duskEnd = 1.0f;   // 制限時間に対する割合：ここから夕暮れ → 夕
    float nightEnd = 1.1f;                    // 夕 → 夜が終わる所（制限時間 × これ）
    float readabilityBoost = 0.6f;            // 夜・天候の最大時に経験球・体力バーを明るくする割合
    float lightningMin = 6.0f, lightningMax = 16.0f;   // 雷の間隔（秒。Storm と雨の強い時）
    float timeOverride = -1.0f;               // >= 0 なら時刻をこの割合（0 昼 〜 1 夕 〜 1.1 夜）に固定（パネル・自動テスト）
    // 風（WindState の元）。出来事の間は strength が 1 + windEventMul × 強さ になる
    float windYawDeg = 30.0f;                 // 普段の向き（吹いていく方）
    float windSpeed = 1.2f, windGust = 0.8f;  // m/s
    float windDriftDeg = 15.0f;               // 向きの揺れの幅（±）
    float windDriftPeriod = 75.0f;            // 向きが一往復する秒数
    float windRainMul = 1.4f, windSandMul = 2.2f, windStormMul = 1.0f;   // 出来事の最大時に足す倍率（草の以前の値）

private:
    enum class State { Idle, FadeIn, Active, FadeOut };

    static SceneLighting::Preset Lerp(const SceneLighting::Preset& a, const SceneLighting::Preset& b, float t);
    void BuildTimeOfDay(const StageDef& stage);
    SceneLighting::Preset PresetAt(float phase) const;   // phase: 0 昼 / 1 夕 / nightEnd 夜
    void ApplyWeatherToPreset(SceneLighting::Preset& p, float w) const;
    void UpdateEvent(float dt, float runTime);
    void UpdateVfx(const Vector3& camPos, const Vector3& camForward);
    void UpdateAudio(float dt);
    void UpdateReadability();

    Kind m_Kind = Kind::None;
    int  m_Biome = 0;
    float m_StageTime = 600.0f;
    SceneLighting::Preset m_Day, m_Dusk, m_NightPreset;
    SceneLighting* m_Lighting = nullptr;
    AreaVFXPlayer* m_Vfx = nullptr;
    const VFXContext* m_Ctx = nullptr;
    SwarmSystem*   m_Swarm = nullptr;
    WindState      m_Wind;

    State m_State = State::Idle;
    float m_Intensity = 0.0f;       // 0〜1（フェード込み）
    float m_Night = 0.0f;
    float m_Phase = 0.0f;           // 今の時刻の割合
    float m_NextEventAt = 0.0f;     // 遊んでいる時間（秒）
    float m_StateLeft = 0.0f;       // 今の段階の残り秒
    float m_Duration = 0.0f;
    int   m_EventCount = 0;

    uint32_t m_VfxHandle = 0;       // 雨 / 砂嵐の粒子（カメラに付いて回る）
    float m_FlashLeft = 0.0f;       // 雷の白（秒）
    float m_NextLightning = 0.0f;
    float m_BaseOrbEmissive = 1.6f;
    DirectX::SimpleMath::Vector4 m_BaseHpFill = { 0.85f, 0.20f, 0.20f, 1.0f };
    std::string m_AmbientFile;
    float m_AmbientVolume = 0.0f;
    uint32_t m_Seed = 0;
};
