// ============================================================
// AutoTestGustAll.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=gustall（2026-10-27、ユーザー「突風の全体テスト、各方面」）
// 突風を場面ごとに撃たせ、1 発ごとに「標的・吹く向き・見た目（粒子の発射の向き）・風源の表の押す向き・半径」と、
// 場面の終わりに「各敵がどれだけ飛んだか」「マントの節点 / 骨の書き戻しが壊れていないか」を書く。
//   dirN          : 5m に雑魚 1 体（0〜315 度の 8 方向を 1 つずつ）。向きの誤差と飛んだ距離
//   bossFar       : 雑魚 4.5m @90 + ボス 12m @0（狙いの優先度でボスを狙うと突風は撃てない）
//   elite5mob3    : エリート 5m @0 + 雑魚 3m @180（3.5m 内の雑魚が優先 → 雑魚へ吹く）
//   elite5mob5    : エリート 5m @0 + 雑魚 5m @180（エリートが優先 → 雑魚は飛ばない）
//   magnifier     : 拡大鏡（半径 / 見た目 / 風源 x1.2）
//   frameArea     : 範囲の枠（半径 x1.2）
//   orb           : 水晶玉に貯蔵（光球から撃つ。根元 = 光球の真下）
//   twister       : 渦 + 突風 → 竜巻
//   multi         : 突風 3 個（マントへの負荷）
//   cpuDummy      : 地形を建て直す（RebuildTerrain が置くデバッグの無敵エリートの的）→ その近くで撃つ
// 行：`gustall cast <場面> ...` / `gustall wind ...` / `gustall result <場面> ...` / `gustall cape ...` / `gustall look <名>`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXParticleEntry.h"
#include "ECS/System/WeaponSystem.h"
#include "Swarm/AreaVFXPlayer.h"

namespace
{
    constexpr float kRad = 57.29578f;

    float AngleOf(float x, float z) { return std::atan2(x, z) * kRad; }
    float AngleDiff(float a, float b)
    {
        float d = std::fmod(a - b + 540.0f, 360.0f) - 180.0f;
        return std::abs(d);
    }

    struct Spawned { float angle, dist; uint32_t kind; };

    class AutoTestGustAll final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void BeginPhase();
        void EndPhase();
        void Spawn(float angleDeg, float dist, uint32_t kind, float hp = 100000.0f);
        void LogCast(const WeaponSystem::GustDebug& g);
        void TrackCape();
        BackpackComponent& Bag() { return m_Registry.Get<BackpackComponent>(m_Player); }

        int   m_Phase = -1;
        float m_PhaseStart = 0.0f;
        float m_PhaseDur = 0.0f;
        bool  m_Unpaused = false;
        int   m_Casts = 0;
        float m_WindCheckAt = -1.0f;
        Vector3 m_WindCaster;
        std::vector<Spawned> m_Spawned;
        std::string m_Name;
        // マント
        float m_CapeBaseFar = -1.0f, m_CapeBaseBone = -1.0f;
        float m_CapeMaxFar = 0.0f, m_CapeMaxBone = 0.0f;
        bool  m_CapeBad = false;
        bool  m_LookedThisPhase = false;
        int   m_DummyCount = 0;
    };
}

void AutoTestGustAll::Spawn(float angleDeg, float dist, uint32_t kind, float hp)
{
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const float a = angleDeg / kRad;
    const float x = tf.position.x + std::sin(a) * dist, z = tf.position.z + std::cos(a) * dist;
    m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + m_Swarm.GetAIParams().groundY, z), hp, 0.0f, kind);
    m_Spawned.push_back({ angleDeg, dist, kind });
}

