// ============================================================
// EliteMoves.cpp
// 面の固定エリートの型毎の技（2026-10-25）。どれも「予告 → 避けられる当たり」:
//   段 0 = 普段（GPU の AI：A は追い、B は距離を取る）。冷却が明けて条件が合えば段 1 へ
//   段 1 = 溜め（予告が育つ）、段 2 = 技の最中、段 3 = 後の立ち止まり、段 4〜 = 型毎
// GPU の体へは e.cmd / e.moveDir / e.moveSpeed / e.hide / e.glow と、毎フレーム組み直す e.flags で伝える
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

void EliteAttacks::UpdateMove(Elite& e, const Input& in, const GridWorld& grid)
{
    uint32_t affixFlags = 0;
    if (e.affixes & kEliteAffixSwift) affixFlags |= Swarm::kEliteFlagSwift;
    if (e.shieldUp) affixFlags |= Swarm::kEliteFlagShield;
    if (e.enraged) affixFlags |= Swarm::kEliteFlagEnraged;
    e.flags = affixFlags;

    if (e.phase == 0)
    {
        e.cmd = Swarm::kEliteCmdAI;
        e.glow = 0.0f;
        e.hide = 0.0f;
        e.moveSpeed = 0.0f;
        e.cd -= in.dt;
        if (e.cd > 0.0f || !in.playerAlive) return;
        if (std::fabs(in.playerFeetY + 0.9f - e.pos.y) > 2.5f) return;   // 別の段に居る（崖の上下）。追い付くのを待つ
        const TypeTuning& tt = tune[(int)e.type];
        const float dist = FlatDist(e.pos, in.player);
        // 弓手だけ近すぎると撃たない（距離を取り直す）。騎兵・影の剣士は追い付いた距離からでも技を出す（近接で張り付いたまま殴るだけにならないように）
        const float minDist = (e.type == EliteType::Archer) ? 3.5f : 0.0f;
        if (dist > tt.trigger || dist < minDist) return;
        e.phase = 1;
        e.t = 0.0f;
        e.cd = Cooldown(e);
        e.hitDone = false;
        e.dir = FlatTo(e.pos, in.player);
        e.target = Ground(grid, e.pos);
        e.count = tt.count;
        ++m_Stats.moves;
        if (e.type == EliteType::Axe) e.count = e.enraged ? 2 : 1;   // 狂暴なら 2 回転
        if (e.type == EliteType::Archer) e.count = e.enraged ? tt.count + 2 : tt.count;
        if (e.type == EliteType::Librarian) e.count = e.enraged ? tt.count + 1 : tt.count;
        if (e.type == EliteType::Rider) e.dashLen = std::clamp(dist + 5.0f, 8.0f, tt.radius);
        if (e.type == EliteType::Shade)
        {
            PlayFx((int)VFXId::EliteShadeBlink, Ground(grid, e.pos), 1.5f);
            AudioSystem::Get().Play("elite_blink");
        }
    }

    switch (e.type)
    {
    case EliteType::Axe:       MoveAxe(e, in, grid); break;
    case EliteType::Archer:    MoveArcher(e, in, grid); break;
    case EliteType::Rider:     MoveRider(e, in, grid); break;
    case EliteType::FlameMage: MoveFlame(e, in, grid); break;
    case EliteType::Shade:     MoveShade(e, in, grid); break;
    case EliteType::Librarian: MoveLibrarian(e, in, grid); break;
    default: Finish(e); break;
    }
}

