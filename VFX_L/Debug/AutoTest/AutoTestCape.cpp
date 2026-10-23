// ============================================================
// AutoTestCape.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=cape
// VFXL_BATTLE_AUTOTEST=cape：横から見たマント（立つ → 走る → 急停止 → 逆へ走る → 滑る → 跳ぶ）。
// 0.1 秒毎に裾の位置（体の後ろへ何 m、根から何 m 下）を記録し、要所で "cape look <名>"（2026-10-04）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "ECS/System/SkinnedAnimSystem.h"

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
// 6〜7.5 左へ走る、7.5〜9.5 右へ滑る、9.6 跳ぶ、10.6〜13 カメラから離れる向きへ走る（背中 = 格子の左右）、13 止まる、14.5 done（2026-10-23）。0.1 秒毎に "cape t move speed back drop"
// （back = 裾が根より体の後ろへ何 m、drop = 根から何 m 下）、要所で "cape look <名>"
// ============================================================
void AutoTestCape::Run(float dt)
{
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();
    const float t = m_AutoTime;
    auto at = [&](float s) { return t >= s && t - dt < s; };
    auto look = [&](const char* name) { char b[64]; snprintf(b, sizeof(b), "cape look %s", name); AutoTestLog(b); };
    static float s_PrevHeight = 1.5f;

    if (at(1.0f))
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        if (m_Registry.Has<HealthComponent>(m_Player)) m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        cam.distance = 4.5f;
        cam.SetPitch(14.0f);
        s_PrevHeight = cam.height;
        cam.height = 0.35f;   // 注視点を体の真ん中へ（既定の 1.5 だと足元の HUD の技能アイコンに体が隠れる。2026-10-23）
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
    else if (t >= 9.5f && t < 10.6f) { phase = "jump"; }
    else if (t >= 10.6f && t < 13.0f) { move = Vector2(0.0f, 1.0f); phase = "runAway"; }   // 背中を見せて走る（格子の左右、2026-10-23）
    else if (t >= 13.0f) { phase = "stopBack"; }
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
    if (at(11.6f)) look("runAway1");
    if (at(12.2f)) look("runAway2");
    if (at(12.7f)) look("runAway3");
    if (at(13.25f)) look("stopBack1");
    if (at(13.9f)) look("stopBack2");

    // 0.1 秒毎に裾の位置
    static float s_Next = 0.0f;
    if (t >= s_Next && m_Registry.Has<ClothChainComponent>(m_Player))
    {
        s_Next = t + 0.1f;
        const auto& c = m_Registry.Get<ClothChainComponent>(m_Player);
        const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
        if (c.ready && (int)c.pos.size() == c.cols * c.rows)
        {
            const float yaw = DirectX::XMConvertToRadians(tf.rotation.y);
            const Vector3 fwd(std::sin(yaw), 0.0f, std::cos(yaw));
            const Vector3 right(fwd.z, 0.0f, -fwd.x);
            const Vector3 d = c.CenterTip() - c.CenterRoot();
            // 格子（2026-10-23）：裾の両端の幅、左右の端の後ろへの出方の差（片側だけ押された / 捻れた）
            const Vector3 tipL = c.pos[c.Node(0, c.rows - 1)], tipR = c.pos[c.Node(c.cols - 1, c.rows - 1)];
            const float hem = (tipR - tipL).Length();
            const float twist = (tipR - tipL).Dot(fwd);
            char line[240];
            snprintf(line, sizeof(line), "cape t %.2f %s speed %.2f back %.2f drop %.2f side %.2f cols %d hem %.2f twist %.2f ms %.3f pose %.3f",
                t, phase, std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z), -d.Dot(fwd), -d.y,
                d.Dot(right), c.cols, hem, twist, c.stepMs, c.poseMs);
            AutoTestLog(line);
        }
    }

    if (at(14.5f))
    {
        pcs.testInput = false;
        cam.height = s_PrevHeight;
        int hits = 0, misses = 0;
        SkinnedAnimSystem::PoseCacheStats(hits, misses);
        char line[120];
        snprintf(line, sizeof(line), "cape poseCache hits %d misses %d", hits, misses);
        AutoTestLog(line);
        AutoTestLog("cape done");
    }
}

REGISTER_BATTLE_AUTOTEST("cape", AutoTestCape)
