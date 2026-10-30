// ============================================================
// AutoTestPadBag.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=padbag
// バックパック画面のパッド操作（UI/BackpackPadControl）を、パッドの代わりの入力（TestPress）で一通り動かす。
// 2026-10-28 からスティックのカーソル（X = 加速、B = 素早く外す、閉じるのは Start、取り消しは無い）。
// 後半は四択のカードを左スティックで選ぶ（LevelUpUI::TestStick）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "UI/BackpackPadControl.h"
#include "UI/LevelUpUI.h"

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
        void RunLevelUp();
        void LogState(const char* what);
        int   m_Next = 0;          // 次に実行する手順
        float m_HoldUntil = -1.0f; // スティックを倒し続ける手順の終わり
        int   m_Phase = 0;         // 四択の段
        float m_PhaseAt = 0.0f;
        std::vector<ItemID> m_RerollBefore;   // 引き直しのボタンを押す前のカード
        int   m_Fired = 0;         // この段の入力を何回入れたか
    };

    using Pad = BackpackPadControl;

    // 時刻（秒）・十字・ボタン・スティック（hold 秒の間、毎フレーム）・スクリーンショットの名前（look が有る行は入力しない）
    struct PadStep { float t; int dx, dy; unsigned buttons; float sx, sy, hold; const char* look; const char* note; };
    const PadStep kSteps[] = {
        { 2.0f,  0,  0, 0,        0, 0, 0,     "cursor",   nullptr },                 // カーソルは (4,4) の火球の上。説明カードが出ている
        { 2.2f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "grab fireball" },
        { 2.4f,  0, -1, 0,        0, 0, 0,     nullptr,    "dpad up -> 3,4 (fits)" },
        { 2.9f,  0,  0, 0,        0, 0, 0,     "carry",    nullptr },
        { 3.2f,  0, -1, 0,        0, 0, 0,     nullptr,    "dpad up -> 2,4 (no frame)" },
        { 3.4f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "A on red: must keep carrying" },
        { 3.8f,  0,  0, 0,        0, 0, 0,     "red",      nullptr },
        { 4.1f,  0,  0, Pad::kRB, 0, 0, 0,     nullptr,    "rotate" },
        { 4.3f,  0,  1, 0,        0, 0, 0,     nullptr,    "dpad down -> 3,4" },
        { 4.5f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "place rotated at 3,4" },
        { 4.7f,  0,  0, 0,        0.5f, 0, 0.3f, nullptr,  "stick right half 0.3s" },
        { 5.1f,  0,  0, Pad::kX,  0.5f, 0, 0.3f, nullptr,  "stick right half 0.3s + X (faster)" },
        { 5.5f,  0,  0, Pad::kX,  -0.5f, 0, 0.3f, nullptr, "stick left half 0.3s + X (back)" },
        { 5.9f,  0,  0, 0,        0, 0, 0,     nullptr,    "moved (X faster?)" },
        { 6.0f,  0,  0, Pad::kY,  0, 0, 0,     nullptr,    "Y: jump to the book" },
        { 6.5f,  0,  0, 0,        0, 0, 0,     "book",     nullptr },                 // 箱の物の上（浮かせて明るい縁）+ 説明カード
        { 6.7f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "A: grab from the book" },
        { 6.9f,  0,  0, Pad::kY,  0, 0, 0,     nullptr,    "Y while carrying: back to the grid" },
        { 7.4f,  0,  0, 0,        0, 0, 0,     "frombook", nullptr },
        { 7.6f,  0,  0, Pad::kB,  0, 0, 0,     nullptr,    "B while carrying: back to the book" },
        { 7.8f,  0,  0, 0,        0, 0, 0,     nullptr,    "returned (box +1)?" },
        { 8.0f, -1,  0, 0,        0, 0, 0,     nullptr,    "dpad left (snaps to the middle of 3,4)" },
        { 8.2f,  0,  0, 0,        0, 0, 0,     nullptr,    "on the rotated fireball?" },
        { 8.4f,  0,  0, Pad::kB,  0, 0, 0,     nullptr,    "B: quick remove the fireball" },
        { 8.6f,  0,  0, 0,        0, 0, 0,     nullptr,    "removed (items empty, box +1)?" },
        { 8.8f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "A: grab the frame here" },
        { 9.0f,  0,  0, Pad::kY,  0, 0, 0,     nullptr,    "Y: carry it to the book" },
        { 9.3f,  0,  0, Pad::kA,  0, 0, 0,     nullptr,    "A outside the grid: back to the book here" },
        { 9.6f,  0,  0, 0,        0, 0, 0,     "dropped",  nullptr },
        { 9.8f,  0,  0, Pad::kX,  0, 0, 0,     nullptr,    "X alone: must not close" },
        { 10.0f, 0,  0, Pad::kB,  0, 0, 0,     nullptr,    "B over the chest: nothing happens" },
        { 10.2f, 0,  0, 0,        0, 0, 0,     nullptr,    "still open (modal 1)?" },
    };
}

