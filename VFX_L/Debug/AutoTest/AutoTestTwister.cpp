// ============================================================
// AutoTestTwister.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=twister
// 竜巻（上級の範囲魔法、2026-10-22。前提 = 渦 + 突風）：誘発で出るか、敵の方へ進むか、周りの敵を巻き込むか、見た目。
//   1 秒: 湧き停止・全消し・HP 10 万・MP 無限・入力 0。9×9 に枠を敷き、
//         竜巻 (3,4) 漏斗 → (3,3)(3,4)(3,5)(4,4)(5,4)、渦 (4,2) 逆 L → (4,3)(5,2)(5,3)（(4,3) が竜巻の (4,4)(3,3) に隣接）、
//         突風 (4,5) 横 2 → (4,5)(4,6)（(4,5) が竜巻の (4,4)(3,5) に隣接）。`twister place` に各 index と竜巻の発動可否。
//         正面 +Z 11〜15m の横 12m に雑魚 18 体（HP 300、1.8 m/s で寄ってくる）。カメラは斜め横 22m / 25°（毎フレーム固定）。
//   以後 0.25 秒ごとに `twister t casts trig twisters pos near avgD alive lost`
//   （casts = 誘発で撃った上級魔法の累計、near = 竜巻 0 の 3m 以内の雑魚、avgD = 竜巻 0 から 6m 以内の雑魚の平均距離）。
//   最初の竜巻が出てから 0.4 / 1.4 / 2.6 / 3.6 秒で `twister look born / sweep / mid / late`。14 秒で `twister result` + done。
//   VFXL_TWISTER_FADE=<倍率>（2026-10-22、出る / 消える移り変わりの確認）：WeaponSystem::twisterLook の introTime / outroTime を
//   その倍率に伸ばし（スクリーンショットは 0.3 秒ほど遅れるので、普段の速さだと途中の形が撮れない）、最初の竜巻が出たら
//   バックパックを空にして 2 本目を出さない。look は intro1〜3（伸び上がる途中）/ full / outro1〜3（ほどける途中）/ gone
// 期待：渦の弾が消えた所か突風の吹いた所に竜巻が出る、敵の方へ進む（pos が動く）、近くの雑魚が集まる（near が増える）
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    // 斜め横から（竜巻はプレイヤーから外へ進むので、真後ろの高い視点だと画面上端の HUD の下へ入る）
    constexpr float kCamYaw = 50.0f;
    constexpr float kCamPitch = 25.0f;

    class AutoTestTwister final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        float m_NextRead = 1.5f;
        float m_FirstTwister = -1.0f;
        Vector3 m_BornPos;
        float m_MaxTravel = 0.0f;
        int m_MaxNear = 0;
        bool m_StatsLogged = false;
        float m_FadeMul = 0.0f;   // VFXL_TWISTER_FADE（0 = 普段の look）
        int m_FadeLook = 0;
    };
}