// ============================================================
// 大斧の戦鬼：その場で溜め（足元に輪が育つ）→ 回転斬り（輪の中に居たら爆発級に飛ばされる）。狂暴なら 2 回
// ============================================================
void EliteAttacks::MoveAxe(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::Axe];
    e.t += in.dt;
    e.cmd = Swarm::kEliteCmdStand;
    if (e.phase == 1)
    {
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        const float w = (std::max)(tt.windup, 0.05f);
        e.glow = (std::min)(e.t / w, 1.0f);
        e.target = Ground(grid, e.pos);
        m_Warns.push_back({ e.target, tt.radius, e.t / w, 0.0f, 0.0f });
        if (e.t >= w)
        {
            e.phase = 2;
            e.t = 0.0f;
            if (PlayerNear(in, e.target, tt.radius, 1.2f))
                AddHit(e.target, Damage(e, in, tt.damage), 1);
            PlayFx((int)VFXId::EliteAxeSpin, e.target, 1.2f, 0.0f, tt.radius / 4.2f);
            AudioSystem::Get().Play("elite_axe");
        }
    }
    else if (e.phase == 2)
    {
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagSpin;
        e.glow = 0.0f;
        if (e.t >= tt.active)
        {
            if (--e.count > 0)
            {
                e.phase = 1;
                e.t = (std::max)(0.0f, tt.windup - 0.45f);   // 2 回目は短い溜め
            }
            else
            {
                e.phase = 3;
                e.t = 0.0f;
            }
        }
    }
    else
    {
        if (e.t >= tt.recover) Finish(e);
    }
}

// ============================================================
// 森の弓手：向きを決めて扇に帯（矢の道）→ 矢が帯に沿って飛ぶ（横へ避ける）。狂暴なら 2 本増える
// ============================================================
void EliteAttacks::MoveArcher(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::Archer];
    e.t += in.dt;
    const float yaw = YawOf(e.dir);
    const float spread = DirectX::XMConvertToRadians(tt.spreadDeg * (e.count > tt.count ? 0.8f : 1.0f));
    if (e.phase == 1)
    {
        e.cmd = Swarm::kEliteCmdMove;   // 速さ 0 = 決めた向きで立つ
        e.moveDir = e.dir;
        e.moveSpeed = 0.0f;
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        const float w = (std::max)(tt.windup, 0.05f);
        e.glow = (std::min)(e.t / w, 1.0f) * 0.6f;
        const Vector3 c = Ground(grid, e.pos);
        for (int k = 0; k < e.count; ++k)
        {
            const float a = yaw + ((float)k - 0.5f * (float)(e.count - 1)) * spread;
            m_Warns.push_back({ c, tt.radius, e.t / w, a, -tt.width });
        }
        if (e.t >= w)
        {
            // 放つ：帯の根元から少し前・胸の高さ
            for (int k = 0; k < e.count; ++k)
            {
                const float a = yaw + ((float)k - 0.5f * (float)(e.count - 1)) * spread;
                const Vector2 d = DirOf(a);
                Arrow ar;
                ar.pos = c + Vector3(d.x * 0.8f, 1.3f, d.y * 0.8f);
                ar.dir = d;
                ar.left = tt.radius;
                ar.speed = tt.speedMove;
                ar.damage = Damage(e, in, tt.damage);
                ar.fx = PlayFx((int)VFXId::EliteArrow, ar.pos, tt.radius / (std::max)(tt.speedMove, 1.0f) + 0.5f, a);
                m_Arrows.push_back(ar);
            }
            m_Stats.arrows += e.count;
            AudioSystem::Get().Play("elite_arrow");
            e.phase = 3;
            e.t = 0.0f;
        }
    }
    else
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.glow = 0.0f;
        if (e.t >= tt.recover) Finish(e);
    }
}

