// ============================================================
// AutoTestPadBag.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=padbag
// バックパック画面のパッド操作（2026-10-05、UI/BackpackPadControl）を、パッドの代わりの入力（TestPress）で一通り動かす
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "UI/BackpackPadControl.h"

namespace
{
    class AutoTestPadBag final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }
        bool SelfClock() const override { return true; }

    private:
        void Run(float dt);
        void LogState(const char* what);
        int m_Next = 0;   // 次に実行する手順
    };

    using Pad = BackpackPadControl;

    // 時刻（秒）・向き・ボタン・スクリーンショットの名前（look が有る行は入力しない）
    struct PadStep { float t; int dx, dy; unsigned buttons; const char* look; const char* note; };
    const PadStep kSteps[] = {
        { 2.0f,  0,  0, 0,        "cursor",   nullptr },                       // カーソルは (4,4) の追尾弾の上。説明カードが出ている
        { 2.2f,  0,  0, Pad::kA,  nullptr,    "grab homing" },
        { 2.4f,  0, -1, 0,        nullptr,    "up -> 3,4 (fits)" },
        { 2.9f,  0,  0, 0,        "carry",    nullptr },
        { 3.2f,  0, -1, 0,        nullptr,    "up -> 2,4 (no frame)" },
        { 3.4f,  0,  0, Pad::kA,  nullptr,    "A on red: must keep carrying" },
        { 3.8f,  0,  0, 0,        "red",      nullptr },
        { 4.1f,  0,  0, Pad::kRB, nullptr,    "rotate" },
        { 4.3f,  0,  1, 0,        nullptr,    "down -> 3,4" },
        { 4.5f,  0,  0, Pad::kA,  nullptr,    "place vertical at 3,4" },
        { 4.7f,  0,  0, Pad::kY,  nullptr,    "focus book" },
        { 5.4f,  0,  0, 0,        "book",     nullptr },
        { 5.7f,  1,  0, 0,        nullptr,    "book right" },
        { 5.9f, -1,  0, 0,        nullptr,    "book left" },
        { 6.1f,  0,  0, Pad::kA,  nullptr,    "take out of book" },
        { 6.3f, -1,  0, 0,        nullptr,    "left -> 3,3" },
        { 6.5f,  0,  1, 0,        nullptr,    "down -> 4,3" },
        { 6.9f,  0,  0, 0,        "frombook", nullptr },
        { 7.2f,  0,  0, Pad::kA,  nullptr,    "place stone at 4,3" },
        { 7.4f,  0,  0, Pad::kX,  nullptr,    "X: return stone to book" },
        { 7.6f,  0,  0, Pad::kA,  nullptr,    "grab frame under 4,3" },
        { 7.8f,  0,  0, Pad::kB,  nullptr,    "B: cancel (frame stays)" },
        { 8.0f,  0,  0, Pad::kB,  nullptr,    "B: close" },
        { 8.4f,  0,  0, 0,        nullptr,    "closed?" },
    };
}

// 状態を 1 行：フォーカス・カーソル・箱の選択・掴んでいる物・置いてある魔法の一覧
void AutoTestPadBag::LogState(const char* what)
{
    const auto& pad = m_GameUI.TestPad();
    const auto& drag = m_GameUI.GetDrag();
    const auto& bp = m_Registry.Get<BackpackComponent>(m_Player);

    char items[200] = "";
    for (const auto& it : bp.items)
    {
        char one[32];
        snprintf(one, sizeof(one), " %d@%d,%d/%d", (int)it.id, it.row, it.col, it.rotation);
        strncat_s(items, one, _TRUNCATE);
    }

    char line[400];
    snprintf(line, sizeof(line),
        "padbag [%s] pad %d focus %s cur %d,%d sel %u carry %d rot %d drop %d,%d can %d frames %d box %d modal %d items%s",
        what, (int)pad.IsActive(), pad.GetFocus() == Pad::Focus::Grid ? "grid" : "book", pad.Row(), pad.Col(),
        pad.BookSelection(), drag.IsActive() ? (int)drag.id : -1, drag.rotation, drag.dropRow, drag.dropCol,
        (int)drag.canDrop, (int)bp.frames.size(), m_GameUI.GetSpellbook().GetBodyCount(),
        (int)m_GameUI.IsModalOpen(), items);
    AutoTestLog(line);
}

// ============================================================
// 1 秒：湧き停止・片付け、魔法書に石弾（L 字 3 マス）を 3 個、バックパックを開く。
// 以後 kSteps の通りに入力し、各入力の直前に前の入力の結果（LogState）を記録する。
// 期待：掴む → 枠の外では A を押しても持ったまま → 回して縦に置く → Y で箱へ（選択が動く）→ A で取り出して
// グリッドへ → 置く → X で箱へ戻る → 枠を掴んで B で取り消し（枠は残る）→ B で閉じる（1 フレーム後）
// ============================================================
void AutoTestPadBag::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<SpellbookComponent>(m_Player))
            m_Registry.Get<SpellbookComponent>(m_Player).Learn(ItemID::StoneShot, 3);
        m_GameUI.TestShow(1);
        m_GameUI.TestPad().TestPress(0, 0, 0);   // パッド操作へ切り替える
        m_AutoStep = 1;
        return;
    }
    if (m_AutoStep != 1) return;

    const int count = (int)(sizeof(kSteps) / sizeof(kSteps[0]));
    if (m_Next >= count)
    {
        AutoTestLog("padbag done");
        m_AutoStep = 2;
        return;
    }

    const PadStep& s = kSteps[m_Next];
    if (m_AutoTime < s.t) return;

    if (s.look)
    {
        LogState(s.look);
        char line[64];
        snprintf(line, sizeof(line), "padbag look %s", s.look);
        AutoTestLog(line);
    }
    else
    {
        LogState(s.note);   // この入力の直前の状態 = 前の入力の結果
        // 閉じた後は GameUI がパッド操作を回さないので、入力を残さない
        if (m_GameUI.IsModalOpen()) m_GameUI.TestPad().TestPress(s.dx, s.dy, s.buttons);
    }
    ++m_Next;
}

REGISTER_BATTLE_AUTOTEST("padbag", AutoTestPadBag)
