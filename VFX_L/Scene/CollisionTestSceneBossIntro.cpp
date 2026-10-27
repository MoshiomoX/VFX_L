// ============================================================
// CollisionTestSceneBossIntro.cpp
// 門から呼んだ Boss の登場の演出（2026-10-27、Enemy/Boss/BossIntro）。
//   UpdateBossIntro    : 門を使ったかを StageDirector から受け取り、演出を 1 コマ進めて合図（赤い渦・揺れ・踏み込み・名前）を出す
//   ApplyBossIntroSteer: 演出の間は BossAttacks の指示の代わりに「門をくぐって歩く / その場で立つ・殴らない」を GPU へ
// UpdateBossAttacks（CollisionTestSceneBoss.cpp）が両方を呼ぶ
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "VFX_Editor/VFXId.h"
#include "Enemy/Boss/BossBrain.h"

using namespace DirectX::SimpleMath;

BossIntro::Frame CollisionTestScene::UpdateBossIntro(float dt)
{
    Vector3 gate, walkDir, spawn;
    if (m_Stage.ConsumeBossIntro(gate, walkDir, spawn))
    {
        if (m_BossIntroVfx) m_AreaVFX.StopInstance(m_BossIntroVfx);
        m_BossIntroVfx = 0;
        m_BossIntro.Start(gate, walkDir, spawn);
    }

    BossIntro::Frame f;
    m_BossIntro.Update(dt, m_Stage.IsBossAlive(), f);

    if (f.rumbleStart)
    {
        // 門の渦（紫）を赤く染めて大きく荒らす。元の渦は門を使った時に消えている（CollisionTestSceneGameplay）
        m_BossIntroVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossPortal), m_Stage.GetPortalCenter(), 1.0e9f, false,
            m_VFXContext, Vector3(1.8f, 0.35f, 0.25f));
        if (m_BossIntroVfx)
        {
            m_AreaVFX.RotateInstance(m_BossIntroVfx, m_Stage.GetPortalYaw());
            m_AreaVFX.ScaleInstance(m_BossIntroVfx, 1.35f);
        }
        AudioSystem::Get().Play("boss_slam_warn");   // 地鳴り
    }
    if (f.shake > 0.0f) m_Camera.Camera().RaiseTrauma(f.shake);
    if (f.emerge)
    {
        // 湧いた瞬間を煙で隠す（影の剣士の消える / 現れる煙を大きく）+ 足元の土煙
        Vector3 p = m_BossIntro.Spawn();
        p.y = m_Grid.SampleHeight(p.x, p.z);
        const uint32_t h = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteShadeBlink), p, 1.5f, false, m_VFXContext,
            Vector3(1.6f, 0.45f, 0.4f));
        if (h) m_AreaVFX.ScaleInstance(h, 2.6f);
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::MobDeath), p, 1.2f, false, m_VFXContext);
        m_Grass.AddGust(p, 4.0f);
        m_Debris.AddGust(p, 4.0f);
    }
    if (f.stomp)
    {
        // 門の前で踏み込む：土煙と石・草が倒れる・揺れ + 名前
        Vector3 p = m_Stage.BossPos();
        p.y = m_Grid.SampleHeight(p.x, p.z);
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), p, 2.0f, false, m_VFXContext);
        AudioSystem::Get().Play("boss_impact");
        AudioSystem::Get().Play("boss_slam");
        m_Grass.AddGust(p, 6.0f);
        m_Debris.AddGust(p, 6.0f);
        m_Stage.SetAnnounce(BossDisplayName(m_BossAttacks.Type()), 2.6f);
    }
    if (f.finished && m_BossIntroVfx)
    {
        m_AreaVFX.StopInstance(m_BossIntroVfx);   // 出ている粒子は寿命まで残る（ふっと消えない）
        m_BossIntroVfx = 0;
    }
    return f;
}

void CollisionTestScene::ApplyBossIntroSteer(const BossIntro::Frame& f)
{
    if (!f.active) return;
    auto& bomber = m_Swarm.GetBomberParams();
    bomber.bossNoContact = 1.0f;   // 出てくる間は体で殴らない
    if (!f.steer) return;
    bomber.bossChargeOn = 1.0f;
    bomber.bossChargeDirX = f.dir.x;
    bomber.bossChargeDirZ = f.dir.y;
    bomber.bossChargeSpeed = f.speed;   // 0 = その場で止まる（突進の溜めと同じ）
    bomber.bossJumpHeight = 0.0f;
    bomber.bossBurrow = 0.0f;
    bomber.bossChargePose = 0.0f;
}