void EliteAttacks::UpdateArrows(const Input& in, const GridWorld& grid)
{
    for (size_t i = 0; i < m_Arrows.size();)
    {
        Arrow& a = m_Arrows[i];
        const float step = (std::min)(a.speed * in.dt, a.left);
        const Vector3 from = a.pos;
        const Vector3 to = from + Vector3(a.dir.x, 0.0f, a.dir.y) * step;
        a.left -= step;
        bool done = a.left <= 0.0f;
        bool hit = false;

        // プレイヤー：この一歩の線分に一番近い所が帯の半分 + 体の半径以内、高さも近い
        if (in.playerAlive)
        {
            const Vector2 p(in.player.x - from.x, in.player.z - from.z);
            const float along = std::clamp(p.x * a.dir.x + p.y * a.dir.y, 0.0f, step);
            const Vector2 close = p - a.dir * along;
            const float half = tune[(int)EliteType::Archer].width * 0.5f;
            if (close.Length() <= half + in.playerRadius * 0.5f && std::fabs(in.player.y - from.y) < 1.5f)
                hit = true;
        }
        // 壁・持ち上がった地面に刺さる
        int gx = 0, gz = 0;
        grid.WorldToCell(to, gx, gz);
        if (!grid.IsWalkable(gx, gz) || grid.SampleHeight(to.x, to.z) > to.y - 0.3f) done = true;

        a.pos = to;
        if (a.fx && m_Vfx) m_Vfx->SetInstance(a.fx, a.pos, a.pos);
        if (hit)
        {
            AddHit(from, a.damage, 0);
            done = true;
        }
        if (done)
        {
            if (a.fx && m_Vfx) m_Vfx->StopInstance(a.fx);
            PlayFx((int)VFXId::EliteArrowHit, a.pos, 0.8f);
            m_Arrows.erase(m_Arrows.begin() + i);
            continue;
        }
        ++i;
    }
}

// ============================================================
// 砂塵の騎兵：向きを決めて帯 → 一直線に突進、を count 回（毎回プレイヤーへ狙い直す）。当たると爆発級
// ============================================================
void EliteAttacks::MoveRider(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::Rider];
    e.t += in.dt;
    if (e.phase == 1)
    {
        const float w = (std::max)((e.count == tt.count) ? tt.windup : tt.extra, 0.05f);   // 2 回目からは短い
        e.cmd = Swarm::kEliteCmdMove;
        e.moveDir = e.dir;
        e.moveSpeed = 0.0f;
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        e.glow = (std::min)(e.t / w, 1.0f);
        m_Warns.push_back({ Ground(grid, e.pos), e.dashLen, e.t / w, YawOf(e.dir), -tt.width });
        if (e.t >= w)
        {
            e.phase = 2;
            e.t = 0.0f;
            e.hitDone = false;
            e.dashFrom = Ground(grid, e.pos);
            const float dur = e.dashLen / (std::max)(tt.speedMove, 1.0f);
            e.fxMove = PlayFx((int)VFXId::EliteRiderDash, e.dashFrom, dur + 0.4f, YawOf(e.dir));
            AudioSystem::Get().Play("elite_dash");
        }
    }
    else if (e.phase == 2)
    {
        const float traveled = (std::min)(e.t * tt.speedMove, e.dashLen);
        const Vector3 at = Ground(grid, e.dashFrom + Vector3(e.dir.x, 0.0f, e.dir.y) * traveled);
        e.cmd = Swarm::kEliteCmdMove;
        e.moveDir = e.dir;
        e.moveSpeed = tt.speedMove;
        e.flags |= Swarm::kEliteFlagNoContact;
        e.glow = 0.4f;
        m_Warns.push_back({ at, (std::max)(e.dashLen - traveled, 0.1f), -1.0f, YawOf(e.dir), -tt.width });
        if (e.fxMove && m_Vfx) m_Vfx->SetInstance(e.fxMove, at, at);
        // 先読み（CPU）とリードバック（2〜3 フレーム遅れ、壁で止まった時はこちらが正しい）の両方で見る
        if (!e.hitDone && (PlayerNear(in, at, 1.5f, 2.0f) || PlayerNear(in, Ground(grid, e.pos), 1.5f, 2.0f)))
        {
            e.hitDone = true;
            AddHit(at - Vector3(e.dir.x, 0.0f, e.dir.y), Damage(e, in, tt.damage), 1);
        }
        if (traveled >= e.dashLen)
        {
            if (e.fxMove && m_Vfx) m_Vfx->StopInstance(e.fxMove);
            e.fxMove = 0;
            if (--e.count > 0 && in.playerAlive)
            {
                e.phase = 1;
                e.t = 0.0f;
                e.dir = FlatTo(e.pos, in.player);
                e.dashLen = std::clamp(FlatDist(e.pos, in.player) + 5.0f, 8.0f, tt.radius);
            }
            else
            {
                e.phase = 3;
                e.t = 0.0f;
            }
        }
    }
    else
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.moveSpeed = 0.0f;
        e.glow = 0.0f;
        if (e.t >= tt.recover) Finish(e);
    }
}

