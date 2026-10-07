// ============================================================
// AutoTestPerch.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=perch（2026-10-07）
// 箱の上に乗ると魔法が出ない（ユーザー報告）の確認。
//   箱・岩の足元のマスは雑魚用に塞いである → 銃口がそのマスの中 → GPU の弾が最初の 1 歩で「壁に入った」と消えていた。
//   直した後：塞がったマスで生まれた弾は、そこを出るまで飛び続ける（壁へは空いたマスから入るので今まで通り止まる）。
// A 段：一番近い箱の上に立ち、9m 先の動かない的 3 体へ黄金の矢（直進）→ 的の HP が減るはず
// B 段：地面に降り、箱を挟んで同じ的を撃つ → 矢は箱のマスで止まり、HP はほとんど減らないはず
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestPerch final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float TargetHp();                       // 生きている敵の HP の合計（的だけ）
        void PlacePlayer(const Vector3& pos);   // 置いて速度を消す
    };

    Vector3 s_Crate;      // 箱の足元（InteractableComponent::basePos）
    Vector3 s_Dir;        // 箱 → 的 の向き（水平・単位）
    float   s_Hp[4] = {};
}

float AutoTestPerch::TargetHp()
{
    std::vector<Swarm::Enemy> enemies;
    std::vector<uint32_t> states;
    if (!m_Swarm.DebugReadEnemies(enemies, states)) return -1.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < enemies.size() && i < states.size(); ++i)
        if (states[i] != Swarm::kStateDead) sum += Swarm::HpFromFixed(enemies[i].hp);
    return sum;
}

void AutoTestPerch::PlacePlayer(const Vector3& pos)
{
    m_Registry.Get<TransformComponent>(m_Player).position = pos;
    if (m_Registry.Has<RigidbodyComponent>(m_Player))
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
}

void AutoTestPerch::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;   // 三択で止めない
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& cam = m_Camera.Camera();
    char line[200];

    auto logPlayer = [&](const char* tag)
    {
        const Vector3 p = m_Registry.Get<TransformComponent>(m_Player).position;
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        snprintf(line, sizeof(line), "perch %s player (%.1f, %.2f, %.1f) ground %.2f cellWalkable %d grounded %d proj %u",
            tag, p.x, p.y, p.z, m_Grid.SampleHeight(p.x, p.z), m_Grid.IsWalkableAt(p) ? 1 : 0, grounded ? 1 : 0,
            m_Swarm.GetCounters().aliveProjectiles);
        AutoTestLog(line);
    };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();

        // 一番近い箱
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        float best = 1.0e9f;
        Entity crate = EntityTraits::NULL_ENTITY;
        for (Entity e : m_Crates.GetCrates())
        {
            const Vector3 cp = m_Registry.Get<InteractableComponent>(e).basePos;
            const float d = (cp - pp).LengthSquared();
            if (d < best) { best = d; crate = e; }
        }
        if (!m_Registry.IsValid(crate)) { AutoTestLog("perch no crate"); AutoTestLog("perch done"); m_AutoStep = 99; return; }
        s_Crate = m_Registry.Get<InteractableComponent>(crate).basePos;

        // 的（9m 先）と B 段の立ち位置（箱の反対側 2.5m）が歩けて平らな向き。場の中央向きから 45 度ずつ回して探す
        Vector3 toCenter = -s_Crate; toCenter.y = 0.0f;
        if (toCenter.LengthSquared() < 1e-4f) toCenter = Vector3(1, 0, 0);
        toCenter.Normalize();
        const float base = std::atan2(toCenter.z, toCenter.x);
        bool found = false;
        for (int k = 0; k < 8 && !found; ++k)
        {
            const float a = base + k * DirectX::XM_PIDIV4;
            const Vector3 d(std::cos(a), 0.0f, std::sin(a));
            const Vector3 side(-d.z, 0.0f, d.x);
            bool ok = m_Grid.IsWalkableAt(s_Crate - d * 2.5f);
            for (float s : { -1.5f, 0.0f, 1.5f })
                ok = ok && m_Grid.IsWalkableAt(s_Crate + d * 9.0f + side * s);
            const float h0 = m_Grid.SampleHeight(s_Crate.x, s_Crate.z);
            const Vector3 t = s_Crate + d * 9.0f;
            ok = ok && std::fabs(m_Grid.SampleHeight(t.x, t.z) - h0) < 1.0f;
            if (ok) { s_Dir = d; found = true; }
        }
        if (!found) { AutoTestLog("perch no clear direction"); AutoTestLog("perch done"); m_AutoStep = 99; return; }

        const float gy = m_Swarm.GetAIParams().groundY;
        const Vector3 side(-s_Dir.z, 0.0f, s_Dir.x);
        for (float s : { -1.5f, 0.0f, 1.5f })
        {
            const Vector3 t = s_Crate + s_Dir * 9.0f + side * s;
            m_Swarm.SpawnEnemy(Vector3(t.x, m_Grid.SampleHeight(t.x, t.z) + gy, t.z), 100000.0f, 0.0f);
        }

        // 黄金の矢だけ（直進なので「箱で止まる / 止まらない」がはっきり出る）
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            // 黄金の矢は縦 3 マス（10-04）。3x3 の枠の一番上の行から置く
            const int idx = BackpackLogic::Place(bp, ItemID::GoldenArrow, lo, lo + 1, 0);
            snprintf(line, sizeof(line), "perch arrow placed index %d", idx);
            AutoTestLog(line);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }

        // A 段：箱の真上から落として乗せる
        PlacePlayer(s_Crate + Vector3(0.0f, 2.5f, 0.0f));
        cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(-s_Dir.x, s_Dir.z)));
        cam.distance = 12.0f;
        cam.SetPitch(30.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        snprintf(line, sizeof(line), "perch crate (%.1f, %.2f, %.1f) cellWalkable %d dir (%.2f, %.2f)",
            s_Crate.x, s_Crate.y, s_Crate.z, m_Grid.IsWalkableAt(s_Crate) ? 1 : 0, s_Dir.x, s_Dir.z);
        AutoTestLog(line);
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f)
    {
        s_Hp[0] = TargetHp();
        logPlayer("A start");
        AutoTestLog("perch look A");
        m_AutoStep = 2;
    }
    else if (m_AutoStep == 2 && m_AutoTime >= 6.0f)
    {
        s_Hp[1] = TargetHp();
        logPlayer("A end");
        snprintf(line, sizeof(line), "perch A on crate: target hp %.0f -> %.0f (damage %.0f in 4 s)", s_Hp[0], s_Hp[1], s_Hp[0] - s_Hp[1]);
        AutoTestLog(line);
        // B 段：地面、箱を挟んで的の反対側
        const Vector3 b = s_Crate - s_Dir * 2.5f;
        PlacePlayer(Vector3(b.x, m_Grid.SampleHeight(b.x, b.z) + 1.0f, b.z));
        cam.SnapToTarget();
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 7.0f)
    {
        s_Hp[2] = TargetHp();
        logPlayer("B start");
        AutoTestLog("perch look B");
        m_AutoStep = 4;
    }
    else if (m_AutoStep == 4 && m_AutoTime >= 11.0f)
    {
        s_Hp[3] = TargetHp();
        logPlayer("B end");
        snprintf(line, sizeof(line), "perch B behind crate: target hp %.0f -> %.0f (damage %.0f in 4 s)", s_Hp[2], s_Hp[3], s_Hp[2] - s_Hp[3]);
        AutoTestLog(line);
        AutoTestLog("perch done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("perch", AutoTestPerch)
