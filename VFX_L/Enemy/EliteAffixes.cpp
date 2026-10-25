// ============================================================
// EliteAffixes.cpp
// 面の固定エリートの詞缀（2026-10-25、ユーザーが選んだ 4 つ）:
//   疾風：歩く速さ × eliteSwiftMul（GPU の旗）+ 足元の風の残像（ループ）
//   鉄壁：盾が残っている間は受けるダメージ半分（GPU の旗）。HP の割合が shieldBreakRatio を切ったら割れる
//   雷光：thunderInterval 秒毎に thunderCount 本の雷。1 本はプレイヤーの足元、残りは周り。先に地面に印が育つ
//   狂暴：HP の割合が enrageRatio を切ったら大きく・速く・赤く（GPU の旗）、技の間隔 × enrageCooldownMul
// 続くエフェクトは体の位置（リードバック）に付いて動く。隠れている間（影の剣士）は地面の下へ隠す
// ============================================================
#include "Enemy/EliteAttacks.h"
#include "Audio/AudioSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/VFXId.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector3;

void EliteAttacks::UpdateAffixes(Elite& e, const Input& in, const GridWorld& grid)
{
    const Vector3 ground = Ground(grid, e.pos);
    // 隠れている間は続くエフェクトを地面の下へ（止めると出し直しで途切れる）
    const Vector3 at = (e.hide > 0.5f) ? ground - Vector3(0.0f, 60.0f, 0.0f) : ground;
    const float sizeMul = tune[(int)e.type].scale / 2.0f;
    auto follow = [&](uint32_t& h, int id)
    {
        if (!h) h = PlayFx(id, at, 1.0e5f, 0.0f, sizeMul);
        else if (m_Vfx) m_Vfx->SetInstance(h, at, at);
    };

    if (e.affixes & kEliteAffixSwift) follow(e.fxSwift, (int)VFXId::EliteSwift);

    if (e.shieldUp)
    {
        follow(e.fxShield, (int)VFXId::EliteShield);
        if (e.hpRatio < shieldBreakRatio)
        {
            e.shieldUp = false;
            if (e.fxShield && m_Vfx) m_Vfx->StopInstance(e.fxShield);
            e.fxShield = 0;
            PlayFx((int)VFXId::EliteShieldBreak, ground + Vector3(0.0f, 0.2f, 0.0f), 1.0f, 0.0f, sizeMul);
            AudioSystem::Get().Play("elite_shield_break");
            ++m_Stats.shieldBreaks;
        }
    }

    if ((e.affixes & kEliteAffixBerserk) && !e.enraged && e.hpRatio < enrageRatio)
    {
        e.enraged = true;
        PlayFx((int)VFXId::EliteBerserk, ground, 1.5f, 0.0f, sizeMul);
        AudioSystem::Get().Play("elite_berserk");
        ++m_Stats.enrages;
    }
    if (e.enraged) follow(e.fxAura, (int)VFXId::EliteBerserkAura);

    if (e.affixes & kEliteAffixThunder)
    {
        e.thunderT += in.dt;
        if (e.thunderT >= thunderInterval)
        {
            e.thunderT = 0.0f;
            std::uniform_real_distribution<float> u01(0.0f, 1.0f);
            const float dmg = Damage(e, in, thunderDamage);
            int placed = 0;
            const float dx = in.player.x - ground.x, dz = in.player.z - ground.z;
            if (in.playerAlive && dx * dx + dz * dz < 16.0f * 16.0f)
            {
                // 足元の地面に落とす（足の高さだと、跳んでいる間は印が宙に浮き、着地すると当たらなかった）
                m_Strikes.push_back({ Ground(grid, in.player), 0.0f, dmg });
                ++placed;
            }
            for (int tries = 0; placed < thunderCount && tries < thunderCount * 4; ++tries)
            {
                const float a = u01(m_Rng) * 6.2831853f;
                const float r = (0.35f + 0.65f * u01(m_Rng)) * thunderSpread;
                const Vector3 p = ground + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
                int gx = 0, gz = 0;
                grid.WorldToCell(p, gx, gz);
                if (!grid.IsWalkable(gx, gz)) continue;
                m_Strikes.push_back({ Ground(grid, p), 0.0f, dmg });
                ++placed;
            }
            m_Stats.strikes += placed;
            AudioSystem::Get().Play("elite_thunder_warn");
        }
    }
}

void EliteAttacks::UpdateStrikes(const Input& in, const GridWorld& grid)
{
    (void)grid;
    for (size_t i = 0; i < m_Strikes.size();)
    {
        Strike& s = m_Strikes[i];
        s.t += in.dt;
        const float w = (std::max)(thunderWarn, 0.05f);
        m_Warns.push_back({ s.pos, thunderRadius, s.t / w, 0.0f, 0.0f });
        if (s.t >= w)
        {
            if (PlayerNear(in, s.pos, thunderRadius, 2.5f))
                AddHit(s.pos, s.damage, 0);
            PlayFx((int)VFXId::EliteThunder, s.pos, 1.0f);
            AudioSystem::Get().Play("elite_thunder");
            m_Strikes.erase(m_Strikes.begin() + i);
            continue;
        }
        ++i;
    }
}
