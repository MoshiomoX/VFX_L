// ============================================================
// AutoTestGold.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=gold
// VFXL_BATTLE_AUTOTEST=gold：金貨（2026-10-04）。経験値で増える・箱を買う（足りないと断る）・四択の引き直し
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestGold final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 金貨（VFXL_BATTLE_AUTOTEST=gold、2026-10-04）。実時間（三択の間も進む）、Update から呼ぶ
//   1 秒: 湧き停止・全消し・無敵・MP 無限、金貨 0。プレイヤーの周り 5〜7m に HP 1 の雑魚 12 体（追尾弾で倒れ、オーブを吸う）
//   6 秒: `gold earned` = 経験値 × 0.1 の金貨が増えたか
//   6.5 秒: 金貨 15、一番近い箱の 1.6m 手前へ（箱の方を向く）。7.5 秒 `gold look deny`（案内「金貨が足りない 15 / 20」）
//   8 秒: F → 断られる（箱は残る・金貨そのまま）。8.5 秒: 金貨 100、`gold look prompt`。9 秒: F → 開く（80、次は 26）
//   9.6 秒 `gold look offer`（引き直しのボタン 10）、10 秒 / 10.5 秒 引き直し（70 → 50、候補が入れ替わる）、
//   10.8 秒 `gold look reroll`（ボタン 30）。11.2 秒: 金貨 5 で引き直し → 断られる、11.5 秒 `gold look poor`。
//   12 秒: 先頭を選ぶ（引き直しの回数が 0 に戻る）、13 秒 `gold done`
// ============================================================
void AutoTestGold::Run(float dt)
{
    static float s_T = 0.0f, s_NextLog = 0.0f;
    static int s_Step = 0;
    static Entity s_Crate = EntityTraits::NULL_ENTITY;
    static size_t s_CratesBefore = 0;
    s_T += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<WalletComponent>(m_Player)) return;
    auto& wallet = m_Registry.Get<WalletComponent>(m_Player);
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto names = [](const std::vector<ItemID>& ids)
    {
        std::string s;
        for (ItemID id : ids)
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!s.empty()) s += ", ";
            s += c ? c->name : "?";
        }
        return s;
    };
    char line[320];
    if (s_Step >= 2 && s_Step < 99) lv.experience = 0.0f;   // 箱の段ではレベルアップさせない

    if (s_Step == 0 && s_T >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        wallet = WalletComponent{};
        lv.experience = 0.0f;
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 12; ++i)
        {
            const float a = 6.2831853f * (float)i / 12.0f;
            const float r = 5.0f + 2.0f * (float)(i % 2);
            const Vector3 p = pp + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
            m_Swarm.SpawnEnemy(Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + gy, p.z), 1.0f, 1.5f);
        }
        AutoTestLog("gold start: 12 mobs (HP 1) around the player, wallet 0");
        s_NextLog = s_T;
        s_Step = 1;
    }
    if (s_Step == 1 && s_T >= s_NextLog)
    {
        s_NextLog = s_T + 1.0f;
        snprintf(line, sizeof(line), "gold t %.1f coins %d earned %.2f kills %u level %d exp %.0f",
            s_T, wallet.Coins(), wallet.earned, m_Swarm.GetCounters().killCount, lv.level, lv.experience);
        AutoTestLog(line);
    }
    if (s_Step == 1 && s_T >= 6.0f)
    {
        snprintf(line, sizeof(line), "gold earned %.2f (goldPerExp %.2f, kills %u)", wallet.earned, wallet.goldPerExp,
            m_Swarm.GetCounters().killCount);
        AutoTestLog(line);
        if (lv.IsChoosing()) LevelUpSystem::Choose(m_Registry, m_Player, lv.pendingChoices.front());   // レベルアップの四択は閉じる
        m_Swarm.KillAll();   // 以後はレベルアップしない（箱の三択とレベルアップが重なると箱は開かない＝払わない、の仕様に当たる）
        // 一番近い箱の手前へ
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        float best = 1.0e9f;
        for (Entity e : m_Crates.GetCrates())
        {
            const Vector3 cp = m_Registry.Get<InteractableComponent>(e).basePos;
            const float d = (cp - pp).LengthSquared();
            if (d < best) { best = d; s_Crate = e; }
        }
        if (!m_Registry.IsValid(s_Crate)) { AutoTestLog("gold no crate"); s_Step = 99; return; }
        s_Step = 2;
    }
    if (s_Step == 2 && s_T >= 6.5f)
    {
        const Vector3 cp = m_Registry.Get<InteractableComponent>(s_Crate).basePos;
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        Vector3 dir = tf.position - cp; dir.y = 0.0f;
        if (dir.LengthSquared() < 1e-4f) dir = Vector3(1, 0, 0);
        dir.Normalize();
        tf.position = Vector3(cp.x + dir.x * 1.6f, cp.y + 1.0f, cp.z + dir.z * 1.6f);
        if (m_Registry.Has<RigidbodyComponent>(m_Player))
            m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(dir.x, -dir.z)));   // 箱の方（forward = -dir）
        cam.SnapToTarget();
        wallet.gold = 15.0f;
        s_CratesBefore = m_Crates.GetCrates().size();
        snprintf(line, sizeof(line), "gold at crate: crates %zu price %d coins %d", s_CratesBefore, m_Crates.Price(), wallet.Coins());
        AutoTestLog(line);
        s_Step = 3;
    }
    if (s_Step == 3 && s_T >= 7.5f) { AutoTestLog("gold look deny"); s_Step = 4; }
    if (s_Step == 4 && s_T >= 8.0f) { m_AutoInteract = true; s_Step = 5; }
    if (s_Step == 5 && s_T >= 8.3f)
    {
        snprintf(line, sizeof(line), "gold deny: crates %zu (before %zu) coins %d choosing %d",
            m_Crates.GetCrates().size(), s_CratesBefore, wallet.Coins(), (int)lv.IsChoosing());
        AutoTestLog(line);
        wallet.gold = 100.0f;
        s_Step = 6;
    }
    if (s_Step == 6 && s_T >= 8.6f) { AutoTestLog("gold look prompt"); s_Step = 7; }
    if (s_Step == 7 && s_T >= 9.0f) { m_AutoInteract = true; s_Step = 8; }
    if (s_Step == 8 && s_T >= 9.3f)
    {
        snprintf(line, sizeof(line), "gold open: crates %zu coins %d next price %d choosing %d choices [%s] reroll cost %d",
            m_Crates.GetCrates().size(), wallet.Coins(), m_Crates.Price(), (int)lv.IsChoosing(),
            names(lv.pendingChoices).c_str(), m_LevelUpSystem.RerollCost(lv));
        AutoTestLog(line);
        s_Step = 9;
    }
    if (s_Step == 9 && s_T >= 9.6f) { AutoTestLog("gold look offer"); s_Step = 10; }
    if ((s_Step == 10 && s_T >= 10.0f) || (s_Step == 11 && s_T >= 10.5f))
    {
        const bool ok = m_LevelUpSystem.Reroll(m_Registry, m_Player);
        snprintf(line, sizeof(line), "gold reroll %s: coins %d count %d next cost %d choices [%s]",
            ok ? "ok" : "FAILED", wallet.Coins(), lv.rerollCount, m_LevelUpSystem.RerollCost(lv), names(lv.pendingChoices).c_str());
        AutoTestLog(line);
        ++s_Step;
    }
    if (s_Step == 12 && s_T >= 10.8f) { AutoTestLog("gold look reroll"); s_Step = 13; }
    if (s_Step == 13 && s_T >= 11.2f)
    {
        wallet.gold = 5.0f;
        const bool ok = m_LevelUpSystem.Reroll(m_Registry, m_Player);
        snprintf(line, sizeof(line), "gold reroll poor %s: coins %d count %d", ok ? "WRONGLY OK" : "denied", wallet.Coins(), lv.rerollCount);
        AutoTestLog(line);
        s_Step = 14;
    }
    if (s_Step == 14 && s_T >= 11.5f) { AutoTestLog("gold look poor"); s_Step = 15; }
    if (s_Step == 15 && s_T >= 12.0f)
    {
        if (lv.IsChoosing()) LevelUpSystem::Choose(m_Registry, m_Player, lv.pendingChoices.front());
        snprintf(line, sizeof(line), "gold chose: count %d next reroll cost %d earned %.1f spent %.1f",
            lv.rerollCount, m_LevelUpSystem.RerollCost(lv), wallet.earned, wallet.spent);
        AutoTestLog(line);
        s_Step = 16;
    }
    if (s_Step == 16 && s_T >= 13.0f) { AutoTestLog("gold done"); s_Step = 99; }
}

REGISTER_BATTLE_AUTOTEST("gold", AutoTestGold)
