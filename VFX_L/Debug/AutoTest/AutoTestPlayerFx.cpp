// ============================================================
// AutoTestPlayerFx.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=playerfx（2026-10-09、プレイヤーの動きのエフェクト。PlayerVFXSystem）
// 1 秒: 湧き停止・全消し・無敵・MP 無限・空中ジャンプ 1 回。周り 12m に動かない的 8 体（杖が撃ち続ける = 詠唱の光）。
//       被弾の閃きを撮りやすいよう 0.5 秒に伸ばす
// 1.2〜1.6 秒: シールド 25 で立ったまま撃つ（手元の光・魔法陣・体の光の粒）→ `playerfx look cast0..4`、`look shield`（トゥーンの護罩）、シールドを空に
// 1.9〜3.5 秒: 前へ走る（小石）→ 2.6 秒 `look run`
// 3.5 秒: 跳ぶ（土煙の輪）→ `look jump`。3.8 秒: 空中ジャンプ（光の輪）→ `look airjump`。着地 → `look land`
// 5.5 秒: 走る速さを 13 m/s に（速さの尾）→ 6.6 秒 `look trail`
// 7.0〜8.2 秒: 走りながらスライディング（土煙と草）→ 7.4 秒 `look slide`
// 9.0 秒: 被弾（無敵を外して 5）→ `look hit`。9.8 秒 done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Player/PlayerStateSystem.h"
#include "Player/ShieldComponent.h"

namespace
{
    class AutoTestPlayerFx final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Look(const char* name);
        int   m_Step = 0;
        bool  m_WasGrounded = true;
        bool  m_LandLooked = false;
        float m_Next = 0.0f;
    };
}

void AutoTestPlayerFx::Look(const char* name)
{
    char line[96];
    const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    const Vector3 hv(rb.velocity.x, 0.0f, rb.velocity.z);
    snprintf(line, sizeof(line), "playerfx look %s speed %.1f vy %.1f grounded %d", name, hv.Length(), rb.velocity.y, rb.isGrounded ? 1 : 0);
    AutoTestLog(line);
}

void AutoTestPlayerFx::Run()
{
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.max = mp.current = 1.0e6f;
    }
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
    auto& pcs = m_PlayerControlSystem;
    const auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);
    const float t = m_AutoTime;

    if (m_Step == 0)
    {
        hp.invincible = true;
        if (t < 1.0f) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const float gy = m_Swarm.GetAIParams().groundY;
        for (int i = 0; i < 8; ++i)
        {
            const float a = (float)i / 8.0f * DirectX::XM_2PI;
            const float x = pp.x + std::sin(a) * 12.0f, z = pp.z + std::cos(a) * 12.0f;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), 1.0e6f, 0.0f);
        }
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = false;
        stats.extraJumps = 1;
        m_PlayerVFX.hitFlashTime = 0.5f;
        if (m_Registry.Has<ShieldComponent>(m_Player))   // 護罩を出す（撃っている間に撮る）
        {
            auto& sh = m_Registry.Get<ShieldComponent>(m_Player);
            sh.max = sh.current = 25.0f;
        }
        pcs.testInput = true;
        pcs.testMove = Vector2::Zero;
        AutoTestLog("playerfx start");
        m_Step = 1;
        m_Next = 1.25f;
        return;
    }

    pcs.testJump = false;
    switch (m_Step)
    {
    case 1:   // 立って撃つ
        if (t >= m_Next)
        {
            static int s_Cast = 0;
            char name[16];
            snprintf(name, sizeof(name), "cast%d", s_Cast++);
            Look(name);
            m_Next += 0.07f;
            if (s_Cast >= 5) { m_Step = 2; m_Next = t + 0.08f; }
        }
        break;
    case 2:   // シールドの護罩（トゥーン）を撮ってから空にして走る
        if (m_Next > 0.0f && t >= m_Next) { Look("shield"); m_Next = -1.0f; }
        if (m_Next < 0.0f)
        {
            if (m_Registry.Has<ShieldComponent>(m_Player))   // 上限ごと 0（戻らない）
            {
                auto& sh = m_Registry.Get<ShieldComponent>(m_Player);
                sh.max = sh.current = 0.0f;
            }
            auto& cam = m_Camera.Camera();   // 足元の小石を撮るため近くへ
            cam.distance = 6.0f;
            cam.SetPitch(22.0f);
        }
        if (t >= 1.9f) pcs.testMove = Vector2(0.0f, 1.0f);
        if (t >= 2.6f) { Look("run"); m_Step = 3; }
        break;
    case 3:   // 跳ぶ
        if (t >= 3.5f) { pcs.testJump = true; m_Step = 4; }
        break;
    case 4:
        if (t >= 3.56f) { Look("jump"); m_Step = 5; }
        break;
    case 5:   // 空中ジャンプ
        if (t >= 3.8f) { pcs.testJump = true; m_Step = 6; }
        break;
    case 6:
        if (t >= 3.86f) { Look("airjump"); m_Step = 7; m_WasGrounded = false; }
        break;
    case 7:   // 着地を待つ
        if (rb.isGrounded && !m_WasGrounded && !m_LandLooked) { m_LandLooked = true; m_Next = t + 0.06f; }
        m_WasGrounded = rb.isGrounded;
        if (m_LandLooked && t >= m_Next) { Look("land"); m_Step = 8; }
        if (t > 6.0f) { AutoTestLog("playerfx no landing"); m_Step = 8; }
        break;
    case 8:   // 速く走る（尾）。カメラから見て右へ（横から見える・前の崖に当たらない）
        if (t >= 5.5f) { stats.moveSpeed = 13.0f; pcs.testMove = Vector2(1.0f, 0.0f); }
        if (t >= 6.6f) { Look("trail"); m_Step = 9; }
        break;
    case 9:   // スライディング
        if (t >= 7.0f) pcs.testSlide = true;
        if (t >= 7.4f) { Look("slide"); m_Step = 10; }
        break;
    case 10:
        if (t >= 8.2f)
        {
            pcs.testSlide = false;
            pcs.testMove = Vector2::Zero;
            stats.moveSpeed = 5.5f;
            m_Step = 11;
        }
        break;
    case 11:  // 被弾
        if (t >= 9.0f)
        {
            hp.invincible = false;
            if (m_Registry.Has<PlayerStateComponent>(m_Player)) m_Registry.Get<PlayerStateComponent>(m_Player).invincibleTimer = 0.0f;
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, 5.0f);
            hp.invincible = true;
            m_Next = t + 0.04f;
            m_Step = 12;
        }
        break;
    case 12:
        if (t >= m_Next) { Look("hit"); m_Step = 13; }
        break;
    case 13:
        if (t >= 9.8f) { AutoTestLog("playerfx done"); m_Step = 14; }
        break;
    default:
        break;
    }
}

REGISTER_BATTLE_AUTOTEST("playerfx", AutoTestPlayerFx)