void AutoTestTwister::Run()
{
    char line[400];
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.max = hp.current = 100000.0f;
        }
        if (m_Registry.Has<ManaComponent>(m_Player))
        {
            auto& mp = m_Registry.Get<ManaComponent>(m_Player);
            mp.max = mp.current = 1.0e6f;
        }
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        if (m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            FillAllFrames(bp);
            const int tw = BackpackLogic::Place(bp, ItemID::Twister, 3, 4, 0);
            const int vv = BackpackLogic::Place(bp, ItemID::VoidVortex, 4, 2, 0);
            const int gu = BackpackLogic::Place(bp, ItemID::Gust, 4, 5, 0);
            // VFXL_TWISTER_MAG=1：拡大鏡を (5,5) に。斜め 4 マス = (4,4) 竜巻 / (4,6) 突風（渦には届かない）
            char env[8] = {};
            const bool mag = GetEnvironmentVariableA("VFXL_TWISTER_MAG", env, sizeof(env)) > 0 && env[0] == '1';
            const int mg = mag ? BackpackLogic::Place(bp, ItemID::Magnifier, 5, 5, 0) : -1;
            bp.dirty = true;
            snprintf(line, sizeof(line), "twister place twister %d vortex %d gust %d magnifier %d ready %d", tw, vv, gu, mg,
                tw >= 0 ? (BackpackLogic::IsTriggerReady(bp, tw) ? 1 : 0) : -1);
            AutoTestLog(line);
        }
        const float groundY = m_Swarm.GetAIParams().groundY;
        for (int k = 0; k < 18; ++k)
        {
            const float x = tf.position.x - 6.0f + 12.0f * (float)(k % 6) / 5.0f;
            const float z = tf.position.z + 11.0f + 2.0f * (float)(k / 6);
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 300.0f, 1.8f, Swarm::kEnemyKindMob);
        }
        auto& cam = m_Camera.Camera();
        cam.SetYaw(kCamYaw);
        cam.SetPitch(kCamPitch);
        cam.distance = 22.0f;
        cam.avoidOcclusion = false;
        cam.SnapToTarget();
        char fade[16] = {};
        if (GetEnvironmentVariableA("VFXL_TWISTER_FADE", fade, sizeof(fade)) > 0)
        {
            m_FadeMul = (std::max)(1.0f, (float)std::atof(fade));
            m_WeaponSystem.twisterLook.introTime *= m_FadeMul;
            m_WeaponSystem.twisterLook.outroTime *= m_FadeMul;
            snprintf(line, sizeof(line), "twister fade x%.1f intro %.2f outro %.2f", m_FadeMul,
                m_WeaponSystem.twisterLook.introTime, m_WeaponSystem.twisterLook.outroTime);
            AutoTestLog(line);
        }
        AutoTestLog("twister start");
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;
    // カメラを毎フレーム固定する（ユーザーがマウスを動かすと視点が回り、スクリーンショットの角度がずれた。2026-10-22）
    {
        auto& cam = m_Camera.Camera();
        cam.SetYaw(kCamYaw);
        cam.SetPitch(kCamPitch);
    }
    // 集約が済んだ後に 1 回：竜巻・突風の半径と MP（拡大鏡の有無を比べる）
    if (!m_StatsLogged && m_AutoTime >= 1.5f && m_Registry.Has<WandComponent>(m_Player))
    {
        m_StatsLogged = true;
        for (const auto& a : m_Registry.Get<WandComponent>(m_Player).areas)
        {
            snprintf(line, sizeof(line), "twister area id %d radius %.2f mana %.1f triggered %d", (int)a.id, a.radius, a.manaCost, a.triggered ? 1 : 0);
            AutoTestLog(line);
        }
    }

    const int count = m_WeaponSystem.GetTwisterCount();
    Vector3 t0;
    const bool has = m_WeaponSystem.GetTwisterPos(0, t0);
    if (has && m_FirstTwister < 0.0f) { m_FirstTwister = m_AutoTime; m_BornPos = t0; }
    if (has && m_FirstTwister >= 0.0f)
    {
        const float dx = t0.x - m_BornPos.x, dz = t0.z - m_BornPos.z;
        m_MaxTravel = (std::max)(m_MaxTravel, std::sqrt(dx * dx + dz * dz));
    }

    if (m_AutoTime >= m_NextRead)
    {
        m_NextRead += 0.25f;
        std::vector<Swarm::Enemy> en;
        std::vector<uint32_t> st;
        if (m_Swarm.DebugReadEnemies(en, st))
        {
            int alive = 0, nearCount = 0, inSix = 0;
            float sumD = 0.0f, hp = 0.0f;
            for (size_t i = 0; i < en.size(); ++i)
            {
                if (st[i] == Swarm::kStateDead) continue;
                ++alive;
                hp += Swarm::HpFromFixed(en[i].hp);
                if (!has) continue;
                const float dx = en[i].position.x - t0.x, dz = en[i].position.z - t0.z;
                const float d = std::sqrt(dx * dx + dz * dz);
                if (d < 3.0f) ++nearCount;
                if (d < 6.0f) { sumD += d; ++inSix; }
            }
            m_MaxNear = (std::max)(m_MaxNear, nearCount);
            const float lost = 300.0f * 18.0f - hp;   // 死んだ雑魚は 300 ずつ数える
            snprintf(line, sizeof(line), "twister t %.2f casts %u trig %u twisters %d pos %.1f %.1f near %d avgD %.2f alive %d lost %.0f",
                m_AutoTime, m_WeaponSystem.GetTriggeredCasts(), m_WeaponSystem.GetTriggerEventsSeen(), count,
                has ? t0.x : 0.0f, has ? t0.z : 0.0f, nearCount, inSix ? sumD / inSix : 0.0f, alive, lost);
            AutoTestLog(line);
        }
    }

    // 出る / 消える移り変わりの look（VFXL_TWISTER_FADE）。最初の竜巻だけを撮る
    if (m_FadeMul > 0.0f && m_FirstTwister >= 0.0f)
    {
        if (m_FadeLook == 0 && m_Registry.Has<BackpackComponent>(m_Player))
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            ClearBackpackItems(bp);
            bp.dirty = true;
        }
        const float in = m_WeaponSystem.twisterLook.introTime, out = m_WeaponSystem.twisterLook.outroTime;
        const float life = 4.0f;   // AreaData/Twister.json の duration
        struct Look { float at; const char* name; };
        const Look looks[] = {
            { in * 0.2f, "intro1" }, { in * 0.45f, "intro2" }, { in * 0.72f, "intro3" }, { in + 0.8f, "full" },
            { life + out * 0.1f, "outro1" }, { life + out * 0.35f, "outro2" }, { life + out * 0.62f, "outro3" },
            { life + out + 0.4f, "gone" } };
        if (m_FadeLook < (int)(sizeof(looks) / sizeof(looks[0])) && m_AutoTime >= m_FirstTwister + looks[m_FadeLook].at)
        {
            snprintf(line, sizeof(line), "twister look %s twisters %d", looks[m_FadeLook].name, count);
            AutoTestLog(line);
            ++m_FadeLook;
        }
        if (m_FadeLook >= (int)(sizeof(looks) / sizeof(looks[0])) && m_AutoStep < 6)
        {
            AutoTestLog("twister done");
            m_AutoStep = 6;
        }
        return;
    }
    if (m_FirstTwister >= 0.0f)
    {
        if (m_AutoStep == 1 && m_AutoTime >= m_FirstTwister + 0.4f) { AutoTestLog("twister look born"); m_AutoStep = 2; }
        if (m_AutoStep == 2 && m_AutoTime >= m_FirstTwister + 1.4f) { AutoTestLog("twister look sweep"); m_AutoStep = 3; }
        if (m_AutoStep == 3 && m_AutoTime >= m_FirstTwister + 2.6f) { AutoTestLog("twister look mid"); m_AutoStep = 4; }
        if (m_AutoStep == 4 && m_AutoTime >= m_FirstTwister + 3.6f) { AutoTestLog("twister look late"); m_AutoStep = 5; }
    }
    if (m_AutoStep >= 1 && m_AutoStep <= 5 && m_AutoTime >= 14.0f)
    {
        snprintf(line, sizeof(line), "twister result first %.2f born %.1f %.1f travel %.1f maxNear %d casts %u",
            m_FirstTwister, m_BornPos.x, m_BornPos.z, m_MaxTravel, m_MaxNear, m_WeaponSystem.GetTriggeredCasts());
        AutoTestLog(line);
        AutoTestLog("twister done");
        m_AutoStep = 6;
    }
}

REGISTER_BATTLE_AUTOTEST("twister", AutoTestTwister)
