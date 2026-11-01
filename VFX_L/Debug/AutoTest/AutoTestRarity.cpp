// ============================================================
// AutoTestRarity.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=rarity
// 稀有度（Item/ItemRarity.h、2026-10-28）：上級魔法をやめて札を ノーマル / レア / レジェンド に分けた。
//   A 表      : 全部の札の稀有度を 1 行ずつ `rarity table <稀有度>: 名前…`
//   B 出方    : 四択を 3000 回ずつ引き（LevelUpSystem::DebugFill）、レベル 1 / 15 / 30・宝箱で
//               `rarity odds lv <L> crate <0/1> card N/R/L % offerR % offerL % frames %`（1 枚あたり / 四択に 1 枚でも入る率）
//               （2026-10-29 から何面目かは率に入らないので pos は外した）
//   C 単独    : 以前の上級魔法 5 つを 1 つずつ前提なしで置き、正面 7〜10m の動かない的 6 体（HP 10 万）へ 4 秒撃つ。
//               `rarity solo <名前> triggered <0/1> lost <HP> damaged <体数>`、2.6 秒目に `rarity look <名前>`
//   D カード  : 四択に ノーマル / レア / レジェンド を並べて `rarity look cards`、宝箱の四択で `rarity look crate`
// VFXL_RARITY_ODDS=1（2026-10-29、ユーザー「稀有度の倍率 = 落ちる率から計算」）：B だけを細かく
//   （レベル 1 / 5 / 10 / 15 / 20 / 25 / 30 / 40 / 50、宝箱は 1 / 15 / 30）と商店（レベル 20 / 30 / 40 で 3000 回品揃えを作る：
//   `rarity shop lv <L> card N/R/L % frame N/R/L % rowR % rowL %`、row = 1 列に 1 枚でも入る率）を出して `rarity done`。C・D はしない
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Scene/RunResult.h"
#include "UI/ShopStock.h"

namespace
{
    class AutoTestRarity final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        // 四択の間は gameplay が止まるので Update から（自分で時計を進める）
        void UpdateFrame(float dt) override { m_AutoTime += dt; Run(); }
        bool SelfClock() const override { return true; }

    private:
        void Run();
        void LogTable();
        void LogOdds(int minutes, bool crate);
        void LogShopOdds(int pos);
        void StartSolo(ItemID id);
        bool ReadLost(float& lost, int& damaged);

        int   m_Solo = -1;        // 単独で撃っている魔法の番号（kSolo）
        float m_SoloAt = 0.0f;
        bool  m_SoloShot = false;
        float m_PhaseAt = 0.0f;
    };

    constexpr ItemID kSolo[] = { ItemID::Meteor, ItemID::Beam, ItemID::Poison, ItemID::IceSpike, ItemID::Twister };
    constexpr int kSoloCount = (int)(sizeof(kSolo) / sizeof(kSolo[0]));
    constexpr float kSoloTime = 4.6f;   // 0.6 秒待って（標的の読み戻し）4 秒撃つ
    constexpr float kTargetHp = 100000.0f;
}

void AutoTestRarity::LogTable()
{
    std::string names[Rarity::kCount];
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;
        std::string& s = names[(int)c->rarity];
        if (!s.empty()) s += ", ";
        s += c->name;
        if (!c->triggeredBy.empty()) s += "(!triggered)";   // 残っていたら間違い
    }
    const char* tier[] = { "normal", "rare", "legendary" };
    for (int r = 0; r < Rarity::kCount; ++r)
        AutoTestLog(("rarity table " + std::string(tier[r]) + ": " + names[r]).c_str());
}

void AutoTestRarity::LogOdds(int minutes, bool crate)
{
    if (!m_Registry.Has<LevelComponent>(m_Player) || !m_Registry.Has<SpellbookComponent>(m_Player)) return;
    LevelComponent tmp = m_Registry.Get<LevelComponent>(m_Player);
    const auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
    m_LevelUpSystem.runMinutes = (float)minutes;

    constexpr int kTries = 3000;
    int cards[Rarity::kCount] = {}, frames = 0, total = 0, offerRare = 0, offerLegend = 0;
    for (int i = 0; i < kTries; ++i)
    {
        if (m_LevelUpSystem.DebugFill(tmp, &book, crate) == 0) continue;
        bool r = false, l = false;
        for (ItemID id : tmp.pendingChoices)
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;
            ++cards[(int)c->rarity];
            ++total;
            if (c->category == ItemCategory::Frame) ++frames;
            r |= c->rarity == ItemRarity::Rare;
            l |= c->rarity == ItemRarity::Legendary;
        }
        offerRare += r ? 1 : 0;
        offerLegend += l ? 1 : 0;
    }
    const float t = (std::max)(total, 1) / 100.0f;
    char line[220];
    snprintf(line, sizeof(line), "rarity odds min %d premium %d card N %.1f%% R %.1f%% L %.1f%% offerR %.1f%% offerL %.1f%% frames %.1f%%",
        minutes, crate ? 1 : 0, cards[0] / t, cards[1] / t, cards[2] / t,
        100.0f * offerRare / kTries, 100.0f * offerLegend / kTries, frames / t);
    AutoTestLog(line);
}

