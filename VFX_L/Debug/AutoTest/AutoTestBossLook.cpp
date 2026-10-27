// ============================================================
// AutoTestBossLook.cpp
// TEMP-TEST: VFXL_STAGE=2 + VFXL_BATTLE_AUTOTEST=bosslook（砂漠の Boss「砂の守護者」の見た目。2026-10-24）
// 歩く Boss を呼び（プレイヤーは 30m 先で止まっている = Boss はまっすぐこちらへ歩く）、カメラを Boss に向けて
// 正面 / 斜め前 / 横 / 後ろ / いつもの見下ろし を撮る（歩きの姿勢で部品の飾りが付いて来るか）→ 砂の槍を出させる。
// 部品の名前・寸法・付け根を記録する
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Player/PlayerControlSystem.h"

namespace
{
    class AutoTestBossLook final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    // 撮る角度（Boss の正面 = Boss → プレイヤーの向きから回す度・俯角・距離・注視点の高さ）
    struct Shot { float yaw, pitch, dist, height; const char* name; };
    const Shot kShots[] = {
        { 0.0f, 12.0f, 11.0f, 3.0f, "front" },
        { 40.0f, 18.0f, 11.0f, 3.0f, "frontside" },
        { 90.0f, 10.0f, 11.0f, 3.0f, "side" },
        { 90.0f, 6.0f, 12.0f, 2.2f, "side2" },   // 歩きの別の瞬間（脚と腕の振り、2026-10-27）
        { 90.0f, 6.0f, 12.0f, 2.2f, "side3" },
        { 180.0f, 15.0f, 11.0f, 3.0f, "back" },
        { 25.0f, 45.0f, 16.0f, 1.5f, "game" },   // いつもの見下ろしに近い
    };
    constexpr int kShotCount = (int)(sizeof(kShots) / sizeof(kShots[0]));
    constexpr float kShotTime = 1.2f;
}

