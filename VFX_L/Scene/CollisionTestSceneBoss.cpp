// ============================================================
// CollisionTestSceneBoss.cpp
// 戦闘シーンの Boss の技（2026-10-07 に CollisionTestScene.cpp から分けた。技が 5 つに増えたので）:
//   BossAttacks の出力 → 被弾（無敵時間・シールド・ノックバックは雑魚と同じ道）・見た目・音・
//   GPU の Boss への突進の指示（BomberCB の bossCharge*）・地面の警告の輪の描画
//   2026-10-24：面毎の Boss（InitBossFx）と砂の守護者の技の見た目（流沙の渦と足元の効き・潜砂の砂丘・砂の槍の石のトゲ）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/RigidbodyComponent.h"
#include "Core/Application.h"
#include "Core/DevUI.h"
#include "Enemy/BossLooks.h"
#include "Enemy/EnemyLooks.h"
#include "VFX_Editor/VFXId.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerStateSystem.h"
#include "Player/PlayerStatsComponent.h"
#include "ResourcePaths.h"

#include <Windows.h>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kSpearPuffSpacing = 3.0f;   // 砂の槍：根元の砂煙をこの間隔で（m）

    // 砂の守護者の「砂の槍」= IceSpikeField を石の地刺（細長い 1 本のトゲ）で。氷より高く密に、不透明な砂岩の色
    // （明るい色だと固定の光 + 縁の光で白く飛ぶ。2026-10-24 の 1 回目は 0.86 で花びらのように白かった）
    void SetupSandSpikes(IceSpikeField& f)
    {
        f.modelPath = Res::VFX::SandSpikes;
        f.blend = 1;
        f.toon = false;
        f.shade = 1.0f;
        f.color = { 0.42f, 0.30f, 0.17f, 1.0f };
        f.alpha = 1.0f;
        f.intensity = 1.0f;
        f.startGap = 0.0f;       // 線の始まりが既に Boss の足元から離してある
        f.spacing = 0.7f;
        f.sideMin = 0.1f;
        f.sideMax = 0.6f;
        f.heightMin = 2.0f;
        f.heightMax = 3.2f;
        f.growFar = 1.0f;
        f.tiltDeg = 10.0f;
        f.tiltJitterDeg = 10.0f;
        f.maxStep = 1.5f;
        f.riseTime = 0.12f;
        f.holdTime = 0.8f;
        f.sinkTime = 0.4f;
        f.sink = 0.2f;
        f.overshoot = 0.15f;
    }
}

// ============================================================
// 面の Boss に合わせる（Init・トレーニングのフィールド切替）。VFXL_BOSS_TYPE=sand / stone で替えられる（TEMP-TEST）
// ============================================================
void CollisionTestScene::InitBossFx(BossType type)
{
    BossType t = type;
    char env[16] = {};
    if (DevUI::kEnabled && GetEnvironmentVariableA("VFXL_BOSS_TYPE", env, sizeof(env)) > 0)
    {
        if (_stricmp(env, "sand") == 0) t = BossType::SandGuardian;
        else if (_stricmp(env, "stone") == 0) t = BossType::StoneGiant;
    }
    m_BossAttacks.Configure(t);
    BossLooks::Apply(t, Application::Get().GetGraphics().GetDevice(), m_Swarm);   // 皮と飾り（2026-10-24）
    EnemyLooks::ApplyRuins(Application::Get().GetGraphics().GetDevice(), m_Swarm);   // 遺跡の敵の皮と飾り（2026-10-25）
    SetupSandSpikes(m_SandSpikes);
    m_SandSpikes.Clear();
    m_BossDelayedVfx.clear();
    if (m_BossMoundVfx != 0) m_AreaVFX.StopInstance(m_BossMoundVfx);
    m_BossMoundVfx = 0;
    if (m_BossAuraVfx != 0) m_AreaVFX.StopInstance(m_BossAuraVfx);
    m_BossAuraVfx = 0;
    InitElites();   // 面の固定エリート（個体・皮・飾り。2026-10-25）
}

