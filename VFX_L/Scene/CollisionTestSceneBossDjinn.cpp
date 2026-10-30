// ============================================================
// CollisionTestSceneBossDjinn.cpp
// 砂漠 3 面目の Boss「砂嵐の魔神」の見た目（2026-10-29。技の拍子と当たりは BossAttacksDjinn.cpp、説明は Enemy/Boss/BossDjinn.h）:
//   ・下半身の砂旋（BossDjinnVortex.json、ループ）：Boss が居る間ずっと足元に付く + 小さな風源（草・砂屑・粒子が回る）。
//     二段目（降りた後）は広く出し直す。砂渡りで沈んでいる間は消す
//   ・砂嵐召喚の砂旋：BossAttacks::Devils() の 1 本毎に砂嵐の術師と同じ砂の柱（SandTornado.json）+ 風源。現れる / ほどけるは SetPresence
//   ・炎の息：吐いている間 BossDjinnBreath.json を体の位置に置き、向きを扇に合わせて回す（RotateInstance は差分）+ 草が扇の向きへ倒れる
//   ・砂渡り：沈む / 浮かぶ瞬間の砂の噴き上げ、地面の下を走る間は砂丘（BossSandMound.json）
//   ・砂の拳：叩き付けた所に岩と砂の噴き上げ・揺れ・波紋
//   ・二段目へ入った瞬間（咆哮と同じ）：足元から大きな砂の噴き上げ
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "VFX_Editor/VFXId.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    constexpr float kDevilVfxScale = 1.25f;   // 砂嵐の術師の砂旋（高さ約 3.5m）より一回り大きく
    constexpr float kVortexPhase2Scale = 1.35f;
}

void CollisionTestScene::ClearBossDjinnFx()
{
    auto stop = [this](uint32_t& h)
        {
            if (h != 0) m_AreaVFX.StopInstance(h);
            h = 0;
        };
    stop(m_DjinnVortexVfx);
    stop(m_DjinnBreathVfx);
    stop(m_DjinnShiftVfx);
    for (DjinnDevilFx& f : m_DjinnDevilFx) stop(f.vfx);
    m_DjinnDevilFx.clear();
    m_DjinnVortexPhase2 = false;
}

