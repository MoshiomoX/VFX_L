// ============================================================
// CollisionTestSceneBossBeast.cpp
// 遺跡 2 面目の Boss「石の守護獣」の見た目とプレイヤーの石化（2026-10-29。技の拍子と当たりは BossAttacksBeast.cpp、説明は Enemy/Boss/BossBeast.h）:
//   ・飛んでいる間：足元に羽ばたきが巻き上げる土埃（BossBeastDust.json、ループ）+ 外へ吹く風源。翼を打ち下ろす度に足元の草が倒れる
//   ・二段目へ入った瞬間（咆哮と同じ）：地面へ降りる岩と土煙・揺れ
//   ・急降下：走り出しの音、滑空している間の砂煙（精鋭の騎兵の突進と同じ物）、当たった瞬間
//   ・石化の息：吐いている間 BossPetrifyBreath.json を頭の高さに置き、扇の向きへ回す + 草が扇の向きへ倒れる
//   ・翼の突風：打ち下ろした瞬間に前へ大きな突風（突風の魔法の見た目を大きく）+ 風源・草・落ち葉
//   ・尾の薙ぎ払い：回り始めた瞬間に輪の大きさの薙ぎの斬撃（精鋭の大斧の回転斬りと同じ物）
//   ・石化：溜まっている間プレイヤーに灰色の欠片と埃（BossPetrify.json、溜まりに合わせて濃く）+ 地面に居る間の走りが遅い
//     （PlayerStateComponent::groundSlow）。石になった / 解けた瞬間に石の砕け
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Player/PlayerStateComponent.h"
#include "VFX_Editor/VFXId.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kAxeSpinRadius = 4.2f;      // EliteAxeSpin.json を作った輪の半径
    constexpr float kGustFxScale = 3.0f;        // 突風の魔法（半径 3.4m）の見た目を前へ 13m 吹く大きさに
}

void CollisionTestScene::ClearBossBeastFx()
{
    auto stop = [this](uint32_t& h)
        {
            if (h != 0) m_AreaVFX.StopInstance(h);
            h = 0;
        };
    stop(m_BeastDustVfx);
    stop(m_BeastBreathVfx);
    stop(m_BeastDiveVfx);
    stop(m_PlayerPetrifyVfx);
    for (DjinnDevilFx& f : m_BeastShardFx) stop(f.vfx);
    m_BeastShardFx.clear();
}