void AutoTestGustAll::BeginPhase()
{
    m_Swarm.KillAll();
    m_Swarm.ClearAreas();
    m_Swarm.ClearProjectiles();
    m_Spawned.clear();
    m_Casts = 0;
    m_Unpaused = false;
    m_WindCheckAt = -1.0f;
    m_CapeMaxFar = m_CapeMaxBone = 0.0f;
    m_CapeBad = false;
    m_LookedThisPhase = false;
    m_PhaseStart = m_AutoTime;
    m_PhaseDur = 2.0f;

    auto& bp = Bag();
    ClearBackpackItems(bp);
    FillAllFrames(bp);
    char line[256];
    const int p = m_Phase;
    if (p < 8)
    {
        snprintf(line, sizeof(line), "dir%d", p * 45);
        m_Name = line;
        BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        Spawn((float)(p * 45), 5.0f, Swarm::kEnemyKindMob);
    }
    else if (p == 8)
    {
        m_Name = "bossFar";
        BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        Spawn(90.0f, 4.5f, Swarm::kEnemyKindMob);
        Spawn(0.0f, 12.0f, Swarm::kEnemyKindBoss, 1.0e6f);
        m_PhaseDur = 3.0f;
    }
    else if (p == 9)
    {
        m_Name = "elite5mob3";
        BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        Spawn(0.0f, 5.0f, Swarm::kEnemyKindElite);
        Spawn(180.0f, 3.0f, Swarm::kEnemyKindMob);
        m_PhaseDur = 2.6f;
    }
    else if (p == 10)
    {
        m_Name = "elite5mob5";
        BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        Spawn(0.0f, 5.0f, Swarm::kEnemyKindElite);
        Spawn(180.0f, 5.0f, Swarm::kEnemyKindMob);
        m_PhaseDur = 2.6f;
    }
    else if (p == 11)
    {
        m_Name = "magnifier";
        const int g = BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        const int m = BackpackLogic::Place(bp, ItemID::Magnifier, 5, 4, 0);   // 影響マス (4,3) = 突風の左端
        snprintf(line, sizeof(line), "gustall place magnifier gust %d mag %d", g, m);
        AutoTestLog(line);
        Spawn(45.0f, 5.0f, Swarm::kEnemyKindMob);
    }
    else if (p == 12)
    {
        m_Name = "frameArea";
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        const int f0 = BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, 3, 3, 0);
        const int f1 = BackpackLogic::PlaceFrame(bp, ItemID::FrameArea, 3, 6, 0);
        const int g = BackpackLogic::Place(bp, ItemID::Gust, 3, 5, 0);   // (3,5) は 3x3、(3,6) は範囲の枠
        snprintf(line, sizeof(line), "gustall place frameArea frame3 %d frameArea %d gust %d", f0, f1, g);
        AutoTestLog(line);
        Spawn(135.0f, 5.0f, Swarm::kEnemyKindMob);
    }
    else if (p == 13)
    {
        m_Name = "orb";
        const int c = BackpackLogic::Place(bp, ItemID::CrystalBall, 4, 4, 0);
        const int g = BackpackLogic::Place(bp, ItemID::Gust, 4, 2, 0);   // 右端 (4,3) = 水晶玉の左の影響マス
        snprintf(line, sizeof(line), "gustall place orb crystal %d gust %d storedBy %d", c, g,
            g >= 0 ? BackpackLogic::StoredBy(bp, g) : -9);
        AutoTestLog(line);
        Spawn(0.0f, 4.0f, Swarm::kEnemyKindMob);
        Spawn(120.0f, 4.0f, Swarm::kEnemyKindMob);
        Spawn(240.0f, 4.0f, Swarm::kEnemyKindMob);
        m_PhaseDur = 7.0f;
    }
    else if (p == 14)
    {
        m_Name = "twister";
        BackpackLogic::Place(bp, ItemID::Twister, 3, 4, 0);
        BackpackLogic::Place(bp, ItemID::VoidVortex, 4, 2, 0);
        BackpackLogic::Place(bp, ItemID::Gust, 4, 5, 0);
        for (int k = 0; k < 6; ++k) Spawn(-40.0f + 16.0f * k, 5.5f + (k % 2), Swarm::kEnemyKindMob);
        m_PhaseDur = 6.0f;
    }
    else if (p == 15)
    {
        m_Name = "multi";
        BackpackLogic::Place(bp, ItemID::Gust, 1, 1, 0);
        BackpackLogic::Place(bp, ItemID::Gust, 3, 1, 0);
        BackpackLogic::Place(bp, ItemID::Gust, 5, 1, 0);
        for (int k = 0; k < 8; ++k) Spawn(45.0f * k, 4.0f, Swarm::kEnemyKindMob);
        m_PhaseDur = 4.0f;
    }
    else if (p == 16)
    {
        m_Name = "cpuDummy";
        auto countDummies = [&](Vector3* pos) {
            int n = 0;
            m_Registry.CreateView<TransformComponent, ColliderComponent>().EachFrom<ColliderComponent>(
                [&](Entity e, TransformComponent& t, ColliderComponent& c) {
                    if (e == m_Player || c.layer != Layer_Enemy) return;
                    if (pos && n == 0) *pos = t.position;
                    ++n;
                });
            return n;
        };
        const int before = countDummies(nullptr);
        RebuildTerrain();   // RespawnElites が呼ばれる（トレーニングのフィールド切替と同じ）
        Vector3 dummy;
        m_DummyCount = countDummies(&dummy);
        snprintf(line, sizeof(line), "gustall cpuDummy before %d after %d pos %.1f,%.1f,%.1f", before, m_DummyCount, dummy.x, dummy.y, dummy.z);
        AutoTestLog(line);
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        if (m_DummyCount > 0)
        {
            const float x = dummy.x, z = dummy.z - 5.0f;
            tf.position = Vector3(x, m_Grid.SampleHeight(x, z) + 1.0f, z);
            if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        }
        BackpackLogic::Place(bp, ItemID::Gust, 4, 3, 0);
        Spawn(180.0f, 6.0f, Swarm::kEnemyKindMob);   // 的（5m @0）の反対 6m。標的は的になり、風は的の方へ
        m_PhaseDur = 3.0f;
    }
    else if (p == 17)
    {
        // アイススパイクの線（2026-10-27）：Boss 14m @0 + 雑魚 5m @90。線は優先度なしの一番近い敵（雑魚）の向きへ走るはず
        m_Name = "iceSpike";
        auto indexOf = [&](ItemID id) { for (int i = 0; i < (int)bp.items.size(); ++i) if (bp.items[i].id == id) return i; return -1; };
        BackpackLogic::Place(bp, ItemID::IceSpike, 4, 4, 0);
        for (ItemID need : { ItemID::IceLance, ItemID::StoneShot })
        {
            bool placed = false;
            for (int r = 0; r < BackpackComponent::GRID && !placed; ++r)
                for (int c = 0; c < BackpackComponent::GRID && !placed; ++c)
                {
                    const int idx = BackpackLogic::Place(bp, need, r, c, 0);
                    if (idx < 0) continue;
                    const auto drv = BackpackLogic::GetTriggerDrivers(bp, indexOf(ItemID::IceSpike));
                    if (std::find(drv.begin(), drv.end(), indexOf(need)) != drv.end()) placed = true;
                    else BackpackLogic::Remove(bp, indexOf(need));
                }
        }
        snprintf(line, sizeof(line), "gustall place iceSpike ready %d", (int)BackpackLogic::IsTriggerReady(bp, indexOf(ItemID::IceSpike)));
        AutoTestLog(line);
        Spawn(0.0f, 14.0f, Swarm::kEnemyKindBoss, 1.0e6f);
        Spawn(90.0f, 5.0f, Swarm::kEnemyKindMob);
        m_PhaseDur = 7.0f;
    }
    bp.dirty = true;
    m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
    snprintf(line, sizeof(line), "gustall phase %d %s", m_Phase, m_Name.c_str());
    AutoTestLog(line);
}

