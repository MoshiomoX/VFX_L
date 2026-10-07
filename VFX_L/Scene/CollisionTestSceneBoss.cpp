// ============================================================
// CollisionTestSceneBoss.cpp
// 戦闘シーンの Boss の技（2026-10-07 に CollisionTestScene.cpp から分けた。技が 5 つに増えたので）:
//   BossAttacks の出力 → 被弾（無敵時間・シールド・ノックバックは雑魚と同じ道）・見た目・音・
//   GPU の Boss への突進の指示（BomberCB の bossCharge*）・地面の警告の輪の描画
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Player/PlayerControlSystem.h"
#include "Player/PlayerStateSystem.h"
#include "Player/PlayerStatsComponent.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

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

    // ---- 合図（音・見た目）----
    if (out.started == BossMove::Shockwave || out.started == BossMove::Charge || out.started == BossMove::Summon)
        AudioSystem::Get().Play("boss_summon");   // 溜めの咆哮
    if (out.ringsPlaced > 0) AudioSystem::Get().Play("boss_slam_warn");
    for (const Vector3& p : out.rockFalls)
        m_AreaVFX.Play("BossRockFall.json", p, 0.6f, false, m_VFXContext);
    if (out.waveLaunched)
    {
        m_AreaVFX.Play("BossSlam.json", out.wavePos, 2.0f, false, m_VFXContext);
        AudioSystem::Get().Play("boss_impact");
        m_Camera.OnShakeAreas(1);
    }
    if (out.chargeGo) AudioSystem::Get().Play("boss_slam_warn");

    // ---- 召喚：輪から雑魚・自爆兵（今の難度の HP・速さ。同時上限は見ない）。土煙は死んだ時の砕けと同じ物 ----
    for (const BossAttacks::Output::Summon& s : out.summons)
    {
        const uint32_t kind = s.bomber ? Swarm::kEnemyKindBomber : Swarm::kEnemyKindMob;
        float hp = 1.0f, speed = 1.0f;
        m_Mobs.KindStats(kind, hp, speed);
        m_Swarm.SpawnEnemy(s.pos, hp, speed, kind);
        m_AreaVFX.Play("MobDeath.json", s.pos, 1.0f, false, m_VFXContext);
    }
    if (!out.summons.empty()) AudioSystem::Get().PlayBurst("boss_slam", (uint32_t)out.summons.size());

    // ---- 当たり ----
    for (const BossAttacks::Hit& h : out.hits)
    {
        if (h.ranged)
        {
            const bool rock = (h.kind == BossMove::RockRain);
            m_AreaVFX.Play(rock ? "BossRockHit.json" : "BossSlam.json", h.center, rock ? 1.2f : 2.0f, false, m_VFXContext);
            AudioSystem::Get().Play("boss_slam");
            if (!rock) m_Camera.OnShakeAreas(1);
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
            // 衝撃波・突進：BossAttacks が当たりを決めた
            if (IsPlayerDead()) continue;
            if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;
            ++m_BossSlamHits;
            PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, h.dir, true);
            if (h.kind == BossMove::Charge)
            {
                AudioSystem::Get().Play("boss_slam");
                m_Camera.OnShakeAreas(1);
            }
        }
    }

    // ---- 警告の輪・衝撃波の帯・突進の道筋を GPU の描画へ ----
    SwarmSystem::WarnCircle circles[SwarmSystem::kMaxWarnCircles];
    int n = 0;
    for (const BossAttacks::Visual& v : m_BossAttacks.Visuals())
    {
        if (n >= SwarmSystem::kMaxWarnCircles) break;
        circles[n].center = v.center;
        circles[n].radius = v.radius;
        circles[n].progress = v.progress;
        circles[n].band = v.band;
        ++n;
    }
    m_Swarm.SetWarnCircles(circles, n);
}