// ============================================================
// 炎砂の術師：向きを決めて扇の予告 → 炎の息（扇がゆっくりプレイヤーを追う。横へ回り込めば外れる）
// ============================================================
void EliteAttacks::MoveFlame(Elite& e, const Input& in, const GridWorld& grid)
{
    const TypeTuning& tt = tune[(int)EliteType::FlameMage];
    const float half = DirectX::XMConvertToRadians(tt.width);
    e.t += in.dt;
    const Vector3 c = Ground(grid, e.pos);
    if (e.phase == 1)
    {
        e.cmd = Swarm::kEliteCmdMove;
        e.moveDir = e.dir;
        e.moveSpeed = 0.0f;
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        const float w = (std::max)(tt.windup, 0.05f);
        e.glow = (std::min)(e.t / w, 1.0f);
        m_Warns.push_back({ c, tt.radius, e.t / w, YawOf(e.dir), half });
        if (e.t >= w)
        {
            e.phase = 2;
            e.t = 0.0f;
            e.tickT = 0.0f;
            e.fxMoveYaw = DirectX::XMConvertToDegrees(YawOf(e.dir));
            e.fxMove = PlayFx((int)VFXId::EliteFlameBreath, c + Vector3(0.0f, 1.3f, 0.0f), tt.active + 0.2f, YawOf(e.dir));
            AudioSystem::Get().Play("elite_flame");
        }
    }
    else if (e.phase == 2)
    {
        // 扇の向きはゆっくりプレイヤーへ（speedMove = 度 / 秒）
        const float want = YawOf(FlatTo(e.pos, in.player));
        const float yaw = TurnToward(YawOf(e.dir), want, DirectX::XMConvertToRadians(tt.speedMove) * in.dt);
        e.dir = DirOf(yaw);
        e.cmd = Swarm::kEliteCmdMove;
        e.moveDir = e.dir;
        e.moveSpeed = 0.0f;
        e.flags |= Swarm::kEliteFlagNoContact | Swarm::kEliteFlagAttack;
        e.glow = 0.2f;
        m_Warns.push_back({ c, tt.radius, -1.0f, yaw, half });
        if (e.fxMove && m_Vfx)
        {
            m_Vfx->SetInstance(e.fxMove, c + Vector3(0.0f, 1.3f, 0.0f), c);
            const float yawDeg = DirectX::XMConvertToDegrees(yaw);
            m_Vfx->RotateInstance(e.fxMove, yawDeg - e.fxMoveYaw);
            e.fxMoveYaw = yawDeg;
        }
        e.tickT -= in.dt;
        if (e.tickT <= 0.0f)
        {
            e.tickT = 0.2f;
            const float d = FlatDist(c, in.player);
            if (in.playerAlive && d <= tt.radius && std::fabs(in.playerFeetY - c.y) < 2.0f)
            {
                const Vector2 to = FlatTo(c, in.player);
                const float ang = std::acos(std::clamp(to.x * e.dir.x + to.y * e.dir.y, -1.0f, 1.0f));
                if (ang <= half || d < 1.2f)
                    AddHit(c, Damage(e, in, tt.damage), 0);
            }
        }
        if (e.t >= tt.active)
        {
            if (e.fxMove && m_Vfx) m_Vfx->StopInstance(e.fxMove);
            e.fxMove = 0;
            e.phase = 3;
            e.t = 0.0f;
        }
    }
    else
    {
        e.cmd = Swarm::kEliteCmdStand;
        e.glow = 0.0f;
        if (e.t >= tt.recover) Finish(e);
    }
}