void AutoTestGustAll::LogCast(const WeaponSystem::GustDebug& g)
{
    ++m_Casts;
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    // 本当に一番近い敵（直線距離。優先度なし）
    float nearAng = 0.0f, nearDist = 1e9f;
    for (const auto& s : m_Spawned)
        if (s.dist < nearDist) { nearDist = s.dist; nearAng = s.angle; }
    const Vector3 toT = g.target - g.caster;
    const float tAng = AngleOf(toT.x, toT.z), tDist = std::sqrt(toT.x * toT.x + toT.z * toT.z);
    const float bAng = AngleOf(g.blowDir.x, g.blowDir.z);
    // 見た目：粒子の発射の向き（円錐の entry の direction）を読む
    float vfxAng = -999.0f;
    if (VFXEffect* fx = m_AreaVFX.DebugEffect(g.vfxHandle))
    {
        for (int i = 0; i < fx->GetEntryCount(); ++i)
        {
            VFXEntry* e = fx->GetEntry(i);
            if (!e || e->GetType() != EntryType::Particle) continue;
            const auto& d = static_cast<VFXParticleEntry*>(e)->emitterData;
            if (d.emitType != EmitType::Cone) continue;
            vfxAng = AngleOf(d.direction.x, d.direction.z);
            break;
        }
    }
    const Vector3 fromP = g.caster - tf.position;
    char line[512];
    snprintf(line, sizeof(line),
        "gustall cast %s t %.2f orb %d blows %d src %s target %.1fm @%.0f blow @%.0f (err vs target %.0f, vs nearest %.0f @%.0f) "
        "vfx @%.0f (err %.0f) yaw %.0f scale %.2f radius %.2f casterOff %.2f",
        m_Name.c_str(), m_AutoTime - m_PhaseStart, g.fromOrb ? 1 : 0, g.blows ? 1 : 0, g.targetIsGpu ? "gpu" : "cpu",
        tDist, tAng, bAng, g.blows ? AngleDiff(bAng, tAng) : -1.0f, g.blows ? AngleDiff(bAng, nearAng) : -1.0f, nearAng,
        vfxAng, vfxAng > -900.0f ? AngleDiff(vfxAng, bAng) : -1.0f, g.vfxYawDeg, g.vfxScale, g.radius,
        std::sqrt(fromP.x * fromP.x + fromP.z * fromP.z));
    AutoTestLog(line);
    if (m_WindCheckAt < 0.0f) { m_WindCheckAt = m_AutoTime + 0.15f; m_WindCaster = g.caster; }
}