// 状態を 1 行：カーソル・下のマス・箱の選択・掴んでいる物・置いてある魔法の一覧
void AutoTestPadBag::LogState(const char* what)
{
    auto& pad = m_GameUI.TestPad();
    const auto& drag = m_GameUI.GetDrag();
    const auto& bp = m_Registry.Get<BackpackComponent>(m_Player);

    char items[200] = "";
    for (const auto& it : bp.items)
    {
        char one[32];
        snprintf(one, sizeof(one), " %d@%d,%d/%d", (int)it.id, it.row, it.col, it.rotation);
        strncat_s(items, one, _TRUNCATE);
    }

    char line[420];
    snprintf(line, sizeof(line),
        "padbag [%s] pad %d cur %.0f,%.0f cell %d,%d sel %u carry %d rot %d drop %d,%d can %d frames %d box %d modal %d items%s",
        what, (int)pad.IsActive(), pad.Cursor().x, pad.Cursor().y, pad.HoverRow(), pad.HoverCol(),
        pad.BookSelection(), drag.IsActive() ? (int)drag.id : -1, drag.rotation, drag.dropRow, drag.dropCol,
        (int)drag.canDrop, (int)bp.frames.size(), m_GameUI.GetSpellbook().GetBodyCount(),
        (int)m_GameUI.IsModalOpen(), items);
    AutoTestLog(line);
}

// ============================================================
// 1 秒：湧き停止・片付け、魔法書に石弾（L 字 3 マス）を 3 個、バックパックを開く。
// 以後 kSteps の通りに入力し、各入力の直前に前の入力の結果（LogState）を記録する。
// 期待：掴む → 枠の外では A を押しても持ったまま → 回して置く → スティック（X で速い）→ Y で箱へ飛ぶ →
// A で取り出す → Y でグリッドへ → B で箱へ戻る → 十字でマスに揃えて B（火球を素早く外す）→ 枠を掴んで Y で箱へ →
// A で箱へ戻る → X / 箱の上の B では閉じない・何も起きない。その後バックパックを閉じて四択（RunLevelUp）
// ============================================================
void AutoTestPadBag::Run(float dt)
{
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<BackpackComponent>(m_Player)) return;
    if (m_AutoStep == 0 && m_Registry.Has<LevelComponent>(m_Player))
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
    if (m_AutoStep == 2) { RunLevelUp(); return; }
    if (m_AutoStep != 1) return;

    // スティックを倒し続ける手順の途中
    if (m_HoldUntil >= 0.0f)
    {
        const PadStep& s = kSteps[m_Next - 1];
        if (m_AutoTime < m_HoldUntil)
        {
            if (m_GameUI.IsModalOpen()) m_GameUI.TestPad().TestPress(0, 0, s.buttons & Pad::kX, { s.sx, s.sy });   // X は押し続け（加速）
            return;
        }
        m_HoldUntil = -1.0f;
    }

    const int count = (int)(sizeof(kSteps) / sizeof(kSteps[0]));
    if (m_Next >= count)
    {
        AutoTestLog("padbag backpack done");
        m_GameUI.TestShow(0);   // 閉じる（パッドなら Start。TestPress では押せない）
        m_AutoStep = 2;
        m_Phase = 0;
        m_PhaseAt = m_AutoTime;
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
        if (m_GameUI.IsModalOpen())
        {
            m_GameUI.TestPad().TestPress(s.dx, s.dy, s.buttons, { s.sx, s.sy });
            if (s.hold > 0.0f) m_HoldUntil = m_AutoTime + s.hold;
        }
    }
    ++m_Next;
}