// 商店の品揃え（2026-10-29）：今の引き継ぎを写してレベルだけ替え、3000 回作って [0..3] と [4]（設置枠）の稀有度を数える
void AutoTestRarity::LogShopOdds(int pos)
{
    RunCarry carry = g_RunCarry;
    carry.pos = pos;   // 次に始める面
    ShopStock shop;
    constexpr int kTries = 3000;
    int cards[Rarity::kCount] = {}, frameCards[Rarity::kCount] = {}, total = 0, frameTotal = 0, rowRare = 0, rowLegend = 0;
    for (int i = 0; i < kTries; ++i)
    {
        shop.Generate(carry);
        bool r = false, l = false;
        for (int s = 0; s < shop.Count(); ++s)
        {
            const ItemCommon* c = ItemDatabase::GetCommon(shop.At(s).id);
            if (!c) continue;
            const int tier = (int)c->rarity;
            if (c->category == ItemCategory::Frame) { ++frameCards[tier]; ++frameTotal; }
            else { ++cards[tier]; ++total; }
            r |= c->rarity == ItemRarity::Rare;
            l |= c->rarity == ItemRarity::Legendary;
        }
        rowRare += r ? 1 : 0;
        rowLegend += l ? 1 : 0;
    }
    const float t = (std::max)(total, 1) / 100.0f, ft = (std::max)(frameTotal, 1) / 100.0f;
    char line[240];
    snprintf(line, sizeof(line), "rarity shop pos %d card N %.1f%% R %.1f%% L %.1f%% frame N %.1f%% R %.1f%% L %.1f%% rowR %.1f%% rowL %.1f%%",
        pos, cards[0] / t, cards[1] / t, cards[2] / t, frameCards[0] / ft, frameCards[1] / ft, frameCards[2] / ft,
        100.0f * rowRare / kTries, 100.0f * rowLegend / kTries);
    AutoTestLog(line);
}

void AutoTestRarity::StartSolo(ItemID id)
{
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    ClearBackpackItems(bp);
    bool placed = false;
    for (int r = 2; r < BackpackComponent::GRID - 2 && !placed; ++r)
        for (int c = 2; c < BackpackComponent::GRID - 2 && !placed; ++c)
            placed = BackpackLogic::Place(bp, id, r, c, 0) >= 0;
    bp.dirty = true;
    m_Swarm.KillAll();
    m_Swarm.ClearProjectiles();
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float gy = m_Swarm.GetAIParams().groundY;
    const float kOff[6][2] = { { -1.5f, 7.0f }, { 1.5f, 7.0f }, { 0.0f, 8.5f }, { -2.0f, 10.0f }, { 2.0f, 10.0f }, { 0.0f, 10.0f } };
    for (const auto& o : kOff)
    {
        const float x = tf.position.x + o[0], z = tf.position.z + o[1];
        m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), kTargetHp, 0.0f);
    }
    m_SoloAt = m_AutoTime;
    m_SoloShot = false;
    char line[96];
    snprintf(line, sizeof(line), "rarity solo start %s placed %d", ItemDatabase::GetCommon(id)->name, placed ? 1 : 0);
    AutoTestLog(line);
}

bool AutoTestRarity::ReadLost(float& lost, int& damaged)
{
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    if (!m_Swarm.DebugReadEnemies(en, st)) return false;
    lost = 0.0f;
    damaged = 0;
    int alive = 0;
    for (size_t i = 0; i < en.size() && i < st.size(); ++i)
    {
        if (st[i] == Swarm::kStateDead) continue;
        const float hp = Swarm::HpFromFixed(en[i].hp);
        ++alive;
        lost += kTargetHp - hp;
        if (hp < kTargetHp - 0.5f) ++damaged;
    }
    lost += (6 - alive) * kTargetHp;   // 消えた的（起きないはず）
    return true;
}

