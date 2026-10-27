// ============================================================
// AutoTestElites.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=elites（VFXL_STAGE=1 / 2 / 3 で面を選ぶ = A / B の個体が変わる）
// 面の固定エリート（2026-10-25）：技の当たりと避け方、詞缀、倒した後の金色の宝箱。
//   A0 / B0：プレイヤーは立ったまま（技が 1 回当たるはず）
//   A1 / B1：溜めが始まったら型毎の避け方で動く（当たらないはず）
//     大斧・炎の息 = 離れる、矢・突進 = 横へ、影の剣士 = 向いている方へ走る（背後の輪から出る）、呪いの玉 = 離れ続ける
//   X：A に詞缀を 4 つ全部（疾風・鉄壁・雷光・狂暴）。プレイヤーは撃たず、CPU が 0.25 秒毎にエリートの所へ傷の範囲を置いて削る
//      → 盾が割れる・狂暴になる・雷が落ちる・死ぬ → 金色の宝箱が出る → 近くへ行って F → 金貨が減らずに四択が出るか
// 0.25 秒毎 `elites <段> t phase hits lost dist hp shield enraged`。段の終わりに `elites <段> result …`。
// `elites look <名>` で自分で写す（VFXL_SELF_SHOT）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestElites final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }
        // 宝箱を開けると四択で gameplay が止まるので、開けた後の確認だけはこちら（止まっていても回る）
        void UpdateFrame(float dt) override;

    private:
        void Run();
        float m_OpenWait = -1.0f;   // F を押してからの実時間（-1 = まだ）
        int   m_GoldBefore = 0;
    };

    constexpr float kSpawnDist = 9.0f;
    constexpr float kPhaseMax = 14.0f;   // 1 段の最長（技が出なくても次へ）
}