void AutoTestGustAll::TrackCape()
{
    if (!m_Registry.Has<ClothChainComponent>(m_Player) || !m_Registry.Has<SkinnedAnimComponent>(m_Player)) return;
    const auto& c = m_Registry.Get<ClothChainComponent>(m_Player);
    const auto& a = m_Registry.Get<SkinnedAnimComponent>(m_Player);
    if (c.pos.empty() || c.rows <= 0) return;
    const Vector3 root = c.CenterRoot();
    float far2 = 0.0f;
    for (const auto& p : c.pos)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) m_CapeBad = true;
        far2 = (std::max)(far2, (p - root).LengthSquared());
    }
    // 骨の書き戻し（モデル空間）：原点からの距離と、行列の軸の長さ（拡大縮小）
    float boneFar = 0.0f;
    for (const auto& o : a.boneOverrides)
    {
        const Vector3 t = o.second.Translation();
        if (!std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) m_CapeBad = true;
        boneFar = (std::max)(boneFar, t.Length());
        const float sx = Vector3(o.second._11, o.second._12, o.second._13).Length();
        if (sx > 3.0f || sx < 0.3f) m_CapeBad = true;
    }
    const float nodeFar = std::sqrt(far2);
    if (m_CapeBaseFar < 0.0f) { m_CapeBaseFar = nodeFar; m_CapeBaseBone = boneFar; }
    m_CapeMaxFar = (std::max)(m_CapeMaxFar, nodeFar);
    m_CapeMaxBone = (std::max)(m_CapeMaxBone, boneFar);
}

