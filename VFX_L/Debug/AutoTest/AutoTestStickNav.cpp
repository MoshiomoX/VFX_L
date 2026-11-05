// ============================================================
// AutoTestStickNav.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=sticknav（2026-11-05）
// メニューの左スティック（UI/StickNav）。StickNav::DebugSetStick でスティックの値を差し替える。
//   unit   : StickNav だけを 1/60 秒刻みで回す。出た時に倒したまま 0.5 秒 → 0 回、戻して倒す → 1 回、
//            倒し続けて 1 秒 → 1 + 連続（0.4 秒の後 0.15 秒毎 = 5 回前後）、0.45 まで戻しても続ける（倒し直しにならない）
//   pause  : 一時停止を開いた時に倒したまま → 動かない、戻して倒す → 1 つ下、倒し続ける → 連続
//   settings / howto : 設定の行が 1 つ下へ、遊び方のページが 1 枚進む
//   training : トレーニングのメニュー。右に 1 回 = ボタン 1 つ右、下に 1 回 = 1 行下、倒し続け 0.65 秒 = 3 行下（2026-11-05）
//   選択画面・ショップのスティックは VFXL_SELECT_AUTOTEST / VFXL_SHOP_AUTOTEST の中で見る（`select stick` / `shop stick`）
// 行：`sticknav unit ...` / `sticknav <場面> ...` / `sticknav look <名>` / `sticknav done`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "UI/StickNav.h"
#include "UI/PauseMenuUI.h"

namespace
{
    class AutoTestStickNav final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }
        bool SelfClock() const override { return true; }

    private:
        void Run(float dt);
        void Unit();
    };

    using Vec2 = DirectX::SimpleMath::Vector2;
}

void AutoTestStickNav::Unit()
{
    StickNav nav;
    const float h = 1.0f / 60.0f;
    auto run = [&](const Vec2& s, float sec, int& pressed, int& repeat)
    {
        StickNav::DebugSetStick(s, true);
        for (float t = 0.0f; t < sec; t += h)
        {
            nav.Update(h);
            if (nav.Pressed(StickNav::kDown)) ++pressed;
            if (nav.Repeat(StickNav::kDown)) ++repeat;
        }
    };
    int p0 = 0, r0 = 0, p1 = 0, r1 = 0, p2 = 0, r2 = 0, pz = 0, rz = 0;
    run(Vec2(0.0f, -1.0f), 0.5f, p0, r0);    // 出た時に倒したまま
    run(Vec2(0.0f, 0.0f), 0.1f, pz, rz);     // 戻す
    run(Vec2(0.0f, -0.9f), 1.0f, p1, r1);    // 倒し続ける
    run(Vec2(0.0f, -0.45f), 0.3f, p2, r2);   // 少し戻す（release 0.3 より上 = 続く）
    StickNav::DebugSetStick(Vec2::Zero, false);
    char line[200];
    snprintf(line, sizeof(line), "sticknav unit heldAtOpen pressed %d repeat %d | push1s pressed %d repeat %d | ease pressed %d repeat %d",
        p0, r0, p1, r1, p2, r2);
    AutoTestLog(line);
}

void AutoTestStickNav::Run(float dt)
{
    m_AutoTime += dt;   // SelfClock：時計は自分で進める
    const float t = m_AutoTime;
    auto at = [&](float s) { return t >= s && t - dt < s; };
    auto in = [&](float a, float b) { return t >= a && t < b; };
    char line[200];
    const auto& pause = m_GameUI.TestPause();

    if (at(1.0f))
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        Unit();
    }
    if (t < 1.5f) return;

    // スティックの値（区間ごと）
    Vec2 s(0.0f, 0.0f);
    if (in(1.5f, 2.2f)) s = Vec2(0.0f, -1.0f);   // 開いた時に倒したまま（走っていた）
    if (in(2.4f, 2.5f)) s = Vec2(0.0f, -1.0f);   // 1 回
    if (in(2.8f, 3.45f)) s = Vec2(0.0f, -1.0f);   // 倒し続ける 0.65 秒 = 1 回 + 連続 2 回（0.40 / 0.55 秒）→ 3 つ下
    if (in(4.3f, 4.4f)) s = Vec2(0.0f, -1.0f);   // 設定：1 つ下
    if (in(5.2f, 5.3f)) s = Vec2(1.0f, 0.0f);    // 遊び方：次のページ
    StickNav::DebugSetStick(s, true);

    if (at(1.5f)) m_GameUI.TestShow(2);
    if (at(2.2f)) { snprintf(line, sizeof(line), "sticknav pause heldAtOpen cursor %d", pause.TestListCursor()); AutoTestLog(line); }
    if (at(2.7f))
    {
        snprintf(line, sizeof(line), "sticknav pause tap cursor %d", pause.TestListCursor());
        AutoTestLog(line);
        AutoTestLog("sticknav look pause");
    }
    if (at(3.9f)) { snprintf(line, sizeof(line), "sticknav pause hold065 cursor %d (expect 4)", pause.TestListCursor()); AutoTestLog(line); }

    if (at(4.0f)) m_GameUI.TestShow(3);
    if (at(4.6f))
    {
        snprintf(line, sizeof(line), "sticknav settings cursor %d", pause.TestSettingsCursor());
        AutoTestLog(line);
        AutoTestLog("sticknav look settings");
    }
    if (at(4.9f)) m_GameUI.TestShow(4);
    if (at(5.5f))
    {
        snprintf(line, sizeof(line), "sticknav howto page %d", pause.TestHowToPage());
        AutoTestLog(line);
        AutoTestLog("sticknav look howto");
    }
    // トレーニングのメニュー（2026-11-05 追加）：右に 1 回 = 「的を置く」の 2 つ目のボタン、下に 1 回 = 行 1、倒し続け 0.65 秒 = 行 4
    if (in(6.5f, 6.6f)) s = Vec2(1.0f, 0.0f);
    if (in(6.8f, 6.9f)) s = Vec2(0.0f, -1.0f);
    if (in(7.1f, 7.75f)) s = Vec2(0.0f, -1.0f);
    StickNav::DebugSetStick(s, true);
    auto& tr = m_GameUI.TestTraining();
    if (at(6.0f)) { m_GameUI.TestShow(0); m_GameUI.TestOpenTraining(true); }
    if (at(6.45f)) { snprintf(line, sizeof(line), "sticknav training open row %d col %d", tr.TestRow(), tr.TestCol()); AutoTestLog(line); }
    if (at(6.7f)) { snprintf(line, sizeof(line), "sticknav training right col %d (expect 1)", tr.TestCol()); AutoTestLog(line); }
    if (at(7.0f)) { snprintf(line, sizeof(line), "sticknav training down row %d (expect 1)", tr.TestRow()); AutoTestLog(line); }
    if (at(7.85f))
    {
        snprintf(line, sizeof(line), "sticknav training hold065 row %d (expect 4)", tr.TestRow());
        AutoTestLog(line);
        AutoTestLog("sticknav look training");
    }
    if (at(8.2f))
    {
        m_GameUI.TestOpenTraining(false);
        m_GameUI.TestShow(0);
        StickNav::DebugSetStick(Vec2::Zero, false);
        AutoTestLog("sticknav done");
    }
}

REGISTER_BATTLE_AUTOTEST("sticknav", AutoTestStickNav)