void AutoTestElites::Run()
{
    char line[256];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    static const char* s_Phase = "";
    static float s_PhaseStart = 0.0f, s_NextLog = 0.0f, s_LastHp = 0.0f, s_Lost = 0.0f, s_DodgeEnd = -1.0f, s_ShotAt = -1.0f;
    static float s_NextChip = 0.0f, s_DeadAt = -1.0f, s_ChestAt = -1.0f;
    static int s_LastPhase = 0, s_Moves0 = 0, s_HitsAtStart = 0, s_Dodges = 0;
    static bool s_Dodge = false, s_LookWind = false, s_LookHit = false, s_Opened = false;
    static int s_GoldBefore = 0;
    static Vector3 s_Origin;
    static Vector2 s_DodgeDir;
    static std::string s_ShotName;

    auto lookAt = [&](const char* name)
    {
        snprintf(line, sizeof(line), "elites look %s", name);
        AutoTestLog(line);
    };
    auto toMove = [&](const Vector2& worldDir) -> Vector2
    {
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        Vector3 r = cam.GetRight(); r.y = 0.0f; r.Normalize();
        return Vector2(worldDir.x * r.x + worldDir.y * r.z, worldDir.x * f.x + worldDir.y * f.z);
    };
    auto elitePos = [&](Vector3& out) -> bool
    {
        std::vector<Vector3> ps;
        m_StageElites.GetPositions(ps);
        if (ps.empty()) return false;
        out = ps[0];
        return true;
    };
    auto beginPhase = [&](const char* ph, bool ranged, uint32_t affixes, float hp, bool dodge)
    {
        m_Swarm.KillAll();
        s_Phase = ph;
        s_PhaseStart = m_AutoTime;
        s_NextLog = m_AutoTime + 0.25f;
        s_Lost = 0.0f;
        s_LastPhase = 0;
        s_Moves0 = m_StageElites.GetStats().moves;
        s_HitsAtStart = (int)m_EliteHits;
        s_Dodge = dodge;
        s_DodgeEnd = -1.0f;
        s_Dodges = 0;
        s_LookWind = s_LookHit = false;
        s_DeadAt = s_ChestAt = -1.0f;
        s_Opened = false;
        pcs.testMove = Vector2::Zero;
        // プレイヤーを最初の場所へ戻し、正面 +Z kSpawnDist に出す（カメラは +Z を向く）
        tf.position = Vector3(s_Origin.x, m_Grid.SampleHeight(s_Origin.x, s_Origin.z) + 1.0f, s_Origin.z);
        tf.rotation.y = 0.0f;
        const Vector3 p(s_Origin.x, 0.0f, s_Origin.z + kSpawnDist);
        // 盾の削れ具合を見るため、詞缀の段は HP 低め（250 → 1/2.5）
        const bool ok = m_StageElites.SpawnAt(m_Grid, m_Swarm, p, ranged, affixes, hp);
        snprintf(line, sizeof(line), "elites %s start ranged %d affixes %u hp %.0f ok %d type %d", ph, ranged ? 1 : 0, affixes, hp, ok ? 1 : 0,
            (int)(ranged ? m_StageElites.TypeB() : m_StageElites.TypeA()));
        AutoTestLog(line);
        m_StageElites.DebugReady();
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
            s_LastHp = hp.current;
        }
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        s_Origin = tf.position;
        cam.SetYaw(0.0f);
        cam.SetPitch(34.0f);
        cam.distance = 17.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        snprintf(line, sizeof(line), "elites stage %d typeA %d typeB %d", m_StageIndex, (int)m_StageElites.TypeA(), (int)m_StageElites.TypeB());
        AutoTestLog(line);
        beginPhase("A0", false, 0u, 250.0f, false);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0 || m_AutoStep >= 7) return;

    // HP の減り（技 + 接触）
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        if (hp.current < s_LastHp - 0.5f) s_Lost += s_LastHp - hp.current;
        s_LastHp = hp.current;
    }
    const int ph = m_StageElites.DebugPhase(0);
    const int hits = (int)m_EliteHits - s_HitsAtStart;
    const int movesDone = m_StageElites.GetStats().moves - s_Moves0;
    const bool ranged = (s_Phase[0] == 'B');
    const EliteType type = ranged ? m_StageElites.TypeB() : m_StageElites.TypeA();
    Vector3 ep;
    const bool seen = elitePos(ep);
    const float dist = seen ? std::sqrt((ep.x - tf.position.x) * (ep.x - tf.position.x) + (ep.z - tf.position.z) * (ep.z - tf.position.z)) : -1.0f;

    // ---- 避け方：溜め（段 1）に入った瞬間に型毎の向きへ走る ----
    if (s_Dodge && seen && ph == 1 && s_LastPhase != 1)
    {
        Vector2 away(tf.position.x - ep.x, tf.position.z - ep.z);
        away = (away.LengthSquared() > 1e-4f) ? away / away.Length() : Vector2(0.0f, -1.0f);
        const Vector2 side((s_Dodges & 1) ? -away.y : away.y, (s_Dodges & 1) ? away.x : -away.x);
        float dur = 1.2f;
        switch (type)
        {
        case EliteType::Archer:    s_DodgeDir = side; dur = 0.45f; break;
        case EliteType::Rider:     s_DodgeDir = side; dur = 0.6f; break;
        case EliteType::Shade:
        {
            const float yaw = DirectX::XMConvertToRadians(tf.rotation.y);
            s_DodgeDir = Vector2(std::sin(yaw), std::cos(yaw));
            if (s_DodgeDir.Dot(away) < -0.3f) s_DodgeDir = away;   // 敵の方へ走らない
            dur = 1.4f;
            break;
        }
        case EliteType::Librarian: s_DodgeDir = away; dur = 6.5f; break;
        default:                   s_DodgeDir = away; dur = 1.3f; break;   // 大斧・炎の息
        }
        s_DodgeEnd = m_AutoTime + dur;
        ++s_Dodges;
        snprintf(line, sizeof(line), "elites %s dodge t %.2f dir %.2f %.2f for %.2f", s_Phase, m_AutoTime - s_PhaseStart, s_DodgeDir.x, s_DodgeDir.y, dur);
        AutoTestLog(line);
    }
    if (s_DodgeEnd > 0.0f)
    {
        if (m_AutoTime < s_DodgeEnd)
        {
            // 呪いの玉：離れ続ける向きを毎フレーム取り直す（玉は追ってくる）
            if (type == EliteType::Librarian && seen)
            {
                Vector2 away(tf.position.x - ep.x, tf.position.z - ep.z);
                if (away.LengthSquared() > 1e-4f) s_DodgeDir = away / away.Length();
            }
            pcs.testMove = toMove(s_DodgeDir);
        }
        else
        {
            pcs.testMove = Vector2::Zero;
            s_DodgeEnd = -1.0f;
        }
    }
    if (ph == 1 && s_LastPhase != 1 && !s_LookWind)
    {
        s_LookWind = true;
        s_ShotAt = m_AutoTime + 0.45f;
        s_ShotName = std::string(s_Phase) + "windup";
    }
    if (hits > 0 && !s_LookHit)
    {
        s_LookHit = true;
        s_ShotAt = m_AutoTime + 0.05f;
        s_ShotName = std::string(s_Phase) + "hit";
    }
    if (s_ShotAt > 0.0f && m_AutoTime >= s_ShotAt)
    {
        s_ShotAt = -1.0f;
        lookAt(s_ShotName.c_str());
    }
    s_LastPhase = ph;

    // ---- X：詞缀。0.25 秒毎にエリートの所へ傷の範囲（盾があれば半分しか通らない）----
    const bool affixPhase = (s_Phase[0] == 'X');
    if (affixPhase && seen && m_AutoTime >= s_NextChip)
    {
        s_NextChip = m_AutoTime + 0.25f;
        Swarm::Area a;
        a.center = Vector3(ep.x, ep.y, ep.z);
        a.radius = 2.0f;
        a.damage = 6.0f;
        a.tickInterval = 0.2f;
        a.timeLeft = 0.22f;
        m_Swarm.SpawnArea(a);
    }

    // ---- 記録 ----
    if (m_AutoTime >= s_NextLog)
    {
        s_NextLog += 0.25f;
        std::vector<EliteAttacks::Bar> bars;
        m_StageElites.GetBars(bars);
        const float hpr = bars.empty() ? -1.0f : bars[0].hp;
        snprintf(line, sizeof(line), "elites %s t %.2f phase %d hits %d lost %.0f dist %.1f hp %.2f shield %d enraged %d strikes %d orbs %d arrows %d",
            s_Phase, m_AutoTime - s_PhaseStart, ph, hits, s_Lost, dist, hpr, bars.empty() ? 0 : (bars[0].shield ? 1 : 0),
            bars.empty() ? 0 : (bars[0].enraged ? 1 : 0), m_StageElites.GetStats().strikes, m_StageElites.GetStats().orbs, m_StageElites.GetStats().arrows);
        AutoTestLog(line);
        if (affixPhase)
        {
            const float tt = m_AutoTime - s_PhaseStart;
            if (tt > 1.5f && tt < 1.76f) lookAt("Xaffixes");
        }
    }

    // ---- 段の終わり ----
    const float elapsed = m_AutoTime - s_PhaseStart;
    const bool moveOver = movesDone >= 1 && ph == 0 && s_DodgeEnd < 0.0f && elapsed > 2.0f && m_StageElites.ActiveShots() == 0;   // 玉・矢が消えるまで待つ
    if (!affixPhase && (moveOver || elapsed > kPhaseMax))
    {
        snprintf(line, sizeof(line), "elites %s result moves %d hits %d lost %.0f dodges %d", s_Phase, movesDone, hits, s_Lost, s_Dodges);
        AutoTestLog(line);
        pcs.testMove = Vector2::Zero;
        if (m_AutoStep == 1) { beginPhase("A1", false, 0u, 250.0f, true); m_AutoStep = 2; }
        else if (m_AutoStep == 2) { beginPhase("B0", true, 0u, 250.0f, false); m_AutoStep = 3; }
        else if (m_AutoStep == 3) { beginPhase("B1", true, 0u, 250.0f, true); m_AutoStep = 4; }
        else if (m_AutoStep == 4)
        {
            beginPhase("X", false, kEliteAffixSwift | kEliteAffixIron | kEliteAffixThunder | kEliteAffixBerserk, 100.0f, false);
            s_NextChip = m_AutoTime + 1.0f;
            if (m_Registry.Has<WalletComponent>(m_Player)) m_Registry.Get<WalletComponent>(m_Player).gold = 7.0f;   // 宝箱が無料か見る
            if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).expBase = 1.0e9f;   // 倒した経験値でレベルアップさせない（宝箱の四択が出せなくなる）
            m_AutoStep = 5;
        }
        return;
    }

    if (affixPhase)
    {
        const auto& st = m_StageElites.GetStats();
        static int s_LastBreaks = 0, s_LastEnrages = 0, s_LastStrikes = 0;
        if (elapsed < 0.1f) { s_LastBreaks = st.shieldBreaks; s_LastEnrages = st.enrages; s_LastStrikes = st.strikes; }
        if (st.shieldBreaks != s_LastBreaks) { s_LastBreaks = st.shieldBreaks; snprintf(line, sizeof(line), "elites X shield broke t %.2f", elapsed); AutoTestLog(line); s_ShotAt = m_AutoTime + 0.08f; s_ShotName = "Xbreak"; }
        if (st.enrages != s_LastEnrages) { s_LastEnrages = st.enrages; snprintf(line, sizeof(line), "elites X enraged t %.2f", elapsed); AutoTestLog(line); s_ShotAt = m_AutoTime + 0.6f; s_ShotName = "Xberserk"; }
        if (st.strikes != s_LastStrikes && s_LastStrikes == 0) { s_ShotAt = m_AutoTime + 0.6f; s_ShotName = "Xthunder"; }
        s_LastStrikes = st.strikes;

        // 死んだ → 金色の宝箱の数
        if (m_AutoStep == 5 && m_StageElites.Alive() == 0 && elapsed > 2.0f && s_DeadAt < 0.0f)
        {
            s_DeadAt = m_AutoTime;
            int free = 0;
            Vector3 chestPos;
            for (Entity c : m_Crates.GetCrates())
                if (m_Crates.IsFree(c) && m_Registry.Has<InteractableComponent>(c)) { ++free; chestPos = m_Registry.Get<InteractableComponent>(c).basePos; }
            snprintf(line, sizeof(line), "elites X dead t %.2f breaks %d enrages %d strikes %d freeChests %d", elapsed, st.shieldBreaks, st.enrages, st.strikes, free);
            AutoTestLog(line);
            if (free > 0)
            {
                // 宝箱の 1.6m 手前（宝箱を向く）
                Vector2 d(chestPos.x - tf.position.x, chestPos.z - tf.position.z);
                d = (d.LengthSquared() > 1e-4f) ? d / d.Length() : Vector2(0.0f, 1.0f);
                const Vector3 at(chestPos.x - d.x * 1.6f, 0.0f, chestPos.z - d.y * 1.6f);
                tf.position = Vector3(at.x, m_Grid.SampleHeight(at.x, at.z) + 1.0f, at.z);
                tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(d.x, d.y));
                cam.distance = 7.0f;
                cam.SetPitch(24.0f);
                cam.SnapToTarget();
                s_ChestAt = m_AutoTime + 1.2f;
                s_GoldBefore = m_Registry.Has<WalletComponent>(m_Player) ? m_Registry.Get<WalletComponent>(m_Player).Coins() : 0;
            }
            m_AutoStep = 6;
        }
        if (m_AutoStep == 6 && s_ChestAt > 0.0f && m_AutoTime >= s_ChestAt - 0.6f && m_AutoTime < s_ChestAt - 0.55f)
            lookAt("chest");
        if (m_AutoStep == 6 && s_ChestAt > 0.0f && m_AutoTime >= s_ChestAt && !s_Opened)
        {
            s_Opened = true;
            m_AutoInteract = true;
            m_GoldBefore = s_GoldBefore;
            m_OpenWait = 0.0f;   // 続きは UpdateFrame
        }
        if (m_AutoStep == 5 && elapsed > 40.0f)
        {
            AutoTestLog("elites X timeout");
            AutoTestLog("elites done");
            m_AutoStep = 7;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("elites", AutoTestElites);

void AutoTestElites::UpdateFrame(float dt)
{
    if (m_OpenWait < 0.0f || m_AutoStep != 6) return;
    m_OpenWait += dt;
    if (m_OpenWait < 0.7f) return;
    int free = 0;
    for (Entity c : m_Crates.GetCrates()) if (m_Crates.IsFree(c)) ++free;
    const int gold = m_Registry.Has<WalletComponent>(m_Player) ? m_Registry.Get<WalletComponent>(m_Player).Coins() : -1;
    const bool offered = m_Registry.Has<LevelComponent>(m_Player) && !m_Registry.Get<LevelComponent>(m_Player).pendingChoices.empty();
    char line[160];
    snprintf(line, sizeof(line), "elites chest opened freeLeft %d gold %d -> %d offered %d", free, m_GoldBefore, gold, offered ? 1 : 0);
    AutoTestLog(line);
    AutoTestLog("elites look chestopen");
    AutoTestLog("elites done");
    m_OpenWait = -1.0f;
    m_AutoStep = 7;
}