void CollisionTestScene::UpdateBossDjinnFx(float dt, const BossAttacks::Input& in, const BossAttacks::Output& out)
{
    (void)dt;
    if (m_BossAttacks.Type() != BossType::Djinn)
    {
        if (m_DjinnVortexVfx != 0 || !m_DjinnDevilFx.empty()) ClearBossDjinnFx();
        return;
    }
    auto groundAt = [this](float x, float z) { return Vector3(x, m_Grid.SampleHeight(x, z), z); };
    const Vector3 bossGround = groundAt(in.bossPos.x, in.bossPos.z);
    const bool present = m_Stage.IsBossAlive();   // 門から出て来る演出の間も（in.bossAlive は演出の間 false）
    const bool phase2 = m_BossAttacks.DjinnPhase2();

    // ---- 下半身の砂旋 + 風源 ----
    const bool sunk = out.bossBurrow > 0.5f;
    if (present && !sunk)
    {
        if (m_DjinnVortexVfx != 0 && m_DjinnVortexPhase2 != phase2)
        {
            m_AreaVFX.StopInstance(m_DjinnVortexVfx);
            m_DjinnVortexVfx = 0;
        }
        if (m_DjinnVortexVfx == 0)
        {
            m_DjinnVortexVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossDjinnVortex), bossGround, 1.0e5f, false, m_VFXContext);
            if (phase2) m_AreaVFX.ScaleInstance(m_DjinnVortexVfx, kVortexPhase2Scale);
            m_DjinnVortexPhase2 = phase2;
        }
        else
            m_AreaVFX.SetInstance(m_DjinnVortexVfx, bossGround, bossGround);
        Swarm::WindSource w = {};
        w.pos[0] = bossGround.x;
        w.pos[1] = bossGround.y;
        w.pos[2] = bossGround.z;
        w.radius = phase2 ? 7.0f : 5.0f;
        w.swirl = 7.0f;
        w.updraft = 2.5f;
        w.inflow = 1.0f;
        w.fade = 1.0f;
        m_Swarm.AddFrameWindSource(w);
    }
    else if (m_DjinnVortexVfx != 0)
    {
        m_AreaVFX.StopInstance(m_DjinnVortexVfx);
        m_DjinnVortexVfx = 0;
    }

    // ---- 二段目へ（咆哮と同じフレーム）：砂旋の体が崩れて降りる ----
    if (out.djinn.landing)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), bossGround, 2.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 2.0f);
        AudioSystem::Get().Play("boss_impact");
        m_Grass.AddGust(bossGround, 6.0f);
        m_Debris.AddGust(bossGround, 6.0f);
    }

    // ---- 砂嵐召喚の砂旋 ----
    if (out.djinn.devilsSummoned) AudioSystem::Get().Play("cast_gust");
    const std::vector<DjinnDevil>& devils = m_BossAttacks.Devils();
    for (auto it = m_DjinnDevilFx.begin(); it != m_DjinnDevilFx.end();)
    {
        bool alive = false;
        for (const DjinnDevil& d : devils) alive = alive || (d.id == it->id);
        if (alive) { ++it; continue; }
        if (it->vfx != 0) m_AreaVFX.StopInstance(it->vfx);
        it = m_DjinnDevilFx.erase(it);
    }
    for (const DjinnDevil& d : devils)
    {
        DjinnDevilFx* fx = nullptr;
        for (DjinnDevilFx& f : m_DjinnDevilFx) if (f.id == d.id) fx = &f;
        if (!fx)
        {
            DjinnDevilFx f;
            f.id = d.id;
            f.vfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::SandTornado), d.pos, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.ScaleInstance(f.vfx, kDevilVfxScale);
            m_DjinnDevilFx.push_back(f);
            fx = &m_DjinnDevilFx.back();
        }
        m_AreaVFX.SetInstance(fx->vfx, d.pos, d.pos);
        m_AreaVFX.SetPresence(fx->vfx, d.presence, d.leaving);
        Swarm::WindSource w = {};
        w.pos[0] = d.pos.x;
        w.pos[1] = d.pos.y;
        w.pos[2] = d.pos.z;
        w.radius = 4.0f;
        w.swirl = 12.0f;
        w.updraft = 4.0f;
        w.inflow = 1.5f;
        w.fade = d.presence;
        m_Swarm.AddFrameWindSource(w);
    }

    // ---- 炎の息：体の位置（浮いた高さ）に置き、扇の向きへ回す ----
    if (out.djinn.breathOn)
    {
        const Vector3 base = bossGround + Vector3(0.0f, out.djinn.hover, 0.0f);
        const float yawDeg = DirectX::XMConvertToDegrees(out.djinn.breathYaw);
        if (m_DjinnBreathVfx == 0)
        {
            m_DjinnBreathVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossDjinnBreath), base, 1.0e5f, false, m_VFXContext);
            m_AreaVFX.RotateInstance(m_DjinnBreathVfx, yawDeg);
            m_DjinnBreathYawDeg = yawDeg;
            AudioSystem::Get().Play("elite_flame");
        }
        else
        {
            m_AreaVFX.SetInstance(m_DjinnBreathVfx, base, base);
            m_AreaVFX.RotateInstance(m_DjinnBreathVfx, yawDeg - m_DjinnBreathYawDeg);
            m_DjinnBreathYawDeg = yawDeg;
        }
        const Vector2 dir(std::sin(out.djinn.breathYaw), std::cos(out.djinn.breathYaw));
        m_Grass.AddGust(bossGround, 2.5f, dir, m_BossAttacks.djinn.breathRadius);
    }
    else if (m_DjinnBreathVfx != 0)
    {
        m_AreaVFX.StopInstance(m_DjinnBreathVfx);
        m_DjinnBreathVfx = 0;
    }

    // ---- 砂渡り ----
    if (out.djinn.shiftSank)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), bossGround, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 1.3f);
        AudioSystem::Get().Play("boss_slam");
        m_Debris.AddGust(bossGround, 3.0f);
    }
    if (out.djinn.shiftTravel)
    {
        if (m_DjinnShiftVfx == 0)
            m_DjinnShiftVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandMound), bossGround, 30.0f, false, m_VFXContext);
        else
            m_AreaVFX.SetInstance(m_DjinnShiftVfx, bossGround, bossGround);
    }
    else if (m_DjinnShiftVfx != 0)
    {
        m_AreaVFX.StopInstance(m_DjinnShiftVfx);
        m_DjinnShiftVfx = 0;
    }
    if (out.djinn.shiftRose)
    {
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), bossGround, 2.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(h, 1.6f);
        AudioSystem::Get().Play("boss_impact");
        m_Grass.AddGust(bossGround, 4.0f);
        m_Debris.AddGust(bossGround, 4.0f);
    }

    // ---- 砂の拳 ----
    if (out.djinn.fistSlammed)
    {
        const Vector3 p = out.djinn.fistPos;
        const uint32_t a = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), p, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(a, m_BossAttacks.djinn.fistRadius * 0.55f / 3.0f);   // BossSlam は半径 3m の輪で作った
        const uint32_t b = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSandBurst), p, 2.0f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(b, 1.4f);
        AudioSystem::Get().Play("boss_impact");
        AudioSystem::Get().Play("boss_slam");
        OnExplosions(1);
        m_Grass.AddGust(p, 4.0f);
        m_Debris.AddGust(p, 4.0f);
        m_Water.AddRipple(p, 2.4f, m_Water.GetSettings().blastStrength, true);
    }
}
