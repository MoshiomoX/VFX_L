// ============================================================
// CollisionTestSceneBossDrama.cpp
// Boss の狂暴・撃破の演出（2026-10-29、ユーザー「Boss の演出を始めて」→ 狂暴 = 止まって吠える（推奨、無回答のため）、
// 撃破 = スロー + 光の爆ぜと砕け、残った雑魚も一緒に砕いて経験値を落とす）。登場は CollisionTestSceneBossIntro.cpp。
//   狂暴：HP が半分を切った後、技の切れ目で 1 回だけ止まって吠える（BossAttacks が止める・赤い輪を出す）。
//         ここは咆哮の音・揺れ・「〇〇が怒り狂った！」・足元の赤い爆ぜ、その後ずっと体に付く赤い火の粉
//   撃破：倒した瞬間から kSlowTime 秒（実時間）ゲームを kSlowMul 倍の速さにし（Update が gameplay の dt を縮める）、
//         金の光の爆ぜ + 石と土煙 + 強い揺れ + 金の「〇〇を倒した！」。倒した所から波が広がり、
//         通った所の雑魚を GPU の範囲（大きな威力）で砕く = 普通の撃破と同じく経験値・砕け・ダメージの数字が出る
// UpdateBossAttacks（CollisionTestSceneBoss.cpp）が毎フレーム呼ぶ（gameplay の dt）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "VFX_Editor/VFXId.h"
#include "Enemy/Boss/BossBrain.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kRageAuraScale = 2.5f;   // 精鋭の赤い火の粉（EliteBerserkAura）を Boss の大きさへ
    constexpr float kSlowTime = 0.8f;        // 倒した瞬間のスローの長さ（実時間の秒）
    constexpr float kSlowMul = 0.25f;        // その間のゲームの速さ
    constexpr float kWaveSpeed = 24.0f;      // 雑魚を砕く波の広がる速さ m/s（gameplay の秒）
    constexpr float kWaveMax = 30.0f;        // 波の届く半径 m
    constexpr float kWaveStep = 0.08f;       // 範囲を置く間隔（秒）。1 つの範囲 = 今の半径の円の中を全部
    constexpr float kWaveDamage = 1.0e6f;    // 何でも 1 回で砕く（ダメージの数字は実際に減った HP だけが出る）
}

void CollisionTestScene::UpdateBossDrama(float dt, const BossAttacks::Input& in, const BossAttacks::Output& out)
{
    const Vector3 bossGround(in.bossPos.x, m_Grid.SampleHeight(in.bossPos.x, in.bossPos.z), in.bossPos.z);
    const wchar_t* bossName = BossDisplayName(m_BossAttacks.Type());

    // ---- 狂暴の咆哮の瞬間 ----
    if (out.enrageRoared)
    {
        AudioSystem::Get().Play("boss_summon");
        AudioSystem::Get().Play("boss_impact");
        m_Camera.Camera().RaiseTrauma(0.55f);
        const uint32_t fx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteBerserk), bossGround, 1.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(fx, kRageAuraScale);
        m_Grass.AddGust(bossGround, m_BossAttacks.enrageRingMax);   // 咆哮の風で草が外へ倒れ、落ち葉が舞う
        m_Debris.AddGust(bossGround, m_BossAttacks.enrageRingMax);
        swprintf_s(m_BossDramaText, L"%sが怒り狂った！", bossName);
        m_Stage.SetAnnounce(m_BossDramaText, 2.5f);
    }

    // ---- 狂暴の間ずっと：体に赤い火の粉（吠えてから。蘇った騎士・テストの狂暴は吠えないのですぐ）。潜っている間は出さない ----
    const bool rage = in.bossAlive && m_BossAttacks.IsEnraged() && out.bossBurrow < 0.5f
        && (m_BossAttacks.EnrageRoarDone() || m_BossAttacks.forceEnraged || m_BossAttacks.testEnraged || !m_BossAttacks.enrageRoar);
    if (rage)
    {
        if (m_BossRageVfx == 0)
        {
            m_BossRageVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteBerserkAura), bossGround, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.ScaleInstance(m_BossRageVfx, kRageAuraScale);
        }
        else
            m_AreaVFX.SetInstance(m_BossRageVfx, bossGround, bossGround);
    }
    else if (m_BossRageVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BossRageVfx);
        m_BossRageVfx = 0;
    }

    // ---- 倒した瞬間（騎士の 1 回目に倒れた時は Reviving で「倒した」にならない）----
    if (m_Stage.IsBossAlive()) m_BossDefeatSeen = false;   // 次の Boss（トレーニングで呼び直す）でまた出す
    if (m_Stage.IsBossDefeated() && !m_BossDefeatSeen)
    {
        m_BossDefeatSeen = true;
        const Vector3 p = m_Stage.BossPos();   // 最後に生きていた所（倒れた後は更新されない）
        m_DefeatPos = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z), p.z);
        m_DefeatSlowLeft = kSlowTime;
        m_DefeatSlowMul = kSlowMul;
        m_DefeatWaveR = 0.0f;
        m_DefeatWaveNext = 0.0f;

        // 金の光の爆ぜ（魔力解放の瞬間の金の火花の輪を大きく）+ 石と土煙
        const uint32_t burst = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::ManaSurgeBurst), m_DefeatPos, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(burst, 3.5f);
        const uint32_t slam = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), m_DefeatPos, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(slam, 1.8f);
        m_Water.AddRipple(m_DefeatPos, 3.0f, m_Water.GetSettings().blastStrength, true);
        AudioSystem::Get().Play("boss_impact");
        AudioSystem::Get().Play("boss_slam");
        m_Camera.Camera().RaiseTrauma(1.0f);
        OnExplosions(1);   // リザルトの写真の山場の候補にも
        swprintf_s(m_BossDramaText, L"%sを倒した！", bossName);
        m_Stage.SetAnnounce(m_BossDramaText, 3.5f, true);
        if (m_BossRageVfx != 0)
        {
            m_AreaVFX.StopInstance(m_BossRageVfx);
            m_BossRageVfx = 0;
        }
    }

    // ---- 倒した所から広がる波：通った所の雑魚を砕く（スローの間はゆっくり広がる）----
    if (m_DefeatWaveR >= 0.0f)
    {
        m_DefeatWaveR = (std::min)(m_DefeatWaveR + kWaveSpeed * dt, kWaveMax);
        m_DefeatWaveNext -= dt;
        if (m_DefeatWaveNext <= 0.0f || m_DefeatWaveR >= kWaveMax)
        {
            m_DefeatWaveNext = kWaveStep;
            Swarm::Area a;
            a.center = m_DefeatPos;
            a.radius = (std::max)(m_DefeatWaveR, 1.0f);
            a.halfHeight = 8.0f;          // 台地の上下も
            a.damage = kWaveDamage;
            a.tickInterval = 1.0f;        // 出た最初のステップで 1 回だけ
            a.timeLeft = 0.05f;
            a.tickNow = (uint32_t)Swarm::DamageElement::Light << Swarm::kAreaExtraElementShift;   // 数字は光（金）の色
            m_Swarm.SpawnArea(a);
            m_Grass.AddGust(m_DefeatPos, m_DefeatWaveR);
            m_Debris.AddGust(m_DefeatPos, m_DefeatWaveR);
        }
        if (m_DefeatWaveR >= kWaveMax) m_DefeatWaveR = -1.0f;
    }
}
