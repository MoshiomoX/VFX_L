// ============================================================
// EliteMovesRuins.cpp
// 遺跡の固定エリートの技（影の剣士 / 呪いの司書と追う玉）。段の流れの説明は EliteMoves.cpp。2026-10-26 に EliteMoves.cpp から分けた
// ============================================================
#include "Enemy/Elite/EliteAttacks.h"
#include "Enemy/Elite/EliteMovesCommon.h"
#include "Audio/AudioSystem.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/VFXId.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;
using namespace EliteMovesDetail;

// ============================================================
// 影の剣士：消える（段 1）→ 隠れたままプレイヤーの背後へ（段 4。行き先に輪）→ 現れて斬る（段 2）
// ============================================================
void EliteAttacks::MoveShade(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::Shade];
    constexpr float kVanish = 0.4f;
    constexpr float kAppear = 0.3f;
    e.t += in.dt;
    e.flags |= Swarm::kEliteFlagNoContact;
    if (e.phase == 1)
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.hide = (std::min)(e.t / kVanish, 1.0f);
        if (e.t >= kVanish)
        {
            // 行き先：プレイヤーの背中側 extra m。歩けない・段が違う時は斜め後ろ・横・自分の側の順に試す
            const Vector2 back = -DirOf(in.playerYaw);
            const Vector2 side(back.y, -back.x);
            const Vector2 tries[] = { back, (back + side) * 0.7071f, (back - side) * 0.7071f, side, -side, FlatTo(in.player, e.pos) };
            const float feet = in.playerFeetY;
            Vector3 pick = Ground(grid, e.pos);
            for (const Vector2& t : tries)
            {
                const Vector3 p = in.player + Vector3(t.x, 0.0f, t.y) * tt.extra;
                int gx = 0, gz = 0;
                grid.WorldToCell(p, gx, gz);
                if (!grid.IsWalkable(gx, gz)) continue;
                const float h = grid.SampleHeight(p.x, p.z);
                if (std::fabs(h - feet) > 1.2f) continue;
                pick = Vector3(p.x, h, p.z);
                break;
            }
            e.target = pick;
            e.phase = 4;
            e.t = 0.0f;
            ++m_Stats.teleports;
        }
    }
    else if (e.phase == 4)
    {
        const float w = (std::max)(tt.windup, kAppear + 0.1f);
        e.cmd = (e.t < 0.25f) ? Swarm::kEliteCmdTeleport : Swarm::kEliteCmdStand;   // 最初の少しで置き直し、後は立つ
        const float appear = e.t - (w - kAppear);
        e.hide = (appear > 0.0f) ? 1.0f - appear / kAppear : 1.0f;
        if (appear > 0.0f && !e.hitDone)
        {
            e.hitDone = true;   // ここでは「現れる煙を出した」印
            PlayFx((int)VFXId::EliteShadeBlink, e.target, 1.5f);
            AudioSystem::Get().Play("elite_blink");
        }
        e.glow = (appear > 0.0f) ? 1.0f : 0.0f;
        m_Warns.push_back({ e.target, tt.radius, e.t / w, 0.0f, 0.0f });
        if (e.t >= w)
        {
            e.phase = 2;
            e.t = 0.0f;
            e.hide = 0.0f;
            if (PlayerNear(in, e.target, tt.radius, 1.6f))
                AddHit(e.target, Damage(e, in, tt.damage), 0);
            PlayFx((int)VFXId::EliteShadeSlash, e.target + Vector3(0.0f, 1.0f, 0.0f), 0.8f, YawOf(FlatTo(e.target, in.player)));
            AudioSystem::Get().Play("elite_slash");
        }
    }
    else if (e.phase == 2)
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.flags |= Swarm::kEliteFlagAttack;
        e.glow = 0.0f;
        if (e.t >= tt.active)
        {
            e.phase = 3;
            e.t = 0.0f;
        }
    }
    else
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.flags &= ~Swarm::kEliteFlagNoContact;
        if (e.t >= tt.recover) Finish(e);
    }
}

// ============================================================
// 呪いの司書：溜め → 暗い紫の玉を count 個（左右に散って出てから、ゆっくりプレイヤーを追う。触れると爆ぜる）
// ============================================================
void EliteAttacks::MoveLibrarian(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::Librarian];
    e.t += in.dt;
    e.cmd = Swarm::kEliteCmdStand;
    if (e.phase == 1)
    {
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        const float w = (std::max)(tt.windup, 0.05f);
        e.glow = (std::min)(e.t / w, 1.0f);
        if (e.t >= w)
        {
            const Vector3 c = Ground(grid, e.pos);
            const float yaw = YawOf(FlatTo(e.pos, in.player));
            for (int k = 0; k < e.count; ++k)
            {
                if (m_Orbs.size() >= 12) break;
                const float a = yaw + ((float)k - 0.5f * (float)(e.count - 1)) * 0.9f;   // 扇に散って出る
                const Vector2 d = DirOf(a);
                Orb o;
                o.pos = c + Vector3(d.x * 1.2f, orbHeight, d.y * 1.2f);
                o.dir = d;
                o.life = orbLife;
                o.speed = tt.speedMove;
                o.damage = Damage(e, in, tt.damage);
                o.fx = PlayFx((int)VFXId::EliteCurseOrb, o.pos, orbLife + 0.5f);
                m_Orbs.push_back(o);
                ++m_Stats.orbs;
            }
            AudioSystem::Get().Play("elite_orb");
            e.phase = 3;
            e.t = 0.0f;
        }
    }
    else
    {
        e.glow = 0.0f;
        if (e.t >= tt.recover) Finish(e);
    }
}

void EliteAttacks::UpdateOrbs(const Input& in, const GridWorld& grid)
{
    for (size_t i = 0; i < m_Orbs.size();)
    {
        Orb& o = m_Orbs[i];
        o.life -= in.dt;
        const float yaw = TurnToward(YawOf(o.dir), YawOf(FlatTo(o.pos, in.player)), DirectX::XMConvertToRadians(orbTurnDeg) * in.dt);
        o.dir = DirOf(yaw);
        o.pos.x += o.dir.x * o.speed * in.dt;
        o.pos.z += o.dir.y * o.speed * in.dt;
        const float ground = grid.SampleHeight(o.pos.x, o.pos.z);
        o.pos.y += (ground + orbHeight - o.pos.y) * (std::min)(1.0f, 6.0f * in.dt);   // 地面の起伏に滑らかに付いていく
        if (o.fx && m_Vfx) m_Vfx->SetInstance(o.fx, o.pos, o.pos);
        m_Warns.push_back({ Vector3(o.pos.x, ground, o.pos.z), orbHitRadius + 0.15f, -1.0f, 0.0f, 0.0f });   // 足元の縁だけの輪

        const float dx = in.player.x - o.pos.x, dz = in.player.z - o.pos.z;
        const float r = orbHitRadius + in.playerRadius;
        const bool touch = in.playerAlive && dx * dx + dz * dz <= r * r && std::fabs(in.player.y - o.pos.y) < 1.6f;
        if (touch || o.life <= 0.0f)
        {
            if (touch) AddHit(Vector3(o.pos.x, ground, o.pos.z), o.damage, 1);
            if (o.fx && m_Vfx) m_Vfx->StopInstance(o.fx);
            PlayFx((int)VFXId::EliteCurseOrbBlast, o.pos, 1.0f, 0.0f, touch ? 1.0f : 0.6f);
            AudioSystem::Get().Play("elite_orb_blast", touch ? 1.0f : 0.5f);
            m_Orbs.erase(m_Orbs.begin() + i);
            continue;
        }
        ++i;
    }
}
