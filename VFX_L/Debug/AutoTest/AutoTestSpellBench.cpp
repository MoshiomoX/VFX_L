// ============================================================
// AutoTestSpellBench.cpp
// TEMP-TEST: VFXL_SPELL_LAB=1 + VFXL_BATTLE_AUTOTEST=spellbench
// 魔法の試算表（2026-10-07 夜、ユーザー「単体攻撃と範囲攻撃の釣り合いが分からない」→ 試算表を作る）。
// 魔法を 1 つずつ（上級魔法は前提の基本魔法と一緒に）バックパックへ置き、場面毎にダメージを測る:
//   ・single：正面 10m に動かない的 1 体（HP 100 万）。8 秒で減った HP / 8 = 単体 DPS
//   ・crowd ：正面 10m を中心に半径 3m の円に動かない的 30 体（各 HP 10 万）。8 秒で全員から減った HP の合計 / 8 = 群れへの DPS
//   どちらも的は死なない（オーバーキル・群れが減る影響を除く）。各場面の前に敵・弾・範囲を全部消し、2 秒撃たせてから測る。
//   ・fodder：同じ円に死ぬ雑魚 30 体（各 HP 25）。全滅までの秒（20 秒で打ち切り、残りの数も記録）
//   crowd / single = 1 回の詠唱で平均何体に当たっているか（範囲の広さの目安）
// VFXL_BENCH_RUNES=1（同日、ユーザー「二重・分裂などのルーンが良い魔法の長所を無限に伸ばし、単体魔法の短所も伸ばす」）:
//   基本魔法 6 種 × ルーン {無し, 分裂, 二重, 加速, 拡大鏡, 4 つ全部} を single / crowd で測る（fodder 無し）。
//   ルーンは影響マスが魔法に届く空きマスを総当たりで探して置く
// 行：`spellbench <名前> <single|crowd> dps <値>`、`spellbench <名前> fodder clear <秒> left <残り>`、魔法毎に `spellbench info …`（集約後の数値）、
//   最後に `spellbench table <名前> single crowd ratio clear left mpps cells` を並べて `spellbench done`
// MP は実験場なので無限（MP の頭打ちは無い。mpps は「全部撃ち続けた時の消費」。誘発される上級魔法の分は入らない）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestSpellBench final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    struct Piece { ItemID id; int r, c; };
    struct Trial
    {
        std::string name;
        std::vector<Piece> pieces;     // 決まった所に置く物（先頭が測る魔法）
        std::vector<ItemID> runes;     // 先頭の魔法に影響が届く所を探して置くルーン
    };
    constexpr float kWarm = 2.0f;
    constexpr float kMeasure = 8.0f;
    constexpr float kSingleHp = 1.0e6f;
    constexpr float kCrowdHp = 1.0e5f;
    constexpr int kCrowd = 30;
    // 場面 fodder：死ぬ雑魚 30 体（中盤の雑魚くらいの HP）。撃ち始めから全滅までの秒
    // （オーバーキルと、群れが減ると範囲が当たる数も減る分が入る = 実戦に近い）。kFodderCap 秒で打ち切り
    constexpr float kFodderHp = 25.0f;
    constexpr float kFodderCap = 20.0f;

    struct Result { float single = 0.0f, crowd = 0.0f, clear = 0.0f, mpps = 0.0f; int cells = 0, left = 0; };

    std::vector<Trial> BuildTrials(bool runes)
    {
        std::vector<Trial> t;
        struct Basic { const char* name; ItemID id; int r, c; };
        const Basic basics[] = {
            { "homing", ItemID::HomingBolt, 4, 4 }, { "arc", ItemID::ArcBolt, 4, 4 }, { "fireball", ItemID::Fireball, 4, 4 },
            { "stone", ItemID::StoneShot, 4, 4 }, { "arrow", ItemID::GoldenArrow, 3, 4 }, { "poison", ItemID::Poison, 4, 4 },
        };
        if (!runes)
        {
            for (const Basic& b : basics) t.push_back({ b.name, { { b.id, b.r, b.c } }, {} });
            // 置き場所は shapes 自動テストと同じ（上級魔法は前提の影響マスが届く並び）
            t.push_back({ "meteor+fireball+stone", { { ItemID::Meteor, 2, 2 }, { ItemID::Fireball, 0, 3 }, { ItemID::StoneShot, 3, 1 } }, {} });
            t.push_back({ "beam+homing+arc", { { ItemID::Beam, 6, 5 }, { ItemID::HomingBolt, 5, 4 }, { ItemID::ArcBolt, 7, 6 } }, {} });
            return t;
        }
        struct Cfg { const char* name; std::vector<ItemID> runes; };
        const Cfg cfgs[] = {
            { "", {} },
            { "+split", { ItemID::SplitRune } },
            { "+double", { ItemID::DoubleCastRune } },
            { "+haste", { ItemID::HasteRune } },
            { "+magnifier", { ItemID::Magnifier } },
            { "+all4", { ItemID::SplitRune, ItemID::DoubleCastRune, ItemID::HasteRune, ItemID::Magnifier } },
        };
        for (const Basic& b : basics)
            for (const Cfg& c : cfgs)
                t.push_back({ std::string(b.name) + c.name, { { b.id, b.r, b.c } }, c.runes });
        return t;
    }
}

