// ============================================================
// AutoTestVortex.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=vortex
// 渦（基本魔法、2026-10-22）：渦が周りの敵を一か所へ集めるか（引き寄せ = AreaProfile::pull）と見た目。
//   1 秒: 湧き停止・全消し・無敵・MP 無限・入力 0。バックパックに渦だけ（詠唱は止めておく）。
//         正面 +Z 7m に餌の雑魚 1 体（一番近い = 弾が当たる = 渦の中心）、その奥の半円（半径 2.5m）に雑魚 10 体 + エリート 1 体。
//         全部動かない、HP 10 万。カメラは後ろ 13m / 40°。
//   1.6 秒: 詠唱を 0.4 秒だけ許す（1 発だけ撃つ）。以後 0.25 秒ごとに回読して
//         `vortex t casts areas spread maxR eliteR lost`（spread = 雑魚の渦の中心（餌の初期位置）からの平均距離、
//         maxR = 一番遠い雑魚、eliteR = エリートの距離、lost = 雑魚の HP の減り合計）。
//   撃ってから 0.5 / 1.2 / 1.8 / 2.6 秒で `vortex look fly / open / mid / late`。7 秒で `vortex result` + done。
// 期待：渦が開いた後、雑魚の spread が約 2.3m（餌 0 + 10 体 2.5m）→ 1m 前後に縮む、エリートは雑魚より縮み方が小さい
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestVortex final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_NextRead = 1.5f;
        float m_FirstCast = -1.0f;
        int   m_Casts = 0;
        float m_Spread0 = -1.0f;    // 撃つ前の spread
        float m_SpreadMin = 1.0e9f; // 一番縮んだ時
        float m_Elite0 = -1.0f;
        float m_EliteMin = 1.0e9f;
        float m_Lost = 0.0f;
        Vector3 m_Center;           // 餌の位置 = 渦が開く所
        Vector3 m_Far;              // 一番遠い雑魚（引き寄せが届かなかった物を調べる）
        float m_FarSpeed = 0.0f;
    };
}