void AutoTestRarity::Run()
{
    if (!m_Registry.Has<LevelComponent>(m_Player)) return;
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    if (lv.pendingChoices.empty()) lv.experience = 0.0f;   // 勝手に四択が出ないように
    char line[200];

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        FillAllFrames(bp);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(40.0f);
        cam.distance = 16.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();

        LogTable();
        if (GetEnvironmentVariableA("VFXL_RARITY_ODDS", nullptr, 0) > 0)
        {
            // 2026-11-01 から率は経過時間（分）で決まる
            const int minutes[] = { 0, 3, 5, 8, 10, 15, 20, 25, 30 };
            for (int m : minutes) LogOdds(m, false);
            for (int m : { 3, 8, 10, 20 }) LogOdds(m, true);   // 金色の箱（エリート 3:00 / 8:00、Boss 10 分頃、2 面目の終わり）
            for (int pos : { 2, 3 }) LogShopOdds(pos);
            AutoTestLog("rarity done");
            m_AutoStep = 7;
            return;
        }
        LogOdds(0, false);
        LogOdds(5, false);
        LogOdds(10, false);
        LogOdds(10, true);
        m_Solo = 0;
        StartSolo(kSolo[0]);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    // ---- C 単独 ----
    if (m_AutoStep == 1)
    {
        const ItemID id = kSolo[m_Solo];
        if (m_Registry.Has<ManaComponent>(m_Player)) m_Registry.Get<ManaComponent>(m_Player).current = 1.0e6f;
        if (!m_SoloShot && m_AutoTime - m_SoloAt >= 2.6f)
        {
            snprintf(line, sizeof(line), "rarity look %s", ItemDatabase::GetCommon(id)->name);
            AutoTestLog(line);
            m_SoloShot = true;
        }
        if (m_AutoTime - m_SoloAt >= kSoloTime)
        {
            // 前提なしで撃てているか：杖の表の triggered（上級の印）と、的が受けた HP
            int triggered = -1;
            if (m_Registry.Has<WandComponent>(m_Player))
            {
                const auto& wand = m_Registry.Get<WandComponent>(m_Player);
                for (const auto& s : wand.spells) if (s.id == id) triggered = s.triggered ? 1 : 0;
                for (const auto& a : wand.areas)  if (a.id == id) triggered = a.triggered ? 1 : 0;
            }
            float lost = 0.0f;
            int damaged = 0;
            ReadLost(lost, damaged);
            snprintf(line, sizeof(line), "rarity solo %s triggered %d lost %.0f damaged %d",
                ItemDatabase::GetCommon(id)->name, triggered, lost, damaged);
            AutoTestLog(line);
            if (++m_Solo < kSoloCount) StartSolo(kSolo[m_Solo]);
            else
            {
                m_Swarm.KillAll();
                ClearBackpackItems(m_Registry.Get<BackpackComponent>(m_Player));
                m_AutoStep = 2;
                m_PhaseAt = m_AutoTime;
            }
        }
        return;
    }

    // ---- D カード（レベルアップ → 宝箱）----
    if (m_AutoStep == 2 && m_AutoTime - m_PhaseAt >= 0.5f)
    {
        lv.ClearChoices();
        const ItemID cards[] = { ItemID::Fireball, ItemID::IceSpike, ItemID::Meteor, ItemID::FrameLifesteal };
        lv.pendingChoices.assign(std::begin(cards), std::end(cards));
        lv.choiceFromCrate = false;
        m_AutoStep = 3;
        m_PhaseAt = m_AutoTime;
    }
    else if (m_AutoStep == 3 && m_AutoTime - m_PhaseAt >= 1.0f)
    {
        AutoTestLog("rarity look cards");
        m_AutoStep = 4;
        m_PhaseAt = m_AutoTime;
    }
    else if (m_AutoStep == 4 && m_AutoTime - m_PhaseAt >= 0.4f)
    {
        lv.ClearChoices();
        const ItemID cards[] = { ItemID::ChainRune, ItemID::Beam, ItemID::Frame3x3, ItemID::CrystalBall };
        lv.pendingChoices.assign(std::begin(cards), std::end(cards));
        lv.choiceFromCrate = true;
        m_AutoStep = 5;
        m_PhaseAt = m_AutoTime;
    }
    else if (m_AutoStep == 5 && m_AutoTime - m_PhaseAt >= 1.0f)
    {
        AutoTestLog("rarity look crate");
        m_AutoStep = 6;
        m_PhaseAt = m_AutoTime;
    }
    else if (m_AutoStep == 6 && m_AutoTime - m_PhaseAt >= 0.4f)
    {
        lv.ClearChoices();
        AutoTestLog("rarity done");
        m_AutoStep = 7;
    }
}

REGISTER_BATTLE_AUTOTEST("rarity", AutoTestRarity)
