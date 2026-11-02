// ============================================================
// CollisionTestSceneBoss.cpp
// 戦闘シーンの Boss の技（2026-10-07 に CollisionTestScene.cpp から分けた。技が 5 つに増えたので）:
//   BossAttacks の出力 → 被弾（無敵時間・シールド・ノックバックは雑魚と同じ道）・見た目・音・
//   GPU の Boss への突進の指示（BomberCB の bossCharge*）・地面の警告の輪の描画
//   2026-10-24：面毎の Boss（InitBossFx）と砂の守護者の技の見た目（流沙の渦と足元の効き・潜砂の砂丘・砂の槍の石のトゲ）
//   2026-10-29：Boss 毎の技の合図は CollisionTestSceneBossSignals.cpp、狂暴・撃破の演出は CollisionTestSceneBossDrama.cpp へ分けた
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/RigidbodyComponent.h"
#include "Core/Application.h"
#include "Core/DevUI.h"
#include "Enemy/Boss/BossLooks.h"
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
void CollisionTestScene::InitBossFx(BossType type, bool ignoreEnv)
{
    BossType t = type;
    char env[16] = {};
    if (!ignoreEnv && DevUI::kEnabled && GetEnvironmentVariableA("VFXL_BOSS_TYPE", env, sizeof(env)) > 0)
    {
        if (_stricmp(env, "sand") == 0) t = BossType::SandGuardian;
        else if (_stricmp(env, "stone") == 0) t = BossType::StoneGiant;
        else if (_stricmp(env, "scorpion") == 0) t = BossType::Scorpion;
        else if (_stricmp(env, "stag") == 0) t = BossType::Stag;
        else if (_stricmp(env, "knight") == 0) t = BossType::CryptKnight;
        else if (_stricmp(env, "wolf") == 0) t = BossType::Werewolf;
        else if (_stricmp(env, "djinn") == 0) t = BossType::Djinn;
        else if (_stricmp(env, "king") == 0) t = BossType::BoneKing;
        else if (_stricmp(env, "beast") == 0) t = BossType::StoneBeast;
    }
    m_BossAttacks.Configure(t);
    // 墓守の騎士は 1 回倒れると立ち上がる（StageDirector が Reviving で待って同じ所へ湧かせ直す。2026-10-26）
    m_Stage.bossLives = (t == BossType::CryptKnight) ? 2 : 1;
    // 骸の王は門ではなく真ん中に湧いて動かない（登場の歩きも無し = 闇から這い出てその場で吠える。2026-10-29）
    m_Stage.bossAtCenter = (t == BossType::BoneKing);
    m_BossIntro.walkSpeed = (t == BossType::BoneKing) ? 0.0f : 3.0f;
    BossLooks::Apply(t, Application::Get().GetGraphics().GetDevice(), m_Swarm);   // 皮と飾り（2026-10-24）
    EnemyLooks::ApplyRuins(Application::Get().GetGraphics().GetDevice(), m_Swarm);   // 遺跡の敵の皮と飾り（2026-10-25）
    SetupSandSpikes(m_SandSpikes);
    m_SandSpikes.Clear();
    m_BossDelayedVfx.clear();
    if (m_BossMoundVfx != 0) m_AreaVFX.StopInstance(m_BossMoundVfx);
    m_BossMoundVfx = 0;
    if (m_BossAuraVfx != 0) m_AreaVFX.StopInstance(m_BossAuraVfx);
    m_BossAuraVfx = 0;
    if (m_BossRageVfx != 0) m_AreaVFX.StopInstance(m_BossRageVfx);   // 狂暴の火の粉・倒した後の波（2026-10-29）
    m_BossRageVfx = 0;
    m_DefeatWaveR = -1.0f;
    m_DefeatSlowLeft = 0.0f;
    ClearBossDjinnFx();   // 砂嵐の魔神の砂旋・息（2026-10-29）
    ClearBossKingFx();    // 骸の王の闇・玉
    ClearBossBeastFx();   // 石の守護獣の土埃・息・石化
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

    // 門から呼んだ時の登場の演出（2026-10-27）。その間は技を出さない（BossAttacks には居ない扱い = 歩き終わってから最初の溜めを数える）
    const BossIntro::Frame intro = UpdateBossIntro(dt);

    BossAttacks::Input in;
    in.dt = dt;
    in.bossAlive = m_Stage.IsBossAlive() && !intro.active;
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
    bomber.bossChargePose = out.chargePose;   // 雷鳴の雄鹿の突進の姿勢（VS が頭・胴・脚を動かす。2026-10-26）
    bomber.bossLeanPivotY = m_BossAttacks.leanPivotY;   // 前傾の支点と角度（Boss 毎）
    bomber.bossLeanPivotZ = m_BossAttacks.leanPivotZ;
    bomber.bossLeanAngle = m_BossAttacks.leanAngle;
    bomber.bossScale = m_BossAttacks.bodyScale;          // 体の大きさ（Boss 毎。2026-10-27）
    bomber.bossJumpHeight = out.jumpHeight;             // 狼人の飛びかかりの放物線（技の間だけ > 0）
    bomber.bossNoContact = out.noContact ? 1.0f : 0.0f; // 飛びかかりの間は体で殴らない
    bomber.bossHover = out.djinn.hover;                 // 砂嵐の魔神（2026-10-29）：浮き・飛び・間合い（他の Boss は 0）
    // 降りている途中も飛んでいる扱い（高さは MoveCS が hover のまま置く。切ると落下になって一瞬で落ちる）
    bomber.bossFly = (out.djinn.fly || out.djinn.hover > 0.01f) ? 1.0f : 0.0f;
    bomber.bossKeepMin = out.djinn.keepMin;
    bomber.bossKeepMax = out.djinn.keepMax;
    bomber.bossWing = out.beast.wing;                   // 石の守護獣（2026-10-29）：翼の角度・尾の薙ぎ払いの一回転（他の Boss は 0）
    bomber.bossSpin = out.beast.spin;
    ApplyBossIntroSteer(intro);                          // 登場の演出の間は門をくぐって歩く・殴らない（上の指示を上書き）

    UpdateBossSandFx(dt, in, out);
    UpdateBossDjinnFx(dt, in, out);   // 砂嵐の魔神（CollisionTestSceneBossDjinn.cpp。2026-10-29）
    UpdateBossKingFx(dt, in, out);    // 骸の王（CollisionTestSceneBossKing.cpp）
    UpdateBossBeastFx(dt, in, out);   // 石の守護獣（CollisionTestSceneBossBeast.cpp）
    UpdateBossDrama(dt, in, out);   // 狂暴の咆哮・倒した瞬間（CollisionTestSceneBossDrama.cpp。2026-10-29）

    // ---- 合図（音・見た目）----
    if (out.started == BossMove::Shockwave || out.started == BossMove::Charge || out.started == BossMove::Summon
        || out.started == BossMove::SandSpear || out.started == BossMove::Quicksand || out.started == BossMove::ClawSweep
        || out.started == BossMove::AntlerToss || out.started == BossMove::AntlerBolt
        || out.started == BossMove::DustDevils || out.started == BossMove::DjinnBreath || out.started == BossMove::SandFist
        || out.started == BossMove::PetrifyBreath || out.started == BossMove::TailSweep || out.started == BossMove::BeastDive)
        AudioSystem::Get().Play("boss_summon");   // 溜めの咆哮
    UpdateBossMoveSignals(in, out, player);   // Boss 毎の技の合図（CollisionTestSceneBossSignals.cpp）
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
        m_Water.AddRipple(out.wavePos, 2.4f, m_Water.GetSettings().blastStrength, true);   // 水たまりの上なら大きな波紋（2026-10-27）
    }
    if (out.chargeGo) AudioSystem::Get().Play("boss_slam_warn");
    if (out.thunderStruck) AudioSystem::Get().Play("elite_thunder_warn");   // 雄鹿の道筋の雷の予告（精鋭の雷光と同じ音）

    // ---- 召喚：輪から雑魚・自爆兵（今の難度の HP・速さ）。同時上限（spawnCap）がいっぱいなら出ない（2026-10-26）。土煙は死んだ時の砕けと同じ物 ----
    for (const BossAttacks::Output::Summon& s : out.summons)
    {
        if ((int)m_Swarm.GetCounters().aliveEnemies >= m_Mobs.Director().spawnCap) break;
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
            const bool thunder = h.thunder;                    // 雷鳴の雄鹿の道筋の雷（2026-10-26）：精鋭の雷光と同じ見た目・音
            // 見た目の json は 雷 半径 1.5 / 岩 2 / 重撃 3 の大きさで作ってあるので、当たりの半径に合わせて拡げる
            // （2026-10-29 に当たりを ×1.2 した時、石・雷が輪より小さく見えないように）
            if (thunder)
            {
                const uint32_t fx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteThunder), h.center, 1.0f, false, m_VFXContext);
                m_AreaVFX.ScaleInstance(fx, h.radius / 1.5f);
                AudioSystem::Get().Play("elite_thunder");
                m_Grass.AddGust(h.center, h.radius);
            }
            else if (!burst)
            {
                const uint32_t fx = m_AreaVFX.Play(VFXDatabase::GetFile(rock ? VFXId::BossRockHit : VFXId::BossSlam), h.center, rock ? 1.2f : 2.0f, false, m_VFXContext);
                m_AreaVFX.ScaleInstance(fx, h.radius / (rock ? 2.0f : 3.0f));
                AudioSystem::Get().Play("boss_slam");
                if (!rock) OnExplosions(1);
                m_Grass.AddGust(h.center, h.radius);   // 重撃 / 投石の着弾でも草が倒れる（2026-10-13）
                m_Debris.AddGust(h.center, h.radius);
                m_Water.AddRipple(h.center, h.radius * 0.6f, m_Water.GetSettings().blastStrength * (rock ? 0.6f : 1.0f), true);
            }
            if (IsPlayerDead()) continue;

            const Vector2 d(player.x - h.center.x, player.z - h.center.z);
            const float feetAbove = in.playerFeetY - h.center.y;
            // 岩・雷は跳んでも届く（kRockReachHeight）、重撃は跳べば避けられる
            if (d.Length() > h.radius || feetAbove > ((rock || thunder) ? kRockReachHeight : kSlamDodgeHeight) || feetAbove < -2.0f)
                continue;
            if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;   // 無敵中
            ++m_BossSlamHits;
            const Vector2 dir = (d.LengthSquared() > 1e-4f) ? d / d.Length() : Vector2(0.0f, 1.0f);
            PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, dir, !rock && !thunder);   // 重撃は爆発の強さ、岩・雷は殴りの強さ
        }
        else
        {
            // 衝撃波・突進・砂の槍：BossAttacks が当たりを決めた
            if (IsPlayerDead()) continue;
            if (!PlayerStateSystem::TryApplyHit(m_Registry, m_Player, h.damage)) continue;
            ++m_BossSlamHits;
            // 砂の槍は下から突き上げる = トゲ玉と同じ高く遠い弾き方、雄鹿の角の振り上げ・魔神の砂旋は砂旋と同じ高い吹き上げ、
            // 魔神の炎の息は殴りの軽い押し（0.25 秒毎に当たる）
            if (!h.noKnock)   // 骸の王の呪いの床のじわじわは押さない（2026-10-29）
                PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, h.dir,
                    h.kind == BossMove::SandSpear ? PlayerControlSystem::KnockKind::Spike
                    : (h.kind == BossMove::AntlerToss || h.kind == BossMove::DustDevils || h.kind == BossMove::WingGust) ? PlayerControlSystem::KnockKind::Wind
                    : (h.kind == BossMove::DjinnBreath || h.kind == BossMove::FeatherShards) ? PlayerControlSystem::KnockKind::Melee : PlayerControlSystem::KnockKind::Blast);
            if (h.kind == BossMove::DustDevils) AudioSystem::Get().Play("boss_impact");
            if (h.kind == BossMove::CurseFloor) AudioSystem::Get().Play("player_hurt");
            if (h.kind == BossMove::AntlerToss) AudioSystem::Get().Play("boss_impact");
            if (h.kind == BossMove::WingGust) AudioSystem::Get().Play("boss_impact");
            if (h.kind == BossMove::FeatherShards) AudioSystem::Get().Play("player_hurt");
            if (h.kind == BossMove::Charge || h.kind == BossMove::ClawSweep || h.kind == BossMove::SandFist
                || h.kind == BossMove::BeastDive || h.kind == BossMove::TailSweep || h.kind == BossMove::SkyCrush)
            {
                AudioSystem::Get().Play("boss_slam");
                OnExplosions(1);
            }
            if (h.kind == BossMove::ClawCombo) AudioSystem::Get().Play("player_hurt");
            if (h.kind == BossMove::Sting)
            {
                // 大サソリの毒針（2026-10-26）：刺さった = 毒。残り秒は BossAttacks が数え、UpdateBossSandFx が遅さと泡を付ける
                m_BossAttacks.ApplyPoison();
                AudioSystem::Get().Play("poison_splash");
                m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::Spore), player, 0.6f, false, m_VFXContext);
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
        c.yaw = v.yaw;       // 大サソリの帯 / 扇（2026-10-26）
        c.shape = v.shape;
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
    const bool scorpion = m_BossAttacks.Type() == BossType::Scorpion;   // 足元に毒の靄（2026-10-26）
    const bool knight = m_BossAttacks.Type() == BossType::CryptKnight;  // 騎士・狼人は常駐の見た目なし
    const bool wolf = m_BossAttacks.Type() == BossType::Werewolf;
    if (in.bossAlive && out.bossBurrow < 0.5f && !knight && !wolf && m_BossAttacks.Type() != BossType::Djinn && m_BossAttacks.Type() != BossType::BoneKing
        && m_BossAttacks.Type() != BossType::StoneBeast)
    {
        if (m_BossAuraVfx == 0)
            m_BossAuraVfx = m_AreaVFX.Play(VFXDatabase::GetFile(sandBoss ? VFXId::BossSandAura : scorpion ? VFXId::BossVenomAura : VFXId::BossStoneAura),
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

    // ---- 大サソリの毒（2026-10-26）：地面に居る間の走りが遅い（PlayerStateComponent::groundSlow は毎フレーム 1 に戻るので
    //      流沙と同じ所で入れる）+ プレイヤーに付く緑の泡（BossVenom.json、ループ）----
    const float poison = m_BossAttacks.PoisonLeft();
    if (poison > 0.0f && !IsPlayerDead())
    {
        if (m_Registry.Has<PlayerStateComponent>(m_Player))
        {
            auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            st.groundSlow = (std::min)(st.groundSlow, m_BossAttacks.stingPoisonSlow);
        }
        if (m_PlayerVenomVfx == 0)
            m_PlayerVenomVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossVenom), in.player, 1.0e5f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_PlayerVenomVfx, in.player, in.player);
    }
    else if (m_PlayerVenomVfx != 0)
    {
        m_AreaVFX.StopInstance(m_PlayerVenomVfx);
        m_PlayerVenomVfx = 0;
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
        m_AreaVFX.ScaleInstance(h, 1.5f * m_BossAttacks.burrowRadius / 3.5f);   // 3.5m の輪の時に 1.5 倍で作った
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
        // トゲの並びの幅・高さは当たりの幅に合わせる（1.1m の時に 1 倍。2026-10-29）
        m_SandSpikes.Spawn(l.start, l.end, m_BossAttacks.spearLaneRadius / 1.1f, [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
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