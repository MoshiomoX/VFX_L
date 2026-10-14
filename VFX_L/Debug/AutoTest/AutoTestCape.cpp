// ============================================================
// AutoTestCape.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=cape
// VFXL_BATTLE_AUTOTEST=cape：横から見たマント（立つ → 走る → 急停止 → 逆へ走る → 滑る → 跳ぶ）。
// 0.1 秒毎に裾の位置（体の後ろへ何 m、根から何 m 下）を記録し、要所で "cape look <名>"（2026-10-04）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestCape final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: マントの揺れ（VFXL_BATTLE_AUTOTEST=cape、2026-10-04）
// カメラは固定の向きで、プレイヤーはカメラの右 / 左へ走る → いつも横顔が映る。
// 1 秒: 湧き停止・全消去・無敵・詠唱停止・カメラ 5.5m / 16°。1.5〜2.5 立つ、2.5〜4.5 右へ走る、4.5 急停止、
// 6〜7.5 左へ走る、7.5〜9.5 右へ滑る、9.6 跳ぶ、11 done。0.1 秒毎に "cape t move speed back drop"
// （back = 裾が根より体の後ろへ何 m、drop = 根から何 m 下）、要所で "cape look <名>"
// ============================================================
void AutoTestCape::Run(float dt)
{
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    const float t = m_AutoTime;
    auto at = [&](float s) { return t >= s && t - dt < s; };
    auto look = [&](const char* name) { char b[64]; snprintf(b, sizeof(b), "cape look %s", name); AutoTestLog(b); };

    if (at(1.0f))
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        cam.distance = 5.5f;
        cam.SetPitch(16.0f);
        AutoTestLog("cape start");
    }
    if (m_Registry.Has<LevelComponent>(m_Player)) m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (t < 1.0f) return;

    Vector2 move(0.0f, 0.0f);
    bool slide = false;
    const char* phase = "idle";
    if (t >= 2.5f && t < 4.5f) { move = Vector2(1.0f, 0.0f); phase = "run"; }
    else if (t >= 4.5f && t < 6.0f) { phase = "stop"; }
    else if (t >= 6.0f && t < 7.5f) { move = Vector2(-1.0f, 0.0f); phase = "runLeft"; }
    else if (t >= 7.5f && t < 9.5f) { move = Vector2(1.0f, 0.0f); slide = true; phase = "slide"; }
    else if (t >= 9.5f) { phase = "jump"; }
    pcs.testInput = true;
    pcs.testMove = move;
    pcs.testSlide = slide;
    pcs.testJump = (t >= 9.6f && t < 9.7f);

    if (at(2.4f)) look("idle");
    if (at(3.6f)) look("run");
    if (at(4.3f)) look("run2");
    if (at(4.62f)) look("stop1");
    if (at(4.85f)) look("stop2");
    if (at(5.3f)) look("stop3");
    if (at(7.1f)) look("runLeft");
    if (at(8.2f)) look("slide");
    if (at(9.0f)) look("slide2");
    if (at(9.85f)) look("jump");
    if (at(10.2f)) look("fall");

    // 0.1 秒毎に裾の位置
    static float s_Next = 0.0f;
    if (t >= s_Next && m_Registry.Has<ClothChainComponent>(m_Player))
    {
        s_Next = t + 0.1f;
        const auto& c = m_Registry.Get<ClothChainComponent>(m_Player);
        const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
        if (c.pos.size() >= 2)
        {
            const float yaw = DirectX::XMConvertToRadians(tf.rotation.y);
            const Vector3 fwd(std::sin(yaw), 0.0f, std::cos(yaw));
            const Vector3 d = c.pos.back() - c.pos.front();
            char line[200];
            snprintf(line, sizeof(line), "cape t %.2f %s speed %.2f back %.2f drop %.2f",
                t, phase, std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z), -d.Dot(fwd), -d.y);
            AutoTestLog(line);
        }
    }

    if (at(11.0f))
    {
        pcs.testInput = false;
        AutoTestLog("cape done");
    }
}

REGISTER_BATTLE_AUTOTEST("cape", AutoTestCape)