void CollisionTestScene::UpdateBossAttacks(float dt, const Vector3& player)
{
    // 輪の爆発（重撃・投石雨）は中に居るかをここで見る：水平距離 ≤ 半径、足の高さが輪の地面から上の範囲
    //   重撃：足が kSlamDodgeHeight より上なら外れ（跳べば避けられる）
    //   投石雨：上から落ちてくるので跳んでも kRockReachHeight までは当たる（走って避ける技）
    constexpr float kSlamDodgeHeight = 0.8f;
    constexpr float kRockReachHeight = 3.0f;

    float feetDrop = 0.9f;   // カプセルの中心 → 足
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        const auto& st = m_Registry.Get<PlayerStatsComponent>(m_Player);
        feetDrop = st.height * 0.5f + st.radius;
    }

    BossAttacks::Input in;
    in.dt = dt;
    in.bossAlive = m_Stage.IsBossAlive();
    in.bossHpRatio = m_Stage.BossHpRatio();
    in.bossPos = m_Stage.BossPos();
    in.player = player;
    in.playerFeetY = player.y - feetDrop;
    in.damageMul = m_Mobs.GetDamageMul();
    BossAttacks::Output out;
    m_BossAttacks.Update(in, m_Grid, out);

    // ---- GPU の Boss へ：突進は一直線に走る、溜め（衝撃波・突進）の間はその場で止まる ----
    auto& bomber = m_Swarm.GetBomberParams();
    bomber.bossChargeOn = out.chargeOn ? 1.0f : 0.0f;
    bomber.bossChargeDirX = out.chargeDir.x;
    bomber.bossChargeDirZ = out.chargeDir.y;
    bomber.bossChargeSpeed = out.chargeSpeed;
    // 面毎の体の色・潜砂の沈み具合（2026-10-24）
    bomber.bossTint[0] = m_BossAttacks.bodyTint.x;
    bomber.bossTint[1] = m_BossAttacks.bodyTint.y;
    bomber.bossTint[2] = m_BossAttacks.bodyTint.z;
    bomber.bossBurrow = out.bossBurrow;

    UpdateBossSandFx(dt, in, out);

    // ---- 合図（音・見た目）----
    if (out.started == BossMove::Shockwave || out.started == BossMove::Charge || out.started == BossMove::Summon
        || out.started == BossMove::SandSpear || out.started == BossMove::Quicksand)
        AudioSystem::Get().Play("boss_summon");   // 溜めの咆哮
    if (out.ringsPlaced > 0) AudioSystem::Get().Play("boss_slam_warn");
    for (const Vector3& p : out.rockFalls)
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockFall), p, 0.6f, false, m_VFXContext);
    if (out.waveLaunched)
    {
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), out.wavePos, 2.0f, false, m_VFXContext);
        AudioSystem::Get().Play("boss_impact");
        OnExplosions(1);
        m_Grass.AddGust(out.wavePos, 4.0f);     // 砕いた所の草が倒れ、落ち葉が舞う（CPU の範囲なので自分で押す。2026-10-13）
        m_Debris.AddGust(out.wavePos, 4.0f);
    }
    if (out.chargeGo) AudioSystem::Get().Play("boss_slam_warn");

    // ---- 召喚：輪から雑魚・自爆兵（今の難度の HP・速さ。同時上限は見ない）。土煙は死んだ時の砕けと同じ物 ----
    for (const BossAttacks::Output::Summon& s : out.summons)
    {
        const uint32_t kind = s.kind;   // Boss 毎（草原 = 雑魚・自爆兵、砂漠 = トゲ玉・砂潜り）
        float hp = 1.0f, speed = 1.0f;
        m_Mobs.KindStats(kind, hp, speed);
        m_Swarm.SpawnEnemy(s.pos, hp, speed, kind);
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::MobDeath), s.pos, 1.0f, false, m_VFXContext);
    }
    if (!out.summons.empty()) AudioSystem::Get().PlayBurst("boss_slam", (uint32_t)out.summons.size());

    // ---- 当たり ----
    for (const BossAttacks::Hit& h : out.hits)
    {
        if (h.ranged)
        {
            const bool rock = (h.kind == BossMove::RockRain);
            const bool burst = (h.kind == BossMove::Burrow);   // 潜砂の飛び出し（見た目・音は UpdateBossSandFx が出す）
            if (!burst)
            {
                m_AreaVFX.Play(VFXDatabase::GetFile(rock ? VFXId::BossRockHit : VFXId::BossSlam), h.center, rock ? 1.2f : 2.0f, false, m_VFXContext);
                AudioSystem::Get().Play("boss_slam");
                if (!rock) OnExplosions(1);
                m_Grass.AddGust(h.center, rock ? 2.0f : 3.0f);   // 重撃 / 投石の着弾でも草が倒れる（2026-10-13）
                m_Debris.AddGust(h.center, rock ? 2.0f : 3.0f);
            }
            if (IsPlayerDead()) continue;

            const Vector2 d(player.x - h.center.x, player.z - h.center.z);
            const float feetAbove = in.playerFeetY - h.center.y;
            if (d.Length() > h.radius || feetAbove > (rock ? kRockReachHeight : kSlamDodgeHeight) || feetAbove < -2.0f)
                continue;
            if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;   // 無敵中
            ++m_BossSlamHits;
            const Vector2 dir = (d.LengthSquared() > 1e-4f) ? d / d.Length() : Vector2(0.0f, 1.0f);
            PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, dir, !rock);   // 重撃は爆発の強さ、岩は殴りの強さ
        }
        else
        {
            // 衝撃波・突進・砂の槍：BossAttacks が当たりを決めた
            if (IsPlayerDead()) continue;
            if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;
            ++m_BossSlamHits;
            // 砂の槍は下から突き上げる = トゲ玉と同じ高く遠い弾き方
            PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, h.dir,
                h.kind == BossMove::SandSpear ? PlayerControlSystem::KnockKind::Spike : PlayerControlSystem::KnockKind::Blast);
            if (h.kind == BossMove::Charge)
            {
                AudioSystem::Get().Play("boss_slam");
                OnExplosions(1);
            }
        }
    }

    // ---- 警告の輪・衝撃波の帯・突進の道筋を GPU の描画へ ----
    // 面の固定エリートの予告も同じ表に足すので、GPU へ上げるのは UpdateElites（2026-10-25）
    m_WarnCircles.clear();
    for (const BossAttacks::Visual& v : m_BossAttacks.Visuals())
    {
        if ((int)m_WarnCircles.size() >= SwarmSystem::kMaxWarnCircles) break;
        SwarmSystem::WarnCircle c;
        c.center = v.center;
        c.radius = v.radius;
        c.progress = v.progress;
        c.band = v.band;
        m_WarnCircles.push_back(c);
    }
}

