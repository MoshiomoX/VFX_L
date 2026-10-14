// ============================================================
// AutoTestWeather.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=weather（2026-10-06）
// 時刻と天候の出来事を撮る：湧き停止・無敵・詠唱停止、カメラは普段の位置から少し引く。
//   day（時刻 0）→ dusk（0.95）→ night（1.1）→ 昼に戻して出来事を今始める → event（強さ 1 になってから）→
//   eventNight（出来事のまま夜）→ 出来事を止めて `weather done`。
// 各段で `weather look <名>` の後に照明の値（太陽の明るさ・環境光・霧）を 1 行。第 2・3 面は VFXL_STAGE=2 / 3
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "World/WeatherSystem.h"

namespace
{
    class AutoTestWeather final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };
}

void AutoTestWeather::Run()
{
    static int s_Shot = 0;
    static float s_Next = 0.0f;
    char line[200];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        auto& cam = m_Camera.Camera();
        cam.distance = 12.0f;
        cam.SetPitch(22.0f);
        cam.SnapToTarget();
        m_Weather.eventsEnabled = false;   // 自分で始める
        m_Weather.fadeIn = 2.0f;
        m_Weather.fadeOut = 2.0f;
        s_Shot = 0;
        s_Next = m_AutoTime;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1 || m_AutoTime < s_Next) return;

    auto logLight = [&](const char* tag)
        {
            snprintf(line, sizeof(line), "weather light %s kind %d intensity %.2f night %.2f", tag,
                (int)m_Weather.CurrentKind(), m_Weather.Intensity(), m_Weather.Night());
            AutoTestLog(line);
        };
    switch (s_Shot)
    {
    case 0: m_Weather.timeOverride = 0.0f;  logLight("day");   AutoTestLog("weather look day");   s_Next = m_AutoTime + 1.5f; break;
    case 1: m_Weather.timeOverride = 0.95f; logLight("dusk");  AutoTestLog("weather look dusk");  s_Next = m_AutoTime + 1.5f; break;
    case 2: m_Weather.timeOverride = 1.1f;  logLight("night"); AutoTestLog("weather look night"); s_Next = m_AutoTime + 1.5f; break;
    case 3: m_Weather.timeOverride = 0.2f;  m_Weather.TriggerNow(30.0f); s_Next = m_AutoTime + 4.0f; break;   // 2 秒で強さ 1、粒子が溜まるまで待つ
    case 4: logLight("event"); AutoTestLog("weather look event"); s_Next = m_AutoTime + 1.5f; break;
    case 5: m_Weather.timeOverride = 1.1f;  s_Next = m_AutoTime + 1.5f; break;
    case 6: logLight("eventNight"); AutoTestLog("weather look eventNight"); s_Next = m_AutoTime + 1.5f; break;
    case 7: m_Weather.StopNow(); s_Next = m_AutoTime + 2.5f; break;
    default:
        snprintf(line, sizeof(line), "weather done stage %d", m_StageIndex);
        AutoTestLog(line);
        AutoTestLog("weather done");
        m_AutoStep = 2;
        return;
    }
    ++s_Shot;
}

REGISTER_BATTLE_AUTOTEST("weather", AutoTestWeather)