// ============================================================
// 四択のカードを左スティックで選ぶ
//   0: 経験を足し、四択が出る前からスティックを右へ倒し続ける（走っている体）→ 出てしばらくしても 0 枚目のまま
//   1: 離す → 2: 右へ一度倒す（1 枚目へ）→ 3: 右へ倒し続ける（すぐ 1 枚 + 0.4 秒後から 0.18 秒ごと）→ 4: 左へ一度
//   5: 下へ一度（引き直しのボタンへ）→ 6: A（引き直す）→ 7: 上へ一度（カードへ）→ 8: 下 → 右（カードへ戻るだけ）
// ============================================================
void AutoTestPadBag::RunLevelUp()
{
    if (!m_Registry.Has<LevelComponent>(m_Player)) return;
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    auto& ui = m_GameUI.TestLevelUp();
    const float t = m_AutoTime - m_PhaseAt;
    auto log = [&](const char* what)
    {
        char line[160];
        snprintf(line, sizeof(line), "padbag levelup [%s] choosing %d modal %d cards %d cursor %d",
            what, (int)lv.IsChoosing(), (int)m_GameUI.IsModalOpen(), (int)lv.pendingChoices.size(), ui.GetCursor());
        AutoTestLog(line);
    };
    auto next = [&](int phase) { m_Phase = phase; m_PhaseAt = m_AutoTime; m_Fired = 0; };

    switch (m_Phase)
    {
    case 0:   // 経験を足して、出るまで + 出てから 0.8 秒、右へ倒し続ける
        if (t >= 0.2f && lv.experience < lv.ExpToNext() && !lv.IsChoosing() && !m_GameUI.IsModalOpen())
            lv.experience += lv.ExpToNext() + 1.0f;
        ui.TestStick({ 1.0f, 0.0f });
        if (m_GameUI.IsModalOpen() && lv.IsChoosing()) { log("shown while the stick is held"); next(10); }
        else if (t > 6.0f) { log("did not show"); m_AutoStep = 99; }
        break;
    case 10:
        ui.TestStick({ 1.0f, 0.0f });
        if (t >= 0.8f) { log("held since before: expect cursor 0"); AutoTestLog("padbag look levelup"); next(1); }
        break;
    case 1:   // 離す
        if (t >= 0.2f) next(2);
        break;
    case 2:   // 右へ一度（0.08 秒）
        if (t < 0.08f) ui.TestStick({ 1.0f, 0.0f });
        else if (t >= 0.25f) { log("tap right: expect cursor 1"); next(3); }
        break;
    case 3:   // 右へ倒し続ける 0.7 秒：すぐ 2、0.4 秒で 3、0.58 秒で 0（4 枚なら回って戻る）
        if (t < 0.7f) ui.TestStick({ 1.0f, 0.0f });
        else if (t >= 0.8f) { log("hold right 0.7s: expect cursor 0 (4 cards: 2 -> 3 -> 0)"); next(4); }
        break;
    case 4:   // 左へ一度
        if (t < 0.08f) ui.TestStick({ -1.0f, 0.0f });
        else if (t >= 0.25f)
        {
            log("tap left: expect cursor 3");
            AutoTestLog("padbag look levelupstick");
            // 引き直しのボタン（2026-10-28）：金貨を渡し、今のカードを覚える
            if (m_Registry.Has<WalletComponent>(m_Player)) m_Registry.Get<WalletComponent>(m_Player).gold = 100.0f;
            m_RerollBefore = lv.pendingChoices;
            next(5);
        }
        break;
    // 5〜8 の入力は段に入った最初のフレームに 1 回だけ（look の写真のフレームは長く、次の段の「最初の 0.08 秒」を飛び越すことがある）
    case 5:   // 下へ一度 → ボタン
        if (m_Fired == 0) { ui.TestStick({ 0.0f, -1.0f }); m_Fired = 1; }
        else if (t >= 0.25f)
        {
            log("tap down: expect cursor 4 (= the reroll button)");
            AutoTestLog("padbag look rerollfocus");
            next(6);
        }
        break;
    case 6:   // A → 引き直す（カーソルはボタンに残る）
        if (m_Fired == 0) { ui.TestConfirm(); m_Fired = 1; }
        else if (t >= 0.25f)
        {
            int changed = 0;
            for (size_t i = 0; i < lv.pendingChoices.size(); ++i)
                if (i >= m_RerollBefore.size() || lv.pendingChoices[i] != m_RerollBefore[i]) ++changed;
            const int gold = m_Registry.Has<WalletComponent>(m_Player) ? m_Registry.Get<WalletComponent>(m_Player).Coins() : -1;
            char line[160];
            snprintf(line, sizeof(line), "padbag reroll button: gold 100 -> %d rerolls %d cardsChanged %d (expect 90 1 >0)",
                gold, lv.rerollCount, changed);
            AutoTestLog(line);
            log("A on the button: expect cursor 4, still choosing");
            AutoTestLog("padbag look rerolled");
            next(7);
        }
        break;
    case 7:   // 上へ一度 → ボタンへ下りる前のカード
        if (m_Fired == 0) { ui.TestStick({ 0.0f, 1.0f }); m_Fired = 1; }
        else if (t >= 0.25f) { log("tap up: expect cursor 3"); next(8); }
        break;
    case 8:   // もう一度下 → 右：ボタンの上の左右はカードへ戻るだけ
        if (m_Fired == 0) { ui.TestStick({ 0.0f, -1.0f }); m_Fired = 1; }
        else if (m_Fired == 1 && t >= 0.15f) { log("down again: expect cursor 4"); ui.TestStick({ 1.0f, 0.0f }); m_Fired = 2; }
        else if (m_Fired == 2 && t >= 0.4f) { log("then right: expect cursor 3"); next(9); }
        break;
    case 9:
        if (t >= 0.3f) { AutoTestLog("padbag done"); m_AutoStep = 99; }
        break;
    default:
        break;
    }
}

REGISTER_BATTLE_AUTOTEST("padbag", AutoTestPadBag)