// ============================================================
// 砂の守護者の技の見た目と足元の効き（2026-10-24）
//   流沙：プレイヤーが中で地面に居れば遅く・中心へ（PlayerStateComponent::groundSlow / groundPull、PlayerControlSystem が使う）。
//         渦は風源にもなる（砂屑・粒子・草が回る）。見た目 = BossQuicksand.json（砂の液面の渦 + 中心へ流れる砂粒）
//   潜砂：潜る / 飛び出す瞬間に砂の噴き上げ、地面の下を動く間は Boss に付いて動く砂丘（BossSandMound.json、ループ）
//   砂の槍：線毎に石のトゲ（m_SandSpikes、前線と同じ速さで足元から先へ）+ 前線が通る時刻に根元の砂煙
// ============================================================
void CollisionTestScene::UpdateBossSandFx(float dt, const BossAttacks::Input& in, const BossAttacks::Output& out)
{
    auto groundAt = [this](float x, float z) { return Vector3(x, m_Grid.SampleHeight(x, z), z); };
    const Vector3 bossGround = groundAt(in.bossPos.x, in.bossPos.z);

    // ---- Boss の体の周りの見た目（地上に居る間ずっと）：砂の守護者 = 舞う砂 + 小さな風源で足元の砂屑・粒子も回す、
    //      岩の巨人 = 肩から舞い落ちる葉と花粉（2026-10-25）----
    const bool sandBoss = m_BossAttacks.Type() == BossType::SandGuardian;
    if (in.bossAlive && out.bossBurrow < 0.5f)
    {
        if (m_BossAuraVfx == 0)
            m_BossAuraVfx = m_AreaVFX.Play(VFXDatabase::GetFile(sandBoss ? VFXId::BossSandAura : VFXId::BossStoneAura),
                bossGround, 1.0e5f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_BossAuraVfx, bossGround, bossGround);
    }
    else if (m_BossAuraVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BossAuraVfx);
        m_BossAuraVfx = 0;
    }
    if (in.bossAlive && sandBoss && out.bossBurrow < 0.5f)
    {
        Swarm::WindSource w = {};
        w.pos[0] = bossGround.x;
        w.pos[1] = bossGround.y;
        w.pos[2] = bossGround.z;
        w.radius = 4.5f;
        w.swirl = 4.5f;
        w.updraft = 0.6f;
        w.inflow = 0.4f;
        w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);
    }

    // ---- 流沙 ----
    const BossAttacks::QuicksandField& sand = m_BossAttacks.Quicksand();
    if (sand.active)
    {
        bool grounded = true;
        if (m_Registry.Has<RigidbodyComponent>(m_Player)) grounded = m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        float slow = 1.0f;
        Vector2 pull;
        if (!IsPlayerDead() && m_Registry.Has<PlayerStateComponent>(m_Player)
            && m_BossAttacks.QuicksandPull(Vector3(in.player.x, in.playerFeetY, in.player.z), grounded, slow, pull))
        {
            auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            st.groundSlow = slow;
            st.groundPullX = pull.x;
            st.groundPullZ = pull.y;
        }
        Swarm::WindSource w = {};
        w.pos[0] = sand.center.x;
        w.pos[1] = sand.center.y;
        w.pos[2] = sand.center.z;
        w.radius = (std::max)(sand.radius, 0.5f) * 1.15f;
        w.swirl = 9.0f;
        w.updraft = 0.8f;
        w.inflow = 2.5f;
        w.fade = sand.strength;
        m_Swarm.AddFrameWindSource(w);
    }
    if (out.quicksandStarted)
    {
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossQuicksand), out.quicksandPos,
            m_BossAttacks.sandWindup + m_BossAttacks.sandDuration, false, m_VFXContext);
        AudioSystem::Get().Play("cast_gust");
    }

    // ---- 潜砂 ----
    if (out.burrowSank)
    {
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), bossGround, 2.0f, false, m_VFXContext);
        AudioSystem::Get().Play("boss_slam");
        m_Debris.AddGust(bossGround, 3.0f);
    }
    if (out.burrowMound)
    {
        if (m_BossMoundVfx == 0)
            m_BossMoundVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandMound), bossGround, 30.0f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_BossMoundVfx, bossGround, bossGround);
    }
    else if (m_BossMoundVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BossMoundVfx);
        m_BossMoundVfx = 0;
    }
    if (out.burrowEmerged)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), bossGround, 2.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 1.5f);
        AudioSystem::Get().Play("boss_impact");
        OnExplosions(1);
        m_Grass.AddGust(bossGround, m_BossAttacks.burrowRadius);
        m_Debris.AddGust(bossGround, m_BossAttacks.burrowRadius);
    }

    // ---- 砂の槍 ----
    for (const BossAttacks::Output::SpearLine& l : out.spearLines)
    {
        const float speed = (std::max)(out.spearSpeed, 1.0f);
        m_SandSpikes.waveSpeed = speed;
        m_SandSpikes.Spawn(l.start, l.end, 1.0f, [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
        Vector3 d(l.end.x - l.start.x, 0.0f, l.end.z - l.start.z);
        const float len = d.Length();
        if (len < 1e-3f) continue;
        d /= len;
        for (float s = 0.0f; s <= len; s += kSpearPuffSpacing)
        {
            BossDelayedVfx v;
            v.wait = s / speed;
            v.id = (int)VFXId::BossSandSpear;
            v.pos = groundAt(l.start.x + d.x * s, l.start.z + d.z * s);
            m_BossDelayedVfx.push_back(v);
        }
    }
    if (!out.spearLines.empty()) AudioSystem::Get().Play("boss_slam");
    for (auto it = m_BossDelayedVfx.begin(); it != m_BossDelayedVfx.end();)
    {
        it->wait -= dt;
        if (it->wait > 0.0f) { ++it; continue; }
        m_AreaVFX.Play(VFXDatabase::GetFile((VFXId)it->id), it->pos, 1.5f, false, m_VFXContext);
        m_Debris.AddGust(it->pos, 1.5f);
        it = m_BossDelayedVfx.erase(it);
    }
    m_SandSpikes.Update(dt);
}