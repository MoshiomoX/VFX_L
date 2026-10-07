// ============================================================
// AutoTestShield.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=shield
// VFXL_BATTLE_AUTOTEST=shield：シールド（2026-10-07）。受け止め・割れ・溢れた分が消える・5 秒後の回復・
//   GPU の雑魚の接触・能力カード「最大シールド +25」を記録し、反応のエフェクトと HUD を撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Player/ShieldComponent.h"
#include "Player/PlayerStateSystem.h"

namespace
{
    class AutoTestShield final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        // 最後に 4 択を開く（gameplay が止まる）ので Update から呼ぶ
        void UpdateFrame(float dt) override { Run(dt); }

    private:
        void Run(float dt);
    };
}

// ============================================================
// 実時間で進める。
// 1 秒: 湧き停止・全消し・詠唱停止・入力 0、HP 100 / 100（瀕死で死なないよう hp.invincible）、カメラ 6m / 16°。
//   護罩の模型の包囲箱を記録（エフェクトの位置合わせ用）
// 1.5 秒: 10 を当てる → シールド 25 → 15、HP は減らないはず。0.1 秒後 `shield look hit`
// 2.5 秒: 40 を当てる → シールド 15 → 0（溢れた 25 は消える）、HP 100 のまま。0.1 / 0.25 秒後 `shield look break / break2`
// 3.5 秒: 10 を当てる → シールド 0 なので HP 90
// 以後 0.5 秒毎に `shield t cur since recharging hp`。8.5 秒から戻り始め 10.5 秒で満タンのはず。
//   9.4 秒 `shield look charging`、満タンになって 0.1 秒後 `shield look full`
// 12 秒: 正面 2.5m に雑魚 1 体（GPU の接触）。0.25 秒毎に記録、14.5 秒 `shield look contact`、16 秒に消す
// 17 秒: 4 択に「最大シールド +25」を入れて開く、17.8 秒 `shield look card`、18.5 秒 それを選ぶ → 上限 50 / 今 +25
// 19.5 秒 done
// ============================================================
void AutoTestShield::Run(float dt)
{
    static float s_Real = 0.0f;
    static int s_Step = 0;
    static float s_NextLog = 0.0f;
    static float s_FullAt = -1.0f;
    static bool s_FullShot = false;
    s_Real += dt;
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<ShieldComponent>(m_Player)) return;
    auto& sh = m_Registry.Get<ShieldComponent>(m_Player);
    auto& hp = m_Registry.Get<HealthComponent>(m_Player);
    auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
    auto& lv = m_Registry.Get<LevelComponent>(m_Player);
    if (s_Step < 9 || s_Step >= 15) lv.experience = 0.0f;   // 勝手に 4 択が出ないように
    char line[200];

    // 被弾の窓口と同じ経路で当てる（前の被弾の無敵時間は切っておく）
    auto hit = [&](float dmg, const char* tag)
        {
            st.invincibleTimer = 0.0f;
            const float shBefore = sh.current, hpBefore = hp.current;
            const bool took = PlayerStateSystem::TryApplyHit(m_Registry, m_Player, dmg);
            snprintf(line, sizeof(line), "shield %s dmg %.0f took %d shield %.1f -> %.1f hp %.1f -> %.1f hits %u breaks %u",
                tag, dmg, took ? 1 : 0, shBefore, sh.current, hpBefore, hp.current, sh.hits, sh.breaks);
            AutoTestLog(line);
        };

    if (s_Step == 0 && s_Real >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player))
            m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        hp.max = 100.0f;
        hp.current = 100.0f;
        hp.invincible = true;
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.distance = 6.0f;
        cam.SetPitch(16.0f);
        cam.avoidOcclusion = false;
        cam.SnapToTarget();

        for (const char* m : { "Assets/VFX/Mesh/fanghuzhao2.FBX", "Assets/VFX/Mesh/suipian01.FBX", "Assets/VFX/Mesh/suipian02.FBX" })
        {
            auto model = ResourceManager::Get().LoadModel(m);
            if (!model) { snprintf(line, sizeof(line), "shield model %s missing", m); AutoTestLog(line); continue; }
            const Vector3 a = model->GetBoundsMin(), b = model->GetBoundsMax();
            snprintf(line, sizeof(line), "shield model %s min (%.3f,%.3f,%.3f) max (%.3f,%.3f,%.3f)", m, a.x, a.y, a.z, b.x, b.y, b.z);
            AutoTestLog(line);
        }
        snprintf(line, sizeof(line), "shield start max %.0f cur %.0f delay %.1f refill %.1f", sh.max, sh.current,
            sh.rechargeDelay, sh.refillTime);
        AutoTestLog(line);
        s_Step = 1;
    }
    else if (s_Step == 1 && s_Real >= 1.3f) { AutoTestLog("shield look bubble"); s_Step = 15; }   // 常に包む護罩（2026-10-07）
    else if (s_Step == 15 && s_Real >= 1.5f) { hit(10.0f, "absorb"); s_Step = 2; }
    else if (s_Step == 2 && s_Real >= 1.55f) { AutoTestLog("shield look hit"); s_Step = 16; }       // 受けた瞬間の赤
    else if (s_Step == 16 && s_Real >= 1.75f) { AutoTestLog("shield look hitfade"); s_Step = 3; }   // 青へ戻る途中
    else if (s_Step == 3 && s_Real >= 2.5f) { hit(40.0f, "break"); s_Step = 4; }
    else if (s_Step == 4 && s_Real >= 2.6f) { AutoTestLog("shield look break"); s_Step = 5; }
    else if (s_Step == 5 && s_Real >= 2.75f) { AutoTestLog("shield look break2"); s_Step = 6; }
    else if (s_Step == 6 && s_Real >= 3.5f) { hit(10.0f, "hp"); s_NextLog = s_Real; s_Step = 7; }
    else if (s_Step == 7)
    {
        if (s_Real >= s_NextLog)
        {
            s_NextLog += 0.5f;
            snprintf(line, sizeof(line), "shield t %.2f cur %.1f since %.2f recharging %d hp %.1f",
                s_Real, sh.current, sh.sinceHit, sh.Recharging() ? 1 : 0, hp.current);
            AutoTestLog(line);
        }
        static bool s_ChargeShot = false;
        if (!s_ChargeShot && s_Real >= 9.4f) { AutoTestLog("shield look charging"); s_ChargeShot = true; }
        if (s_FullAt < 0.0f && sh.Full())
        {
            s_FullAt = s_Real;
            snprintf(line, sizeof(line), "shield full again t %.2f (last hit 3.50, delay %.1f + refill %.1f)", s_Real,
                sh.rechargeDelay, sh.refillTime);
            AutoTestLog(line);
        }
        if (s_FullAt > 0.0f && !s_FullShot && s_Real >= s_FullAt + 0.1f) { AutoTestLog("shield look full"); s_FullShot = true; }
        if (s_Real >= 12.0f)
        {
            const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
            m_Swarm.SpawnEnemy(Vector3(pp.x, m_Swarm.GetAIParams().groundY, pp.z + 2.5f), 100000.0f, 1.5f);
            AutoTestLog("shield contact: 1 mob at +Z 2.5m");
            s_NextLog = s_Real;
            s_Step = 8;
        }
    }
    else if (s_Step == 8)
    {
        if (s_Real >= s_NextLog)
        {
            s_NextLog += 0.25f;
            snprintf(line, sizeof(line), "shield contact t %.2f cur %.1f hp %.1f hits %u breaks %u inv %.2f",
                s_Real, sh.current, hp.current, sh.hits, sh.breaks, st.invincibleTimer);
            AutoTestLog(line);
        }
        static bool s_ContactShot = false;
        if (!s_ContactShot && s_Real >= 14.5f) { AutoTestLog("shield look contact"); s_ContactShot = true; }
        if (s_Real >= 16.0f)
        {
            m_Swarm.KillAll();
            s_Step = 9;
        }
    }
    else if (s_Step == 9 && s_Real >= 17.0f)
    {
        lv.ClearChoices();
        lv.pendingChoices = { ItemID::ShieldUp, ItemID::MaxHealthUp, ItemID::MaxManaUp, ItemID::SpellPowerUp };
        snprintf(line, sizeof(line), "shield card offered (choosing %d) before max %.0f cur %.1f", lv.IsChoosing() ? 1 : 0,
            sh.max, sh.current);
        AutoTestLog(line);
        s_Step = 10;
    }
    else if (s_Step == 10 && s_Real >= 17.8f) { AutoTestLog("shield look card"); s_Step = 11; }
    else if (s_Step == 11 && s_Real >= 18.5f)
    {
        const float curBefore = sh.current;
        const bool ok = lv.IsChoosing() && LevelUpSystem::Choose(m_Registry, m_Player, ItemID::ShieldUp);
        snprintf(line, sizeof(line), "shield card chosen %d max %.0f cur %.1f -> %.1f", ok ? 1 : 0, sh.max, curBefore, sh.current);
        AutoTestLog(line);
        s_Step = 12;
    }
    else if (s_Step == 12 && s_Real >= 19.0f) { AutoTestLog("shield look after"); s_Step = 13; }
    else if (s_Step == 13 && s_Real >= 19.5f)
    {
        m_PlayerControlSystem.testInput = false;
        AutoTestLog("shield done");
        s_Step = 14;
    }
}

REGISTER_BATTLE_AUTOTEST("shield", AutoTestShield)
