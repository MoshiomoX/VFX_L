// ============================================================
// CollisionTestSceneBossKing.cpp
// 遺跡 3 面目の Boss「骸の王」の見た目と足元の効き（2026-10-29。技の拍子と当たりは BossAttacksKing.cpp、説明は Enemy/Boss/BossKing.h）:
//   ・足元の闇（BossKingMist.json、ループ）：Boss が居る間ずっと付く黒い水溜まりと靄・紫の火の粉 + 弱い風源（靄が渦を巻く）
//   ・溜め（呪いの玉・大起尸・呪いの床の始め）：死霊術師の詠唱の光を大きく + 咆哮
//   ・呪いの玉：BossAttacks::KingOrbs() の 1 つ毎に司書の暗い紫の玉（EliteCurseOrb.json、大きく）、爆ぜたら EliteCurseOrbBlast
//   ・大起尸：MobSpawner::RaiseCorpsesAround（印と骸骨の起き上がりは死霊術師と同じ道 = CollisionTestSceneRuins.cpp が出す）
//   ・影へ消える：沈む / 浮かぶ瞬間に影の剣士の黒い煙を大きく（走る間は足元の闇がそのまま付いて行く）
//   ・呪いの床：BossCurseFloor.json + 中で地面に居るプレイヤーは遅く・中心へ（PlayerStateComponent::groundSlow / groundPull）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "Component/RigidbodyComponent.h"
#include "Player/PlayerStateComponent.h"
#include "VFX_Editor/VFXId.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kOrbVfxScale = 1.4f;   // 司書の玉（エリート）より一回り大きく
}

void CollisionTestScene::ClearBossKingFx()
{
    if (m_KingMistVfx != 0) m_AreaVFX.StopInstance(m_KingMistVfx);
    m_KingMistVfx = 0;
    for (DjinnDevilFx& f : m_KingOrbFx)
        if (f.vfx != 0) m_AreaVFX.StopInstance(f.vfx);
    m_KingOrbFx.clear();
}

void CollisionTestScene::UpdateBossKingFx(float dt, const BossAttacks::Input& in, const BossAttacks::Output& out)
{
    (void)dt;
    if (m_BossAttacks.Type() != BossType::BoneKing)
    {
        if (m_KingMistVfx != 0 || !m_KingOrbFx.empty()) ClearBossKingFx();
        return;
    }
    auto groundAt = [this](float x, float z) { return Vector3(x, m_Grid.SampleHeight(x, z), z); };
    const Vector3 bossGround = groundAt(in.bossPos.x, in.bossPos.z);
    const bool present = m_Stage.IsBossAlive();

    // ---- 足元の闇（影へ消える で走る間も付いて行く = 闇の水溜まりだけが滑る）+ 靄を回す弱い風源 ----
    if (present)
    {
        if (m_KingMistVfx == 0)
            m_KingMistVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossKingMist), bossGround, 1.0e5f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_KingMistVfx, bossGround, bossGround);
        Swarm::WindSource w = {};
        w.pos[0] = bossGround.x;
        w.pos[1] = bossGround.y;
        w.pos[2] = bossGround.z;
        w.radius = 4.5f;
        w.swirl = 3.0f;
        w.updraft = 0.8f;
        w.inflow = 0.5f;
        w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);
    }
    else if (m_KingMistVfx != 0)
    {
        m_AreaVFX.StopInstance(m_KingMistVfx);
        m_KingMistVfx = 0;
    }

    // ---- 溜め：詠唱の光（死霊術師の物を大きく）----
    if (out.king.casting)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::RuinsNecroCast), bossGround, 1.6f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 2.6f);
        AudioSystem::Get().Play("necro_cast");
    }

    // ---- 呪いの玉 ----
    if (out.king.orbsLaunched) AudioSystem::Get().Play("elite_orb");
    const std::vector<KingOrb>& orbs = m_BossAttacks.KingOrbs();
    for (auto it = m_KingOrbFx.begin(); it != m_KingOrbFx.end();)
    {
        bool alive = false;
        for (const KingOrb& o : orbs) alive = alive || (o.id == it->id);
        if (alive) { ++it; continue; }
        if (it->vfx != 0) m_AreaVFX.StopInstance(it->vfx);
        it = m_KingOrbFx.erase(it);
    }
    for (const KingOrb& o : orbs)
    {
        DjinnDevilFx* fx = nullptr;
        for (DjinnDevilFx& f : m_KingOrbFx) if (f.id == o.id) fx = &f;
        if (!fx)
        {
            DjinnDevilFx f;
            f.id = o.id;
            f.vfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteCurseOrb), o.pos, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.ScaleInstance(f.vfx, kOrbVfxScale);
            m_KingOrbFx.push_back(f);
            fx = &m_KingOrbFx.back();
        }
        m_AreaVFX.SetInstance(fx->vfx, o.pos, o.pos);
    }
    for (const KingOutput::Blast& b : out.king.orbBlasts)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteCurseOrbBlast), b.pos, 1.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, b.touched ? kOrbVfxScale : 0.8f);
        AudioSystem::Get().Play("elite_orb_blast", b.touched ? 1.0f : 0.5f);
    }

    // ---- 大起尸：プレイヤーの周りの死体から骸骨（足りなければ周りの地面から）----
    if (out.king.raise)
    {
        const KingTune& k = m_BossAttacks.king;
        const int n = m_Mobs.RaiseCorpsesAround(out.king.raiseCenter, k.raiseRadius, k.raiseMax, k.raiseMin, k.raiseFillMin, k.raiseFillMax, m_Grid);
        m_KingRaised += (uint32_t)n;
        AudioSystem::Get().Play("boss_summon");
    }

    // ---- 影へ消える：沈む / 浮かぶ瞬間の黒い煙 ----
    if (out.djinn.shiftSank || out.djinn.shiftRose)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteShadeBlink), bossGround, 1.2f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 2.8f);
        AudioSystem::Get().Play(out.djinn.shiftSank ? "armor_fall" : "armor_rise");
        m_Debris.AddGust(bossGround, 3.5f);
    }

    // ---- 呪いの床：見た目 + 中のプレイヤーへの効き ----
    if (out.king.curseStarted)
    {
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossCurseFloor), out.king.cursePos,
            m_BossAttacks.king.curseWindup + m_BossAttacks.king.curseDuration, false, m_VFXContext);
        AudioSystem::Get().Play("cast_poison");
    }
    const KingCurse& curse = m_BossAttacks.KingCurseField();
    if (curse.active && !IsPlayerDead() && m_Registry.Has<PlayerStateComponent>(m_Player))
    {
        bool grounded = true;
        if (m_Registry.Has<RigidbodyComponent>(m_Player)) grounded = m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        float slow = 1.0f;
        Vector2 pull;
        if (m_BossAttacks.CursePull(Vector3(in.player.x, in.playerFeetY, in.player.z), grounded, slow, pull))
        {
            auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            st.groundSlow = (std::min)(st.groundSlow, slow);
            st.groundPullX += pull.x;
            st.groundPullZ += pull.y;
        }
    }
}