void AutoTestBossLook::Run()
{
    static float s_Base = 0.0f;
    static int s_Shot = -1;
    static bool s_A = false, s_B = false;
    char line[256];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_GameUI.hideAll = true;   // HUD が脚を隠すので（2026-10-27）
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_BossAttacks.enabled = false;
        m_Stage.bossHp = 1.0e6f;
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        hp.max = hp.current = 100000.0f;
        const bool ok = m_Stage.RequestBoss(true);
        snprintf(line, sizeof(line), "bosslook start requested %d type %s", ok ? 1 : 0, BossTypeName(m_BossAttacks.Type()));
        AutoTestLog(line);
        if (auto model = m_Swarm.GetEnemyModel())
        {
            const auto& subs = model->GetSubMeshes();
            for (size_t i = 0; i < subs.size(); ++i)
            {
                const Vector3 pv = m_Swarm.GetPartPivot((uint32_t)i);
                snprintf(line, sizeof(line), "bosslook part %zu '%s' lo %.2f,%.2f,%.2f hi %.2f,%.2f,%.2f pivot %.2f,%.2f,%.2f", i, subs[i].nodeName.c_str(),
                    subs[i].boundsMin.x, subs[i].boundsMin.y, subs[i].boundsMin.z, subs[i].boundsMax.x, subs[i].boundsMax.y, subs[i].boundsMax.z, pv.x, pv.y, pv.z);
                AutoTestLog(line);
            }
        }
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        m_AutoStep = 1;
        return;
    }
    if (m_AutoStep == 1 && m_Stage.IsBossAlive() && m_Stage.BossPos().LengthSquared() > 0.01f)
    {
        // プレイヤーを Boss から 30m（今居る方向）。止まっているので Boss はまっすぐこちらへ歩いて来る
        const Vector3 boss = m_Stage.BossPos();
        Vector3 away = tf.position - boss;
        away.y = 0.0f;
        if (away.LengthSquared() < 1e-2f) away = Vector3(0.0f, 0.0f, 1.0f);
        away.Normalize();
        const Vector3 p = boss + away * 30.0f;
        tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
        m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
        m_AutoStep = 2;
        s_Shot = -1;
        s_Base = m_AutoTime;
        snprintf(line, sizeof(line), "bosslook boss at %.1f,%.1f,%.1f", boss.x, boss.y, boss.z);
        AutoTestLog(line);
        return;
    }
    if (m_AutoStep < 2) return;

    const float t = m_AutoTime - s_Base;
    const Vector3 boss = m_Stage.BossPos();
    const Vector3 target(boss.x, m_Grid.SampleHeight(boss.x, boss.z), boss.z);
    Vector2 f(tf.position.x - boss.x, tf.position.z - boss.z);   // Boss の正面（プレイヤーの方へ歩いている）
    if (f.LengthSquared() < 1e-3f) f = Vector2(0.0f, 1.0f);
    f.Normalize();
    // Boss から見て正面を deg 回した方にカメラを置き、Boss を見る（カメラの前 = (-sin yaw, cos yaw)）
    auto aim = [&](float deg, float pitch, float dist, float height)
        {
            const float a = DirectX::XMConvertToRadians(deg);
            const Vector2 u(f.x * std::cos(a) - f.y * std::sin(a), f.x * std::sin(a) + f.y * std::cos(a));
            m_Camera.SetTargetOverride(&target);
            cam.SetYaw(DirectX::XMConvertToDegrees(std::atan2(u.x, -u.y)));
            cam.SetPitch(pitch);
            cam.distance = dist;
            cam.height = height;
            cam.avoidOcclusion = false;
            cam.SnapToTarget();
        };

    // ---- 角度を変えて撮る（1 枚 kShotTime 秒。0.8 秒で撮る）----
    if (m_AutoStep == 2)
    {
        const int idx = (int)(t / kShotTime);
        if (idx >= kShotCount)
        {
            m_AutoStep = 3;
            s_Base = m_AutoTime;
            // 砂の守護者 = 砂の槍、岩の巨人（草原）= 重撃
            // 雄鹿 = 突進（頭を下げて走る）
            m_BossAttacks.QueueMove(m_BossAttacks.Type() == BossType::SandGuardian ? BossMove::SandSpear
                : m_BossAttacks.Type() == BossType::Stag ? BossMove::Charge
                : m_BossAttacks.Type() == BossType::Scorpion ? BossMove::Sting
                : m_BossAttacks.Type() == BossType::CryptKnight ? BossMove::ChainPull
                : m_BossAttacks.Type() == BossType::Werewolf ? BossMove::Pounce : BossMove::Slam);
            snprintf(line, sizeof(line), "bosslook spear dist %.1f", Vector2(tf.position.x - boss.x, tf.position.z - boss.z).Length());
            AutoTestLog(line);
            return;
        }
        const Shot& sh = kShots[idx];
        aim(sh.yaw, sh.pitch, sh.dist, sh.height);
        if (idx != s_Shot && t - idx * kShotTime >= 0.8f)
        {
            s_Shot = idx;
            snprintf(line, sizeof(line), "bosslook look %s", sh.name);
            AutoTestLog(line);
        }
        return;
    }

    // ---- 雷鳴の雄鹿の突進（2026-10-26）：真横から 溜め（頭を下げ脚を開く）→ 走り（前傾・駆け足）→ 道筋の雷 ----
    // 大サソリの毒針も同じ 3 枚（溜め = 尾が反る 0.5 秒 / 突っ込み = 尾を振り下ろす 0.95 秒 / 休み 1.5 秒）
    const bool scorp = (m_BossAttacks.Type() == BossType::Scorpion);
    if (m_AutoStep == 3 && (m_BossAttacks.Type() == BossType::Stag || scorp))
    {
        static bool s_C = false;
        aim(95.0f, 12.0f, 15.0f, 1.3f);
        if (!s_A && t >= (scorp ? 0.5f : 0.9f)) { s_A = true; AutoTestLog(scorp ? "bosslook look stingwind" : "bosslook look chargewind"); }
        if (!s_B && t >= (scorp ? 0.95f : 1.55f)) { s_B = true; AutoTestLog(scorp ? "bosslook look stingdash" : "bosslook look chargedash"); }
        if (!s_C && t >= (scorp ? 1.5f : 2.1f)) { s_C = true; AutoTestLog(scorp ? "bosslook look stingrest" : "bosslook look chargethunder"); }
        if (t >= 3.4f)
        {
            m_Camera.SetTargetOverride(nullptr);
            pcs.testInput = false;
            snprintf(line, sizeof(line), "bosslook charge hits %u", m_BossSlamHits);
            AutoTestLog(line);
            m_GameUI.hideAll = false;
            AutoTestLog("bosslook done");
            m_AutoStep = 4;
        }
        return;
    }
    // ---- 砂の槍（溜め・突き出し）----
    if (m_AutoStep == 3)
    {
        aim(60.0f, 22.0f, 16.0f, 2.0f);
        if (!s_A && t >= 0.5f) { s_A = true; AutoTestLog("bosslook look spearwind"); }
        if (!s_B && t >= 1.3f) { s_B = true; AutoTestLog("bosslook look spearfire"); }
        if (t >= 2.5f)
        {
            m_Camera.SetTargetOverride(nullptr);
            pcs.testInput = false;
            m_GameUI.hideAll = false;
            AutoTestLog("bosslook done");
            m_AutoStep = 4;
        }
    }
}

REGISTER_BATTLE_AUTOTEST("bosslook", AutoTestBossLook)
