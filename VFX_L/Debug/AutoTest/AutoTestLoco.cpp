// ============================================================
// AutoTestLoco.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=loco
// VFXL_BATTLE_AUTOTEST=loco：前 / 右 / 後 / 左 / 左後 / 右前 / 前（ゆっくり）へ走らせて
// 脚の向き・後ろ走り・歩様・再生速度を記録し、続けて既定のカメラで火球と隕石の爆発を映す（画面は外から連写）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestLoco final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// TEMP-TEST: 横 / 後ろ走りと爆発の見え方（VFXL_BATTLE_AUTOTEST=loco）
// 1 秒: 湧き停止・全消し・カメラを寄せる（5m、見下ろし 12 度）
// 2 秒〜: カメラから見て 前 / 右 / 後 / 左 / 左後 / 右前 / 前（ゆっくり）へ 1.6 秒ずつ走る。
//         各段 0.9 秒で "loco <向き>" 行（脚の角度・後ろ走り・歩様・再生速度）→ 外から撮る
// その後: カメラを既定に戻し、正面 8〜9m に動かない的 3 体 + 火球・隕石をバックパックへ。
//         "loco blast" 行を 2 回（外から撮る）→ "loco done"
// ============================================================
void AutoTestLoco::Run(float dt)
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& pcs = m_PlayerControlSystem;
    auto& cam = m_Camera.Camera();

    struct Seg { const char* name; float x, y; };   // カメラ基準の入力（x = 右、y = 前）
    static const Seg kSegs[] = {
        { "forward", 0.0f, 1.0f }, { "right", 1.0f, 0.0f }, { "back", 0.0f, -1.0f }, { "left", -1.0f, 0.0f },
        { "back-left", -0.7071f, -0.7071f }, { "forward-right", 0.7071f, 0.7071f }, { "forward-slow", 0.0f, 0.35f },
    };
    constexpr int kSegCount = (int)(sizeof(kSegs) / sizeof(kSegs[0]));
    constexpr float kSegTime = 1.6f;
    static float s_Phase = 0.0f;
    static bool  s_Logged = false;

    if (m_AutoStep == 1 && m_AutoTime >= 1.7f && m_AutoTime - dt < 1.7f) AutoTestLog("loco look idle");
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        cam.distance = 5.0f;
        cam.SetPitch(12.0f);
        AutoTestLog("loco start");
        m_AutoStep = 1;
        s_Phase = 0.0f;
    }
    else if (m_AutoStep >= 1 && m_AutoStep <= kSegCount && m_AutoTime >= 2.0f)
    {
        const Seg& s = kSegs[m_AutoStep - 1];
        pcs.testInput = true;
        pcs.testMove = Vector2(s.x, s.y);
        pcs.testSlide = false;
        pcs.testJump = false;
        s_Phase += dt;
        if (!s_Logged && s_Phase >= 0.9f)
        {
            s_Logged = true;
            const auto& pa = m_PlayerAnimSystem;
            const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
            // 体の向きもカメラの前から測る（振り向き終わっていれば move の角度と同じになる）
            Vector3 cf = cam.GetForward(); cf.y = 0.0f;
            const float camYaw = DirectX::XMConvertToDegrees(std::atan2(cf.x, cf.z));
            const float bodyFromCam = std::remainder(camYaw - m_Registry.Get<TransformComponent>(m_Player).rotation.y, 360.0f);
            char line[200];
            snprintf(line, sizeof(line), "loco %s dir %s move %.0f body %.0f gait %d rate %.2f speed %.2f",
                s.name, MoveDirName(st.moveDir), st.moveAngleCam, bodyFromCam, pa.CurrentGait(), pa.CurrentPlayRate(),
                std::sqrt(rb.velocity.x * rb.velocity.x + rb.velocity.z * rb.velocity.z));
            AutoTestLog(line);
            snprintf(line, sizeof(line), "loco look %s", s.name);   // 外部のスクリーンショット用
            AutoTestLog(line);
        }
        if (s_Phase >= kSegTime)
        {
            s_Phase = 0.0f;
            s_Logged = false;
            ++m_AutoStep;
        }
    }
    else if (m_AutoStep == kSegCount + 1)
    {
        pcs.testInput = false;
        // カメラは既定（Camera.json が無ければコードの既定）へ
        const FollowCamera def;
        cam.distance = def.distance;
        cam.ResetView();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        for (float side : { 0.0f, -1.5f, 1.5f })
        {
            const Vector3 t = pp + f * (side == 0.0f ? 8.0f : 9.0f) + r * side;
            m_Swarm.SpawnEnemy(Vector3(t.x, gy, t.z), 1000.0f, 0.0f);
        }
        if (m_Registry.Has<BackpackComponent>(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int lo = BackpackComponent::GRID / 2 - 1;
            ClearBackpackItems(bp);
            BackpackLogic::Place(bp, ItemID::Fireball, lo, lo, 0);
            BackpackLogic::Place(bp, ItemID::Meteor, lo + 2, lo + 2, 0);
            bp.dirty = true;
            m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        }
        AutoTestLog("loco targets");
        s_Phase = 0.0f;
        m_AutoStep = kSegCount + 2;
    }
    else if (m_AutoStep == kSegCount + 2)
    {
        s_Phase += dt;
        if (s_Phase >= 4.0f && s_Phase - dt < 4.0f) { AutoTestLog("loco blast"); AutoTestLog("loco look blast1"); }
        if (s_Phase >= 5.5f && s_Phase - dt < 5.5f) { AutoTestLog("loco blast"); AutoTestLog("loco look blast2"); }
        if (s_Phase >= 7.0f) { AutoTestLog("loco done"); m_AutoStep = kSegCount + 3; }
    }
}

REGISTER_BATTLE_AUTOTEST("loco", AutoTestLoco)
