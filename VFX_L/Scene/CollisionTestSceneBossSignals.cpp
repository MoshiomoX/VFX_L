// ============================================================
// CollisionTestSceneBossSignals.cpp
// Boss 毎の技の合図（音・見た目）：大サソリの突っ込み / 鋏、墓守の騎士の双鎖（鎖を投げる）と復活、月下の狼人の跳び / 爪 / 遠吠え、
// 雷鳴の雄鹿の角の振り上げ / 角の雷撃。2026-10-29 に CollisionTestSceneBoss.cpp（509 行）から分けた（中身は変えていない）。
// 共通の技の合図（咆哮・輪・衝撃波・召喚）は CollisionTestSceneBoss.cpp、狂暴・撃破の演出は CollisionTestSceneBossDrama.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Audio/AudioSystem.h"
#include "VFX_Editor/VFXId.h"

using namespace DirectX::SimpleMath;

void CollisionTestScene::UpdateBossMoveSignals(const BossAttacks::Input& in, const BossAttacks::Output& out, const Vector3& player)
{
    // 大サソリ（2026-10-26）：突っ込み始め = 突撃の音、鋏を振った = 斧の風切り
    if (out.stingLunge) AudioSystem::Get().Play("elite_dash");
    if (out.clawSwung) AudioSystem::Get().Play("elite_axe");
    // 墓守の騎士の双鎖（2026-10-26）：鎖の番人と同じ鎖（CollisionTestSceneRuins.cpp が飛ばす・当てる・引く）を Boss の手の高さから
    for (const BossAttacks::Output::ChainThrow& t : out.chainThrows)
    {
        if ((int)m_Chains.size() >= 4) break;
        ChainHook c;
        c.origin = t.pos + Vector3(0.0f, 1.1f, 0.0f);
        c.dir = Vector3(std::sin(t.yaw), 0.0f, std::cos(t.yaw));
        c.reach = m_BossAttacks.chainRange;
        c.pullStop = m_BossAttacks.chainPullStop;
        c.damage = m_BossAttacks.chainDamage * in.damageMul;
        c.boss = true;
        c.hitRadius = m_BossAttacks.chainWidth * 0.5f;   // 予告の帯の半幅と同じ（2026-10-29）
        m_Chains.push_back(c);
    }
    if (!out.chainThrows.empty()) AudioSystem::Get().Play("chain_throw");
    // 月下の狼人（2026-10-26）：跳び = 突撃の音、爪 = 斬撃の音と影の剣士の斬撃の見た目（Boss の前）、遠吠え = 咆哮
    if (out.pounceJump) AudioSystem::Get().Play("elite_dash");
    if (out.wolfSlash)
    {
        AudioSystem::Get().Play("elite_slash");
        const Vector3 p = in.bossPos + Vector3(out.clawDir.x, 0.3f, out.clawDir.y) * 2.2f;
        m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::EliteShadeSlash), p, 0.8f, false, m_VFXContext);
    }
    if (out.howled)
    {
        AudioSystem::Get().Play("boss_summon");
        m_Stage.SetAnnounce(L"狼人の遠吠え！　群れが来る", 2.0f);
    }
    // 雷鳴の雄鹿（2026-10-29）：振り上げ = 角ですくった土・石 + 風切り、雷撃 = 溜めの音 → 角から帯の先まで稲妻 + 先に落雷
    if (out.antlerTossed)
    {
        AudioSystem::Get().Play("elite_axe");
        const Vector2 d = out.clawDir;
        const Vector3 front = in.bossPos + Vector3(d.x, 0.0f, d.y) * 3.0f;
        const Vector3 g(front.x, m_Grid.SampleHeight(front.x, front.z), front.z);
        const uint32_t fx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossSlam), g, 1.5f, false, m_VFXContext);
        m_AreaVFX.ScaleInstance(fx, 0.6f);
        m_Grass.AddGust(g, 2.5f, d, m_BossAttacks.tossRadius);
        m_Debris.AddGust(g, 2.5f, d, m_BossAttacks.tossRadius);
    }
    if (out.boltCharging) AudioSystem::Get().Play("beam_charge");
    if (!out.boltLines.empty())
    {
        // 角の高さ（体格 3 倍で地面から約 5m、胴の中心から前へ約 3.5m）から帯の先の地面へ
        constexpr float kAntlerUp = 5.0f, kAntlerFwd = 3.5f;
        for (const BossAttacks::Output::BoltLine& l : out.boltLines)
        {
            Vector2 d(l.end.x - l.start.x, l.end.z - l.start.z);
            const float len = d.Length();
            if (len > 1e-3f) d /= len;
            const Vector3 antler = l.start + Vector3(d.x * kAntlerFwd, kAntlerUp, d.y * kAntlerFwd);
            const uint32_t bolt = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossStagBolt), antler, 0.4f, false, m_VFXContext);
            m_AreaVFX.SetInstance(bolt, antler, l.end);
            // 先の火花はアークボルトの命中を大きく（精鋭の落雷 EliteThunder は HDR が強く、5 本同時だと画面が白く飛んだ）
            const uint32_t spark = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::ArcBoltHit), l.end, 0.6f, false, m_VFXContext);
            m_AreaVFX.ScaleInstance(spark, 2.0f);
            m_Grass.AddGust(l.start, m_BossAttacks.boltWidth, d, len);   // 帯に沿って草が倒れる
        }
        AudioSystem::Get().Play("elite_thunder");
        OnExplosions(1);
    }
    // 復活（StageDirector が Reviving で待つ）：倒れた所に崩れた鎧の山、立ち上がる時に紫の炎。蘇った後は狂暴（間が短く連技）
    {
        Vector3 fell;
        if (m_Stage.ConsumeBossFell(fell))
        {
            const Vector3 g(fell.x, m_Grid.SampleHeight(fell.x, fell.z), fell.z);
            m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::RuinsArmorFall), g, m_Stage.bossReviveDelay, false, m_VFXContext);
            AudioSystem::Get().Play("armor_fall");
            m_Stage.SetAnnounce(L"墓守の騎士が崩れ落ちた……", 2.0f);
        }
        if (m_Stage.ConsumeBossRevived())
        {
            const Vector3 p = in.bossPos.LengthSquared() > 0.01f ? in.bossPos : player;
            m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::RuinsArmorRise), Vector3(p.x, m_Grid.SampleHeight(p.x, p.z), p.z), 1.5f, false, m_VFXContext);
            AudioSystem::Get().Play("armor_rise");
            AudioSystem::Get().Play("boss_summon");
            m_Stage.SetAnnounce(L"墓守の騎士が立ち上がった！", 2.5f);
            OnExplosions(1);
        }
        m_BossAttacks.forceEnraged = m_Stage.BossLivesUsed() > 0;
        if (m_BossAttacks.Type() == BossType::CryptKnight)
            m_BossAttacks.bodyTint = (m_Stage.BossLivesUsed() > 0) ? Vector3(0.85f, 0.65f, 1.35f) : Vector3(1.0f, 1.0f, 1.0f);   // 蘇った鎧は紫がかる
        else if (m_BossAttacks.Type() == BossType::Werewolf)
            m_BossAttacks.bodyTint = m_BossAttacks.IsEnraged() ? Vector3(1.35f, 0.55f, 0.5f) : Vector3(1.0f, 1.0f, 1.0f);   // 狂暴の狼人は赤く
    }
}