void AutoTestVortex::Run()
{
    char line[400];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        int placed = -1;
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            placed = BackpackLogic::Place(bp, ItemID::VoidVortex, lo, lo, 0);   // 逆 L 字 3 マス
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;      // 標的のリードバックを待ってから 1 発だけ撃つ
        }
        // 餌（一番近い 1 体）を正面 7m に置き、残りはその奥の半円（半径 2.5m）に並べる：弾は餌に当たり、渦は餌の所で開く
        const float groundY = m_Swarm.GetAIParams().groundY;
        m_Center = tf.position + Vector3(0.0f, 0.0f, 7.0f);
        m_Swarm.SpawnEnemy(Vector3(m_Center.x, m_Grid.SampleHeight(m_Center.x, m_Center.z) + groundY, m_Center.z), 100000.0f, 0.0f, Swarm::kEnemyKindMob);
        for (int k = 0; k < 11; ++k)
        {
            const float ang = (-80.0f + 16.0f * (float)k) * 0.0174533f;
            const float x = m_Center.x + std::sin(ang) * 2.5f, z = m_Center.z + std::cos(ang) * 2.5f;
            const uint32_t kind = (k == 5) ? Swarm::kEnemyKindElite : Swarm::kEnemyKindMob;   // エリートは真奥
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 100000.0f, 0.0f, kind);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(40.0f);
        cam.distance = 13.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "vortex start placed %d", placed);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    // 1 発だけ撃つ（発動間隔 1.5 秒なので 0.4 秒開けておけば 1 発）
    if (m_Registry.Has<WandComponent>(m_Player))
    {
        auto& wand = m_Registry.Get<WandComponent>(m_Player);
        if (m_AutoTime >= 1.6f && m_AutoTime < 2.0f) wand.castingPaused = false;
        else if (m_AutoTime >= 2.0f) wand.castingPaused = true;
    }
    for (const auto& fx : m_WeaponSystem.GetCastFx())
        if (fx.id == ItemID::VoidVortex)
        {
            ++m_Casts;
            if (m_FirstCast < 0.0f) m_FirstCast = m_AutoTime;
        }

    if (m_AutoTime >= m_NextRead)
    {
        m_NextRead += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        std::vector<Swarm::EnemyExtra> ex;
        if (m_Swarm.DebugReadEnemies(en, st, &ex))
        {
            int mobs = 0;
            float hp = 0.0f;
            for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead || ex[i].kind != Swarm::kEnemyKindMob) continue;
                hp += Swarm::HpFromFixed(en[i].hp);
                ++mobs;
            }
            if (mobs > 0)
            {
                const Vector3 mid = m_Center;   // 渦の中心（餌の位置）
                float spread = 0.0f, maxR = 0.0f, eliteR = 0.0f;
                for (size_t i = 0; i < en.size() && i < ex.size(); ++i)
                {
                    if (st[i] == Swarm::kStateDead) continue;
                    const float dx = en[i].position.x - mid.x, dz = en[i].position.z - mid.z;
                    const float d = std::sqrt(dx * dx + dz * dz);
                    if (ex[i].kind == Swarm::kEnemyKindElite) eliteR = d;
                    else if (ex[i].kind == Swarm::kEnemyKindMob)
                    {
                        spread += d;
                        if (d > maxR) { maxR = d; m_Far = en[i].position; m_FarSpeed = en[i].moveSpeed; }
                    }
                }
                spread /= (float)mobs;
                if (m_Spread0 < 0.0f) { m_Spread0 = spread; m_Elite0 = eliteR; }
                m_SpreadMin = (std::min)(m_SpreadMin, spread);
                m_EliteMin = (std::min)(m_EliteMin, eliteR);
                m_Lost = 100000.0f * (float)mobs - hp;
                const auto& cn = m_Swarm.GetCounters();
                snprintf(line, sizeof(line), "vortex t %.2f casts %d areas %u spread %.2f maxR %.2f eliteR %.2f lost %.1f",
                    m_AutoTime, m_Casts, cn.aliveAreas, spread, maxR, eliteR, m_Lost);
                AutoTestLog(line);
            }
        }
    }

    if (m_FirstCast >= 0.0f)
    {
        if (m_AutoStep == 1 && m_AutoTime >= m_FirstCast + 0.5f) { AutoTestLog("vortex look fly"); m_AutoStep = 2; }
        if (m_AutoStep == 2 && m_AutoTime >= m_FirstCast + 1.2f) { AutoTestLog("vortex look open"); m_AutoStep = 3; }
        if (m_AutoStep == 3 && m_AutoTime >= m_FirstCast + 1.8f) { AutoTestLog("vortex look mid"); m_AutoStep = 4; }
        if (m_AutoStep == 4 && m_AutoTime >= m_FirstCast + 2.6f) { AutoTestLog("vortex look late"); m_AutoStep = 5; }
    }
    if (m_AutoStep >= 1 && m_AutoStep <= 5 && m_AutoTime >= 7.0f)
    {
        snprintf(line, sizeof(line), "vortex result casts %d spread %.2f -> %.2f elite %.2f -> %.2f mobLost %.1f",
            m_Casts, m_Spread0, m_SpreadMin, m_Elite0, m_EliteMin, m_Lost);
        AutoTestLog(line);
        int fgx = 0, fgz = 0;
        m_Grid.WorldToCell(m_Far, fgx, fgz);
        snprintf(line, sizeof(line), "vortex far pos %.2f %.2f %.2f centre %.2f %.2f %.2f ground %.2f / %.2f speed %.2f cell %d %d walkable %d",
            m_Far.x, m_Far.y, m_Far.z, m_Center.x, m_Center.y, m_Center.z,
            m_Grid.SampleHeight(m_Far.x, m_Far.z), m_Grid.SampleHeight(m_Center.x, m_Center.z), m_FarSpeed, fgx, fgz,
            m_Grid.IsWalkable(fgx, fgz) ? 1 : 0);
        AutoTestLog(line);
        AutoTestLog("vortex done");
        m_AutoStep = 6;
    }
}

REGISTER_BATTLE_AUTOTEST("vortex", AutoTestVortex)
