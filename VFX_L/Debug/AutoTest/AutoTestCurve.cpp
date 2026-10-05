// ============================================================
// AutoTestCurve.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=curve
// VFXL_BATTLE_AUTOTEST=curve：難度曲線の実測（2026-10-04）。自動で 8 分遊び、30 秒毎の被弾・撃破・周りの数を記録
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestCurve final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 難度曲線の実測（VFXL_BATTLE_AUTOTEST=curve、2026-10-04）
// 普通に湧かせたまま 8 分（m_RunTime。VFXL_CURVE_MIN で変える）、人の代わりに自動で遊ぶ:
//   ・プレイヤーは始点の周りの半径 8m の円を全速で回る（引き撃ち）
//   ・三択 / 四択は「今置ける魔法・ルーン > 拡張枠 > 先頭」で選び、バックパックの中央に近い所へ自動で置く
//   ・HP は大きくして毎フレーム満タンに戻す。減った分 = 実際に受けたダメージ（無敵時間込み）。
//     GPU の playerDamage の累計の差 = 無敵時間を無視した生の被弾
// 30 秒毎に `curve t alive kills level items frames mul contact spawn raw eff near3 near8 ttd`
//   （kills = この 30 秒の撃破、raw / eff = 毎秒の被弾、near = 1 秒毎のリードバックでプレイヤーから 3 / 8m 以内の平均数、
//    ttd = HP 100 がこの eff で何秒持つか）。2 / 4 / 6 / 8 分で `curve look <分>m`、最後に `curve done`。
// 三択の間は gameplay が止まるので Update から呼ぶ
// ============================================================
void AutoTestCurve::Run(float dt)
{
    static bool s_Init = false, s_Done = false;
    static Vector3 s_Center;
    static float s_Angle = 0.0f, s_WinStart = 0.0f, s_NextNear = 0.0f, s_EffDmg = 0.0f, s_EndMin = 8.0f;
    static uint32_t s_RawStart = 0, s_KillStart = 0;
    static int s_NearSamples = 0, s_Near3 = 0, s_Near8 = 0, s_NextLook = 2;
    if (s_Done || !m_Registry.IsValid(m_Player)) return;
    if (!m_Registry.Has<BackpackComponent>(m_Player) || !m_Registry.Has<SpellbookComponent>(m_Player)) return;

    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    const auto& c = m_Swarm.GetCounters();
    constexpr float kBigHp = 100000.0f;

    if (!s_Init)
    {
        char env[16] = {};
        if (GetEnvironmentVariableA("VFXL_CURVE_MIN", env, sizeof(env)) > 0) s_EndMin = (float)std::atof(env);
        // VFXL_OLD_DIFFICULTY=1：2026-10-04 以前の直線の難度（湧き 1 → 10 分 8 体/秒、HP・ダメージとも 1 + 0.12 × 分）。
        // 2 点の表で同じになる（最後の区間の傾きで伸びるので 10 分以降も同じ）。改める前後の比較用
        if (GetEnvironmentVariableA("VFXL_OLD_DIFFICULTY", nullptr, 0) > 0)
        {
            m_Mobs.curve.points = { { 0.0f, 1.0f, 1.0f, 1.0f }, { 10.0f, 8.0f, 2.2f, 2.2f } };
            AutoTestLog("curve uses the old linear difficulty");
        }
        s_Center = tf.position;
        hp.max = hp.current = kBigHp;
        s_RawStart = c.playerDamage;
        s_KillStart = c.killCount;
        s_WinStart = m_RunTime;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        s_Init = true;
        char line[96];
        snprintf(line, sizeof(line), "curve start (run %.0f s, until %.0f min)", m_RunTime, s_EndMin);
        AutoTestLog(line);
        return;
    }

    // ---- 受けたダメージ（前のフレームに満タンへ戻した後に減った分）→ 満タンへ ----
    // HP の能力アップで上限が増えても、毎フレーム上限ごと戻すので数えには影響しない
    s_EffDmg += (std::max)(0.0f, hp.max - hp.current);
    hp.max = hp.current = kBigHp;

    // ---- バックパックの空きに置く（枠は frame = true）。中央に近い所・4 向きから選ぶ ----
    auto bestSpot = [&bp](ItemID id, bool frame, int& outR, int& outC, int& outRot) -> bool
    {
        const int G = BackpackComponent::GRID;
        const float mid = (float)(G - 1) * 0.5f;
        float best = 1.0e9f;
        for (int rot = 0; rot < 4; ++rot)
            for (int r = 0; r < G; ++r)
                for (int col = 0; col < G; ++col)
                {
                    const bool ok = frame ? BackpackLogic::CanPlaceFrame(bp, id, r, col, rot)
                                          : BackpackLogic::CanPlace(bp, id, r, col, rot);
                    if (!ok) continue;
                    const float cr = frame ? (float)r + 1.0f : (float)r;   // 枠は左上が錨 → 中心で比べる
                    const float cc = frame ? (float)col + 1.0f : (float)col;
                    const float d = (cr - mid) * (cr - mid) + (cc - mid) * (cc - mid);
                    if (d < best) { best = d; outR = r; outC = col; outRot = rot; }
                }
        return best < 1.0e9f;
    };

    // ---- 三択 / 四択：今置ける魔法・ルーン > 拡張枠 > 先頭 ----
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        if (lv.IsChoosing())
        {
            // 0 = 置ける基礎の攻撃魔法、1 = 拡張枠、2 = 魔力の能力アップ、3 = 他の能力アップ、
            // 4 = ルーン・上級魔法・置けない物（ルーンは消費 MP を増やす物が多く、雑に置くと MP が尽きて撃てなくなる）
            ItemID pick = lv.pendingChoices.front();
            int rank = 9;
            for (ItemID id : lv.pendingChoices)
            {
                int rk = 4, r = 0, col = 0, rot = 0;
                const ItemCommon* ic = ItemDatabase::GetCommon(id);
                const ItemCategory cat = ItemDatabase::GetCategory(id);
                if (ItemDatabase::IsFrame(id)) rk = 1;
                else if (const StatItemDef* st = ItemDatabase::GetStat(id))
                    rk = (st->kind == StatKind::MaxMana || st->kind == StatKind::ManaRegen) ? 2 : 3;
                else if ((cat == ItemCategory::Projectile || cat == ItemCategory::Area) && ic && ic->triggeredBy.empty()
                    && bestSpot(id, false, r, col, rot))
                    rk = 0;
                if (rk < rank) { rank = rk; pick = id; }
            }
            LevelUpSystem::Choose(m_Registry, m_Player, pick);

            // 持っているのに置いていない物を置く（枠を先に。枠が増えると魔法の置き場が増える）
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            bool changed = false;
            for (int pass = 0; pass < 2; ++pass)
                for (const auto& e : book.entries)
                {
                    const bool frame = ItemDatabase::IsFrame(e.id);
                    if (frame != (pass == 0)) continue;
                    int placed = frame ? BackpackLogic::CountPlacedFrames(bp, e.id) : BackpackLogic::CountPlaced(bp, e.id);
                    while (placed < e.count)
                    {
                        int r = 0, col = 0, rot = 0;
                        if (!bestSpot(e.id, frame, r, col, rot)) break;
                        if ((frame ? BackpackLogic::PlaceFrame(bp, e.id, r, col, rot) : BackpackLogic::Place(bp, e.id, r, col, rot)) < 0) break;
                        ++placed;
                        changed = true;
                    }
                }
            if (changed) bp.dirty = true;

            const ItemCommon* ic = ItemDatabase::GetCommon(pick);
            char line[160];
            snprintf(line, sizeof(line), "curve pick %s (level %d, run %.0f) items %d frames %d",
                ic ? ic->name : "?", lv.level, m_RunTime, (int)bp.items.size(), (int)bp.frames.size());
            AutoTestLog(line);
        }
    }

    // ---- 動き：始点の周りの円を全速で回る ----
    {
        auto& cam = m_Camera.Camera();
        Vector3 camF = cam.GetForward(); camF.y = 0.0f; camF.Normalize();
        Vector3 camR = cam.GetRight();   camR.y = 0.0f; camR.Normalize();
        s_Angle += dt * 0.6f;
        const Vector3 goal = s_Center + Vector3(std::cos(s_Angle + 0.6f), 0.0f, std::sin(s_Angle + 0.6f)) * 8.0f;
        Vector3 d = goal - tf.position;
        d.y = 0.0f;
        if (d.LengthSquared() > 1e-4f) d.Normalize();
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testSlide = false;
        m_PlayerControlSystem.testJump = false;
        m_PlayerControlSystem.testMove = Vector2(d.Dot(camR), d.Dot(camF));
    }

    // ---- 毎フレーム：MP の残りの割合（MP が足りず撃てない時間がどれだけあるか）----
    static float s_MpSum = 0.0f;
    static int s_MpSamples = 0;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        const auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        s_MpSum += (mp.max > 0.0f) ? mp.current / mp.max : 0.0f;
        ++s_MpSamples;
    }

    // ---- 1 秒毎：プレイヤーの近くの数 ----
    if (m_RunTime >= s_NextNear)
    {
        s_NextNear = m_RunTime + 1.0f;
        static std::vector<Swarm::Enemy> s_En;
        static std::vector<uint32_t> s_St;
        if (m_Swarm.DebugReadEnemies(s_En, s_St))
        {
            for (size_t i = 0; i < s_En.size(); ++i)
            {
                if (s_St[i] == Swarm::kStateDead) continue;
                const float dx = s_En[i].position.x - tf.position.x;
                const float dz = s_En[i].position.z - tf.position.z;
                const float d2 = dx * dx + dz * dz;
                if (d2 < 9.0f) ++s_Near3;
                if (d2 < 64.0f) ++s_Near8;
            }
            ++s_NearSamples;
        }
    }

    // ---- 30 秒毎の 1 行 ----
    if (m_RunTime >= s_WinStart + 30.0f)
    {
        const float span = m_RunTime - s_WinStart;
        const float raw = (float)(c.playerDamage - s_RawStart) * 0.01f / span;
        const float eff = s_EffDmg / span;
        const float ns = (float)(std::max)(1, s_NearSamples);
        const int level = m_Registry.Has<LevelComponent>(m_Player) ? m_Registry.Get<LevelComponent>(m_Player).level : 0;
        char line[320];
        snprintf(line, sizeof(line),
            "curve t %.0f alive %u kills %u level %d items %d frames %d mul %.2f contact %.1f spawn %.2f raw %.1f eff %.1f near3 %.1f near8 %.1f ttd %.1f",
            m_RunTime, c.aliveEnemies, c.killCount - s_KillStart, level, (int)bp.items.size(), (int)bp.frames.size(),
            m_Mobs.GetHpMul(), m_Swarm.GetAIParams().contactDamage, m_Mobs.Director().spawnPerSecond,
            raw, eff, (float)s_Near3 / ns, (float)s_Near8 / ns, eff > 0.01f ? 100.0f / eff : 999.0f);
        AutoTestLog(line);

        // MP：平均の残り割合・回復（毎秒）・杖が全部撃ち続けた時の消費（毎秒。集約後の消費 / 発動間隔の和）
        float demand = 0.0f, regen = 0.0f;
        if (m_Registry.Has<WandComponent>(m_Player))
        {
            const auto& w = m_Registry.Get<WandComponent>(m_Player);
            for (const auto& sp : w.spells)
                if (sp.castInterval > 0.0f && !sp.triggered) demand += sp.manaCost * (float)sp.castCount / sp.castInterval;
            for (const auto& ar : w.areas) if (ar.castInterval > 0.0f && !ar.triggered) demand += ar.manaCost / ar.castInterval;
        }
        if (m_Registry.Has<ManaComponent>(m_Player)) regen = m_Registry.Get<ManaComponent>(m_Player).EffectiveRegen(level);
        snprintf(line, sizeof(line), "curve mp t %.0f avg %.2f regen %.1f demand %.1f",
            m_RunTime, s_MpSamples > 0 ? s_MpSum / (float)s_MpSamples : 0.0f, regen, demand);
        AutoTestLog(line);

        s_WinStart = m_RunTime;
        s_RawStart = c.playerDamage;
        s_KillStart = c.killCount;
        s_EffDmg = 0.0f;
        s_Near3 = s_Near8 = s_NearSamples = 0;
        s_MpSum = 0.0f;
        s_MpSamples = 0;
    }

    if (s_NextLook <= (int)s_EndMin && m_RunTime >= (float)s_NextLook * 60.0f)
    {
        char line[48];
        snprintf(line, sizeof(line), "curve look %dm", s_NextLook);
        AutoTestLog(line);
        s_NextLook += 2;
    }
    if (m_RunTime >= s_EndMin * 60.0f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("curve done");
        s_Done = true;
    }
}

REGISTER_BATTLE_AUTOTEST("curve", AutoTestCurve)
