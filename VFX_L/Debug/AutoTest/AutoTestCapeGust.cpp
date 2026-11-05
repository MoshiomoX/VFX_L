// ============================================================
// AutoTestCapeGust.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=capegust（2026-11-05）
// 自分の突風でマントが体の中・前へ押し込まれないか（ユーザーの画像：突風の輪の中でマントが見えない）。
// プレイヤーは +Z を向いて立ち、正面 4.5m に歩いてくる的 3 体（吹き飛ばされても戻ってくる）、バックパックは突風だけ。カメラは横から。
// 0.05 秒毎に `capegust t casts back drop up`（back = 裾が根より体の後ろへ何 m、drop = 根から何 m 下、
// up = 裾の一番前の節点が根より前へ出た m）。突風の 0.12 / 0.3 秒後に `capegust look c<n>a / c<n>b`、
// 終わりに `capegust result casts minBack maxFront`（minBack < 0 = 裾が体の前へ回った）。
// VFXL_CAPE_OWN_GUST=1 で前の作り（自分の突風もマントが受ける）と比べられる
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "ECS/System/WeaponSystem.h"

namespace
{
    class AutoTestCapeGust final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
        int   m_Casts = 0;
        float m_LastCast = -10.0f;
        float m_NextLog = 0.0f;
        float m_MinBack = 1e9f;
        float m_MaxFront = -1e9f;
        float m_PrevHeight = 1.5f;
    };
}

void AutoTestCapeGust::Run(float dt)
{
    auto& cam = m_Camera.Camera();
    const float t = m_AutoTime;
    auto at = [&](float s) { return t >= s && t - dt < s; };
    char line[256];

    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }

    if (m_AutoStep == 0 && t >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        tf.rotation.y = 0.0f;   // +Z を向く（突風は +Z へ吹く = 背中側から前へ）
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ClearBackpackItems(bp);
        const int lo = BackpackComponent::GRID / 2 - 1;
        if (BackpackLogic::Place(bp, ItemID::Gust, lo, lo, 0) < 0) AutoTestLog("capegust place FAILED");
        bp.dirty = true;
        const Vector3 o = tf.position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int i = -1; i <= 1; ++i)
        {
            const float x = o.x + i * 1.2f, z = o.z + 4.5f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 1.0e5f, 2.5f);
        }
        cam.SetYaw(-90.0f);   // 横から（体の前後が画面の左右）
        cam.SetPitch(12.0f);
        cam.distance = 5.0f;
        cam.avoidOcclusion = false;
        m_PrevHeight = cam.height;
        cam.height = 0.35f;
        cam.SnapToTarget();
        m_GameUI.hideAll = true;
        AutoTestLog("capegust start");
        m_AutoStep = 1;
    }
    if (m_AutoStep != 1) return;
    m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;

    if (!m_WeaponSystem.GetGustFx().empty())
    {
        ++m_Casts;
        m_LastCast = t;
        snprintf(line, sizeof(line), "capegust cast %d t %.2f", m_Casts, t);
        AutoTestLog(line);
    }
    if (m_Casts >= 1 && m_Casts <= 3)
    {
        const float since = t - m_LastCast;
        if (since >= 0.12f && since - dt < 0.12f) { snprintf(line, sizeof(line), "capegust look c%da", m_Casts); AutoTestLog(line); }
        if (since >= 0.30f && since - dt < 0.30f) { snprintf(line, sizeof(line), "capegust look c%db", m_Casts); AutoTestLog(line); }
    }
    if (at(1.6f)) AutoTestLog("capegust look idle");

    if (t >= m_NextLog && m_Registry.Has<ClothChainComponent>(m_Player))
    {
        m_NextLog = t + 0.05f;
        const auto& c = m_Registry.Get<ClothChainComponent>(m_Player);
        const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        if (c.ready && (int)c.pos.size() == c.cols * c.rows)
        {
            const float yaw = DirectX::XMConvertToRadians(tf.rotation.y);
            const Vector3 fwd(std::sin(yaw), 0.0f, std::cos(yaw));
            const Vector3 root = c.CenterRoot();
            const Vector3 d = c.CenterTip() - root;
            float front = -1e9f;
            for (const Vector3& p : c.pos) front = (std::max)(front, (p - root).Dot(fwd));
            const float back = -d.Dot(fwd);
            if (t > 1.5f)
            {
                m_MinBack = (std::min)(m_MinBack, back);
                m_MaxFront = (std::max)(m_MaxFront, front);
            }
            snprintf(line, sizeof(line), "capegust t %.2f casts %d back %.2f drop %.2f front %.2f", t, m_Casts, back, -d.y, front);
            AutoTestLog(line);
        }
    }

    if (t >= 7.5f)
    {
        snprintf(line, sizeof(line), "capegust result casts %d minBack %.2f maxFront %.2f", m_Casts, m_MinBack, m_MaxFront);
        AutoTestLog(line);
        m_PlayerControlSystem.testInput = false;
        cam.height = m_PrevHeight;
        m_GameUI.hideAll = false;
        AutoTestLog("capegust done");
        m_AutoStep = 2;
    }
}

REGISTER_BATTLE_AUTOTEST("capegust", AutoTestCapeGust)