void CollisionTestScene::UpdateBossBeastFx(float dt, const BossAttacks::Input& in, const BossAttacks::Output& out)
{
    (void)dt;
    // 石化はプレイヤー側の状態（Boss が倒れても少し残る）なので、Boss の種類を見る前に
    const float petrify = out.beast.petrify;
    const float stone = out.beast.stoneLeft;
    if ((petrify > 0.0f || stone > 0.0f) && !IsPlayerDead())
    {
        if (m_Registry.Has<PlayerStateComponent>(m_Player))
        {
            auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            st.groundSlow = (std::min)(st.groundSlow, m_BossAttacks.BeastPetrifySlow());
        }
        if (m_PlayerPetrifyVfx == 0)
            m_PlayerPetrifyVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossPetrify), in.player, 1.0e5f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_PlayerPetrifyVfx, in.player, in.player);
        m_AreaVFX.SetPresence(m_PlayerPetrifyVfx, (stone > 0.0f) ? 1.0f : 0.25f + 0.75f * petrify, false);
    }
    else if (m_PlayerPetrifyVfx != 0)
    {
        m_AreaVFX.StopInstance(m_PlayerPetrifyVfx);
        m_PlayerPetrifyVfx = 0;
    }
    if (out.beast.petrified)
    {
        ++m_BeastPetrified;
        const Vector3 feet(in.player.x, in.playerFeetY, in.player.z);
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockHit), feet, 1.2f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 0.5f);
        AudioSystem::Get().Play("armor_fall");
    }
    if (out.beast.stoneBroke)
    {
        const Vector3 feet(in.player.x, in.playerFeetY, in.player.z);
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockHit), feet, 1.2f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 0.4f);
        AudioSystem::Get().Play("armor_rise");
    }

    if (m_BossAttacks.Type() != BossType::StoneBeast)
    {
        if (m_BeastDustVfx != 0 || m_BeastBreathVfx != 0 || m_BeastDiveVfx != 0 || !m_BeastShardFx.empty())
        {
            const uint32_t keep = m_PlayerPetrifyVfx;   // 石化の見た目は残す
            m_PlayerPetrifyVfx = 0;
            ClearBossBeastFx();
            m_PlayerPetrifyVfx = keep;
        }
        return;
    }
    auto groundAt = [this](float x, float z) { return Vector3(x, m_Grid.SampleHeight(x, z), z); };
    const Vector3 bossGround = groundAt(in.bossPos.x, in.bossPos.z);
    const bool present = m_Stage.IsBossAlive();   // 門から出て来る演出の間も（in.bossAlive は演出の間 false）
    const float hover = out.djinn.hover;

    // ---- 飛んでいる間：足元の土埃 + 外へ吹く風源、翼を打ち下ろす度に足元の草が倒れる ----
    if (present && out.beast.flying && hover > 0.3f)
    {
        if (m_BeastDustVfx == 0)
            m_BeastDustVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossBeastDust), bossGround, 1.0e5f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_BeastDustVfx, bossGround, bossGround);
        m_AreaVFX.SetPresence(m_BeastDustVfx, std::clamp(1.4f - hover / 4.0f, 0.3f, 1.0f), false);   // 低いほど濃い
        Swarm::WindSource w = {};
        w.pos[0] = bossGround.x;
        w.pos[1] = bossGround.y;
        w.pos[2] = bossGround.z;
        w.radius = 6.0f;
        w.swirl = 1.5f;
        w.updraft = 0.5f;
        w.inflow = -2.0f;   // 外へ（羽ばたきの下向きの風が地面で広がる）
        w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);
        if (m_BeastPrevWing > 0.25f && out.beast.wing <= 0.25f)
        {
            m_Grass.AddGust(bossGround, 5.0f);
            m_Debris.AddGust(bossGround, 5.0f);
        }
    }
    else if (m_BeastDustVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BeastDustVfx);
        m_BeastDustVfx = 0;
    }
    m_BeastPrevWing = out.beast.wing;

    // ---- 二段目へ（咆哮と同じフレーム）：地面へ降りる ----
    if (out.beast.landing)
    {
        const uint32_t a = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), bossGround, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(a, 1.6f);
        const uint32_t b = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockHit), bossGround, 1.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(b, 2.0f);
        AudioSystem::Get().Play("boss_impact");
        OnExplosions(1);
        m_Grass.AddGust(bossGround, 7.0f);
        m_Debris.AddGust(bossGround, 7.0f);
        m_Water.AddRipple(bossGround, 3.0f, m_Water.GetSettings().blastStrength, true);
    }

    // ---- 急降下 ----
    if (out.beast.diveGo)
    {
        AudioSystem::Get().Play("elite_dash");
        AudioSystem::Get().Play("cast_gust");
    }
    if (out.beast.diving)
    {
        if (m_BeastDiveVfx == 0)
        {
            m_BeastDiveVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteRiderDash), bossGround, 30.0f, false, m_VFXContext);
            m_AreaVFX.ScaleInstance(m_BeastDiveVfx, 1.6f);
        }
        else
            m_AreaVFX.SetInstance(m_BeastDiveVfx, bossGround, bossGround);
        m_Grass.AddGust(bossGround, 3.5f);
    }
    else if (m_BeastDiveVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BeastDiveVfx);
        m_BeastDiveVfx = 0;
    }
    if (out.beast.diveHit) AudioSystem::Get().Play("boss_impact");

    // ---- 石化の息：頭の高さ（浮きの上）に置き、扇の向きへ回す ----
    if (out.beast.breathOn)
    {
        const Vector3 base = bossGround + Vector3(0.0f, hover, 0.0f);
        const float yawDeg = DirectX::XMConvertToDegrees(out.beast.breathYaw);
        if (m_BeastBreathVfx == 0)
        {
            m_BeastBreathVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossPetrifyBreath), base, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.RotateInstance(m_BeastBreathVfx, yawDeg);
            m_BeastBreathYawDeg = yawDeg;
            AudioSystem::Get().Play("cast_gust");
        }
        else
        {
            m_AreaVFX.SetInstance(m_BeastBreathVfx, base, base);
            m_AreaVFX.RotateInstance(m_BeastBreathVfx, yawDeg - m_BeastBreathYawDeg);
            m_BeastBreathYawDeg = yawDeg;
        }
        const Vector2 dir(std::sin(out.beast.breathYaw), std::cos(out.beast.breathYaw));
        m_Grass.AddGust(bossGround, 2.5f, dir, m_BossAttacks.beast.breathRadius);
    }
    else if (m_BeastBreathVfx != 0)
    {
        m_AreaVFX.StopInstance(m_BeastBreathVfx);
        m_BeastBreathVfx = 0;
    }

    // ---- 翼の突風：前へ大きな突風 + 風源・草・落ち葉 ----
    if (out.beast.gustBeat)
    {
        const Vector2 d = out.beast.gustDir;
        const float r = m_BossAttacks.beast.gustRadius;
        const Vector3 front = bossGround + Vector3(d.x, 0.0f, d.y) * 1.5f;
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::Gust), front, 1.2f, false, m_VFXContext);
        m_AreaVFX.RotateInstance(h, DirectX::XMConvertToDegrees(std::atan2(d.x, d.y)));
        m_AreaVFX.ScaleInstance(h, kGustFxScale);
        AddGustWindBurst(front, d, r, r * 0.5f);
        m_Grass.AddGust(bossGround, 3.0f, d, r);
        m_Debris.AddGust(bossGround, 3.0f, d, r);
        AudioSystem::Get().Play("cast_gust");
        AudioSystem::Get().Play("boss_slam");
    }

    // ---- 天からの圧殺（10-29 追加）：舞い上がる / 落ち始める音、叩き潰した所の岩と土（衝撃波の見た目・音・揺れは共通の waveLaunched）----
    if (out.beast.crushRose)
    {
        AudioSystem::Get().Play("cast_gust");
        m_Grass.AddGust(bossGround, 6.0f);
        m_Debris.AddGust(bossGround, 6.0f);
    }
    if (out.beast.crushFalling) AudioSystem::Get().Play("boss_slam_warn");
    if (out.beast.crushImpact)
    {
        const Vector3 p = out.beast.crushPos;
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockHit), p, 1.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, m_BossAttacks.beast.crushRadius / 2.0f);   // BossRockHit は半径 2m の輪で作った
        AudioSystem::Get().Play("boss_slam");
        m_Grass.AddGust(p, m_BossAttacks.beast.crushRadius + 2.0f);
        m_Debris.AddGust(p, m_BossAttacks.beast.crushRadius + 2.0f);
    }

    // ---- 石羽の散弾（10-29 追加）：羽根 1 枚毎に石のトゲ（BossStoneShard.json、飛ぶ向きへ回す）、砕けた所に小さな石の砕け ----
    if (out.beast.shardsFired)
    {
        AudioSystem::Get().Play("cast_stone");
        AudioSystem::Get().Play("cast_gust");
    }
    const std::vector<BeastShard>& shards = m_BossAttacks.BeastShards();
    for (auto it = m_BeastShardFx.begin(); it != m_BeastShardFx.end();)
    {
        bool alive = false;
        for (const BeastShard& s : shards) alive = alive || (s.id == it->id);
        if (alive) { ++it; continue; }
        if (it->vfx != 0) m_AreaVFX.StopInstance(it->vfx);
        it = m_BeastShardFx.erase(it);
    }
    for (const BeastShard& s : shards)
    {
        DjinnDevilFx* fx = nullptr;
        for (DjinnDevilFx& f : m_BeastShardFx) if (f.id == s.id) fx = &f;
        if (!fx)
        {
            DjinnDevilFx f;
            f.id = s.id;
            f.vfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossStoneShard), s.pos, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.RotateInstance(f.vfx, DirectX::XMConvertToDegrees(std::atan2(s.dir.x, s.dir.y)));
            m_BeastShardFx.push_back(f);
            fx = &m_BeastShardFx.back();
        }
        m_AreaVFX.SetInstance(fx->vfx, s.pos, s.pos);
    }
    for (const Vector3& p : out.beast.shardEnds)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossRockHit), p, 1.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 0.35f);
    }
    if (!out.beast.shardEnds.empty()) AudioSystem::Get().PlayBurst("hit_stone", (uint32_t)out.beast.shardEnds.size());

    // ---- 尾の薙ぎ払い：輪の大きさの薙ぎ ----
    if (out.beast.sweepStart)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteAxeSpin), bossGround, 1.2f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, m_BossAttacks.beast.sweepRadius / kAxeSpinRadius);
        AudioSystem::Get().Play("elite_axe");
        m_Grass.AddGust(bossGround, m_BossAttacks.beast.sweepRadius);
        m_Debris.AddGust(bossGround, m_BossAttacks.beast.sweepRadius);
    }
}