void AutoTestGustAll::EndPhase()
{
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    std::vector<Swarm::Enemy> en;
    std::vector<uint32_t> st;
    std::vector<Swarm::EnemyExtra> ex;
    std::string each;
    if (m_Swarm.DebugReadEnemies(en, st, &ex))
    {
        for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
        {
            if (st[i] == Swarm::kStateDead) continue;
            const float dx = en[i].position.x - tf.position.x, dz = en[i].position.z - tf.position.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            // 生成した時の距離と比べる（一番近い角度の物）
            const float ang = AngleOf(dx, dz);
            float start = 0.0f, best = 1e9f;
            for (const auto& s : m_Spawned)
            {
                const float df = AngleDiff(ang, s.angle);
                if (df < best) { best = df; start = s.dist; }
            }
            char one[64];
            snprintf(one, sizeof(one), " k%u@%.0f:%.1f->%.1f", ex[i].kind, ang, start, d);
            each += one;
        }
    }
    char line[700];
    snprintf(line, sizeof(line), "gustall result %s casts %d twisters %d enemies%s", m_Name.c_str(), m_Casts,
        m_WeaponSystem.GetTwisterCount(), each.c_str());
    AutoTestLog(line);
    snprintf(line, sizeof(line), "gustall cape %s nodeFar base %.2f max %.2f boneFar base %.1f max %.1f bad %d",
        m_Name.c_str(), m_CapeBaseFar, m_CapeMaxFar, m_CapeBaseBone, m_CapeMaxBone, m_CapeBad ? 1 : 0);
    AutoTestLog(line);
}

void AutoTestGustAll::Run()
{
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(50.0f);
        cam.distance = 13.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        AutoTestLog("gustall start");
        m_AutoStep = 1;
        m_Phase = 0;
        BeginPhase();
    }
    if (m_AutoStep != 1) return;

    TrackCape();
    if (!m_Unpaused && m_AutoTime >= m_PhaseStart + 0.5f)
    {
        m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        m_Unpaused = true;
    }
    for (const auto& g : m_WeaponSystem.GetGustDebug()) LogCast(g);
    for (const auto& fx : m_WeaponSystem.GetTriggeredFx())   // アイススパイクの線の向き（2026-10-27）
    {
        const Vector3 d = fx.end - fx.start;
        char tl[160];
        snprintf(tl, sizeof(tl), "gustall line %s t %.2f dir @%.0f len %.1f", m_Name.c_str(), m_AutoTime - m_PhaseStart, AngleOf(d.x, d.z), std::sqrt(d.x * d.x + d.z * d.z));
        AutoTestLog(tl);
    }

    if (m_WindCheckAt >= 0.0f && m_AutoTime >= m_WindCheckAt)
    {
        m_WindCheckAt = 1e9f;   // 場面ごとに 1 回
        std::string s;
        for (const auto& w : m_Swarm.GetLocalWind().sources)
        {
            if (w.radius <= 0.0f) continue;
            char one[128];
            const float dx = w.pos[0] - m_WindCaster.x, dz = w.pos[2] - m_WindCaster.z;
            snprintf(one, sizeof(one), " [r %.2f push %.1f @%.0f fade %.2f offset %.2f]", w.radius, w.push,
                w.push > 0.0f ? AngleOf(w.pushDir[0], w.pushDir[1]) : 0.0f, w.fade, std::sqrt(dx * dx + dz * dz));
            s += one;
        }
        char line[600];
        snprintf(line, sizeof(line), "gustall wind %s sources%s", m_Name.c_str(), s.c_str());
        AutoTestLog(line);
    }

    // 画を数枚（最初の 1 発の 0.25 秒後）
    if (!m_LookedThisPhase && m_Casts > 0 && m_WindCheckAt > 1e8f &&
        (m_Phase == 0 || m_Phase == 2 || m_Phase == 5 || m_Phase >= 8))
    {
        m_LookedThisPhase = true;
        char line[96];
        snprintf(line, sizeof(line), "gustall look %s", m_Name.c_str());
        AutoTestLog(line);
    }

    if (m_AutoTime >= m_PhaseStart + m_PhaseDur)
    {
        EndPhase();
        ++m_Phase;
        if (m_Phase > 17) { AutoTestLog("gustall done"); m_AutoStep = 2; return; }
        BeginPhase();
    }
}

REGISTER_BATTLE_AUTOTEST("gustall", AutoTestGustAll)