// ============================================================
// 1 回の試し = (魔法, 場面)。手順：消す → 置く（最初の場面の時だけ）→ 的を湧かせる → kWarm 秒 → HP の合計を読む → kMeasure 秒 → もう一度読む
// ============================================================
void AutoTestSpellBench::Run()
{
    static std::vector<Trial> s_Trials;
    static std::vector<Result> s_Results;
    static int s_Trial = 0, s_Scene = 0, s_Phase = 0, s_Scenes = 3;
    static float s_PhaseT = 0.0f, s_NextPoll = 0.0f;
    static double s_Hp0 = 0.0;
    char line[320];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<WandComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& wand = m_Registry.Get<WandComponent>(m_Player);

    // 生きている的の HP の合計（GPU を待って読む。自動テスト専用）
    auto sumHp = [&]() -> double
    {
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (!m_Swarm.DebugReadEnemies(en, st)) return -1.0;
        double sum = 0.0;
        for (size_t i = 0; i < en.size(); ++i)
            if (st[i] != Swarm::kStateDead) sum += Swarm::HpFromFixed(en[i].hp);
        return sum;
    };

    if (m_AutoStep == 0)
    {
        if (m_AutoTime < 1.0f) return;
        const bool runes = GetEnvironmentVariableA("VFXL_BENCH_RUNES", nullptr, 0) > 0;
        s_Trials = BuildTrials(runes);
        s_Results.assign(s_Trials.size(), Result{});
        s_Scenes = runes ? 2 : 3;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        // 9×9 全面に枠
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        for (int r = 0; r < BackpackComponent::GRID; ++r)
            for (int c = 0; c < BackpackComponent::GRID; ++c)
                BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(35.0f);
        cam.distance = 14.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "spellbench start trials %d runes %d", (int)s_Trials.size(), runes ? 1 : 0);
        AutoTestLog(line);
        s_Trial = 0; s_Scene = 0; s_Phase = 0;
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;

    const Trial& tr = s_Trials[s_Trial];
    const char* name = tr.name.c_str();

    // 次の試しへ。全部終わったら表を出す
    auto nextScene = [&]()
    {
        s_Phase = 0;
        if (++s_Scene < s_Scenes) return;
        s_Scene = 0;
        if (++s_Trial < (int)s_Trials.size()) return;
        for (size_t i = 0; i < s_Trials.size(); ++i)
        {
            const Result& r = s_Results[i];
            snprintf(line, sizeof(line), "spellbench table %s single %.1f crowd %.1f ratio %.2f clear %.2f left %d mpps %.1f cells %d",
                s_Trials[i].name.c_str(), r.single, r.crowd, r.single > 0.0f ? r.crowd / r.single : 0.0f, r.clear, r.left, r.mpps, r.cells);
            AutoTestLog(line);
        }
        m_Swarm.KillAll();
        wand.castingPaused = true;
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("spellbench done");
        m_AutoStep = 2;
    };

    if (s_Phase == 0)
    {
        // ---- 消す・置く・的 ----
        m_Swarm.KillAll();
        m_Swarm.ClearProjectiles();
        m_Swarm.ClearAreas();
        if (s_Scene == 0)
        {
            ClearBackpackItems(bp);
            int spellIdx = -1;
            for (const Piece& p : tr.pieces)
            {
                const int idx = BackpackLogic::CanPlace(bp, p.id, p.r, p.c, 0) ? BackpackLogic::Place(bp, p.id, p.r, p.c, 0) : -1;
                if (spellIdx < 0) spellIdx = idx;
                if (idx < 0)
                {
                    snprintf(line, sizeof(line), "spellbench %s place failed %s", name, ItemDatabase::GetCommon(p.id)->name);
                    AutoTestLog(line);
                }
            }
            // ルーン：影響が先頭の魔法に届く空きマスを総当たり
            for (ItemID rune : tr.runes)
            {
                bool ok = false;
                for (int r = 0; r < BackpackComponent::GRID && !ok && spellIdx >= 0; ++r)
                    for (int c = 0; c < BackpackComponent::GRID && !ok; ++c)
                    {
                        if (!BackpackLogic::CanPlace(bp, rune, r, c, 0)) continue;
                        const int ri = BackpackLogic::Place(bp, rune, r, c, 0);
                        if (ri < 0) continue;
                        const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, spellIdx);
                        if (std::find(inf.begin(), inf.end(), ri) != inf.end()) ok = true;
                        else BackpackLogic::Remove(bp, ri);
                    }
                if (!ok)
                {
                    snprintf(line, sizeof(line), "spellbench %s rune place failed %s", name, ItemDatabase::GetCommon(rune)->name);
                    AutoTestLog(line);
                }
            }
            bp.dirty = true;
        }
        wand.castingPaused = false;
        const float gy = m_Swarm.GetAIParams().groundY;
        const Vector3 c = tf.position + Vector3(0.0f, 0.0f, 10.0f);
        if (s_Scene == 0)
            m_Swarm.SpawnEnemy(Vector3(c.x, m_Grid.SampleHeight(c.x, c.z) + gy, c.z), kSingleHp, 0.0f);
        else
        {
            // 半径 3m の円に一様（黄金角の渦巻き）
            for (int i = 0; i < kCrowd; ++i)
            {
                const float rr = 3.0f * std::sqrt(((float)i + 0.5f) / (float)kCrowd);
                const float a = (float)i * 2.39996323f;
                const float x = c.x + std::cos(a) * rr, z = c.z + std::sin(a) * rr;
                m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), (s_Scene == 2) ? kFodderHp : kCrowdHp, 0.0f);
            }
        }
        s_PhaseT = m_AutoTime;
        s_NextPoll = 0.0f;
        s_Phase = (s_Scene == 2) ? 3 : 1;
        return;
    }

    // ---- fodder：0.25 秒毎に生き残りを数え、全滅（湧いて 0.5 秒以降）か打ち切りで終わり ----
    if (s_Phase == 3)
    {
        if (m_AutoTime < s_NextPoll) return;
        s_NextPoll = m_AutoTime + 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (!m_Swarm.DebugReadEnemies(en, st)) return;
        int alive = 0;
        for (size_t i = 0; i < st.size(); ++i) if (st[i] != Swarm::kStateDead) ++alive;
        const float t = m_AutoTime - s_PhaseT;
        if ((alive == 0 && t > 0.5f) || t >= kFodderCap)
        {
            s_Results[s_Trial].clear = t;
            s_Results[s_Trial].left = alive;
            snprintf(line, sizeof(line), "spellbench %s fodder clear %.2f s left %d", name, t, alive);
            AutoTestLog(line);
            nextScene();
        }
        return;
    }

    if (s_Phase == 1 && m_AutoTime >= s_PhaseT + kWarm)
    {
        s_Hp0 = sumHp();
        s_PhaseT = m_AutoTime;
        s_Phase = 2;
        // 魔法毎に 1 回：集約後の数値（間隔・威力・弾数・消費）と占有マス
        if (s_Scene == 0)
        {
            float mpps = 0.0f;
            int cells = 0;
            for (const SpellStats& s : wand.spells)
            {
                const float rate = (s.castInterval > 0.0f) ? (float)s.castCount / s.castInterval : 0.0f;
                if (!s.triggered) mpps += s.manaCost * rate;
                snprintf(line, sizeof(line), "spellbench info %s spell %s interval %.3f damage %.1f areaMul %.2f radius %.2f count %d casts %d mana %.2f triggered %d",
                    name, ItemDatabase::GetCommon(s.id)->name, s.castInterval, s.damage, s.areaDamageMul, s.radius, s.projectileCount, s.castCount,
                    s.manaCost, s.triggered ? 1 : 0);
                AutoTestLog(line);
            }
            for (const AreaStats& a : wand.areas)
            {
                snprintf(line, sizeof(line), "spellbench info %s area %s interval %.3f tick %.1f / %.2f s duration %.2f mana %.2f triggered %d",
                    name, ItemDatabase::GetCommon(a.id)->name, a.castInterval, a.damagePerTick, a.tickInterval, a.duration,
                    a.manaCost, a.triggered ? 1 : 0);
                AutoTestLog(line);
            }
            for (const Piece& p : tr.pieces)
                if (const ItemCommon* ic = ItemDatabase::GetCommon(p.id)) cells += (int)ic->occupyCells.size();
            s_Results[s_Trial].mpps = mpps;
            s_Results[s_Trial].cells = cells + (int)tr.runes.size();
        }
        return;
    }
    if (s_Phase == 2 && m_AutoTime >= s_PhaseT + kMeasure)
    {
        const double hp1 = sumHp();
        const float dps = (s_Hp0 >= 0.0 && hp1 >= 0.0) ? (float)((s_Hp0 - hp1) / (m_AutoTime - s_PhaseT)) : -1.0f;
        (s_Scene == 0 ? s_Results[s_Trial].single : s_Results[s_Trial].crowd) = dps;
        snprintf(line, sizeof(line), "spellbench %s %s dps %.1f", name, s_Scene == 0 ? "single" : "crowd", dps);
        AutoTestLog(line);
        if (tr.name == "poison" && s_Scene == 1) AutoTestLog("spellbench look poison");   // 毒沼の群れ
        if (tr.name == "poison+all4" && s_Scene == 1) AutoTestLog("spellbench look poisonall4");
        nextScene();
    }
}

REGISTER_BATTLE_AUTOTEST("spellbench", AutoTestSpellBench)
