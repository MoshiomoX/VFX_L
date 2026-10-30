// ============================================================
// BossAttacksDjinn.cpp
// 砂漠 3 面目の Boss「砂嵐の魔神」の技（2026-10-29）。説明・数値は BossDjinn.h。
// 流れ（StartMove → UpdateMove → 間）は BossAttacks.cpp と同じ。ここは技毎の中身と、一段目 / 二段目の入れ替え・浮き
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    float Flat(const Vector3& a, const Vector3& b)
    {
        const float dx = a.x - b.x, dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    Vector2 FlatDir(const Vector3& from, const Vector3& to, const Vector2& fallback)
    {
        Vector2 d(to.x - from.x, to.z - from.z);
        const float l = d.Length();
        return (l > 1e-3f) ? d / l : fallback;
    }
    Vector3 Ground(const GridWorld& grid, float x, float z) { return Vector3(x, grid.SampleHeight(x, z), z); }
    Vector2 Rotate(const Vector2& v, float rad)
    {
        const float c = std::cos(rad), s = std::sin(rad);
        return Vector2(v.x * c - v.y * s, v.x * s + v.y * c);
    }
    float Clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }   // +Z が 0（SwarmSystem::WarnCircle と同じ）
    Vector2 DirOf(float yaw) { return Vector2(std::sin(yaw), std::cos(yaw)); }
    float Approach(float v, float target, float step)
    {
        return (v < target) ? (std::min)(v + step, target) : (std::max)(v - step, target);
    }
    // 向き a を b へ、最大 maxStep rad 回す
    float TurnToward(float a, float b, float maxStep)
    {
        float d = b - a;
        while (d > 3.1415927f) d -= 6.2831853f;
        while (d < -3.1415927f) d += 6.2831853f;
        return a + std::clamp(d, -maxStep, maxStep);
    }
    // 中心 foot・向き dir・半角 half・半径 r の扇にプレイヤーが居るか（すぐ近くなら向きは見ない）
    bool InFan(const Vector3& foot, const Vector2& dir, float half, float r, const Vector3& player)
    {
        const float d = Flat(foot, player);
        if (d > r) return false;
        if (d < 1.5f) return true;
        const Vector2 to = FlatDir(foot, player, dir);
        return std::acos(std::clamp(to.Dot(dir), -1.0f, 1.0f)) <= half;
    }

    constexpr float kPlayerThick = 0.4f;   // プレイヤーの太さ（当たりの幅に足す）
    // 腕の姿勢（chargePose → VS の g_AnimMode 6）：0 = 下ろす、1 = 振り上げ、1.3 = 叩き付け（腕は前下）。
    // 叩き付けた後の 1.3 と同じ腕の角度は 1 より下の側では 0.25（VS：角度 = w × (下げ - 0.75 × 振り)）= そこから 0 へ戻す
    constexpr float kPoseSlam = 1.3f;
    constexpr float kPoseAfterSlam = 0.25f;
}

// ============================================================
// 一段目 / 二段目の入れ替え（Configure・Reset・咆哮）。砂嵐召喚は両方、他は段毎
// ============================================================
void BossAttacks::ConfigureDjinnPhase(bool phase2)
{
    brain.rules[(int)BossMove::DustDevils].enabled = true;
    brain.rules[(int)BossMove::DjinnBreath].enabled = !phase2;
    brain.rules[(int)BossMove::DjinnShift].enabled = !phase2;
    brain.rules[(int)BossMove::SandFist].enabled = phase2;
    brain.rules[(int)BossMove::Shockwave].enabled = phase2;
    brain.rules[(int)BossMove::Quicksand].enabled = phase2;
}

void BossAttacks::EnterDjinnPhase2(Output& out)
{
    m_Djinn.phase2 = true;
    ConfigureDjinnPhase(true);
    out.djinn.landing = true;
}

// ============================================================
// 毎フレーム：浮き（二段目は landTime 秒で降りる。砂渡りの間は沈みに合わせて 0 へ）・飛び・間合い・腕の姿勢
// ============================================================
void BossAttacks::UpdateDjinnOutputs(const Input& in, Output& out)
{
    // 砂嵐の魔神と骸の王（2026-10-29。骸の王は浮かず・段も無く、ずっと飛んで間合いを取る）。他の Boss は全部 0（Output の既定）
    const bool kingType = (m_Type == BossType::BoneKing);
    if (m_Type != BossType::Djinn && !kingType) return;
    const float hover = kingType ? king.hover : djinn.hover;
    const bool floating = kingType || !m_Djinn.phase2;
    const float target = floating ? hover : 0.0f;
    const float rate = (std::max)(djinn.hover, 0.1f) / (std::max)(djinn.landTime, 0.05f);
    m_Djinn.hoverBase = Approach(m_Djinn.hoverBase, target, rate * in.dt);
    out.djinn.hover = m_Djinn.hoverBase * (1.0f - m_Djinn.shiftSink);
    out.djinn.fly = floating;
    // 間合い：浮いている間、技を出していない間だけ（技の間は突進の指示 chargeOn が動きを決める）
    if (in.bossAlive && floating && m_Move == BossMove::None && !IsRoaring())
    {
        out.djinn.keepMin = kingType ? king.keepMin : djinn.keepMin;
        out.djinn.keepMax = kingType ? king.keepMax : djinn.keepMax;
    }
    // 腕：技が書かない間は下ろす。咆哮の間は振り上げる（吠えながら降りる）
    const bool owned = (m_Move == BossMove::DustDevils || m_Move == BossMove::DjinnBreath || m_Move == BossMove::SandFist
        || m_Move == BossMove::CurseOrbs || m_Move == BossMove::RaiseDead || m_Move == BossMove::CurseFloor);
    if (IsRoaring())
        m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, 3.0f * in.dt);
    else if (!owned)
        m_Djinn.pose = Approach((m_Djinn.pose > 1.0f) ? kPoseAfterSlam : m_Djinn.pose, 0.0f, 2.5f * in.dt);
    out.chargePose = m_Djinn.pose;
    // 骸の王は真ん中から一歩も動かない（10-29 ユーザー「まだ動いているじゃないか」）：技が動きを指示していない間は、湧いた時の速さに
    // 関係なく「その場で止まってプレイヤーへ向く」指示（突進の溜めと同じ）。門の真ん中に湧く時は速さ 0 だが、トレーニング・自動テストでは歩く Boss で湧く
    if (kingType && in.bossAlive && !out.chargeOn)
    {
        out.chargeOn = true;
        out.chargeSpeed = 0.0f;
        out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    }
}

// ============================================================
// 砂嵐召喚：プレイヤーの今の足元を中心に、輪の上へ等間隔に砂旋を置く。魔神は止まって腕を上げている（devilCastTime）。
// 砂旋は技が終わっても UpdateDevils で回り続ける
// ============================================================
void BossAttacks::StartDustDevils(const Input& in, const GridWorld& grid, Output& out)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    const Vector3 c = Ground(grid, in.player.x, in.player.z);
    const int n = (std::max)(1, m_Djinn.phase2 ? djinn.devilCountPhase2 : djinn.devilCount);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float base = u01(m_Rng) * 6.2831853f;
    for (int k = 0; k < n; ++k)
    {
        DjinnDevil d;
        d.center = c;
        d.angle = base + 6.2831853f * (float)k / (float)n;
        d.radius = djinn.devilRadiusStart;
        d.pos = Ground(grid, c.x + std::cos(d.angle) * d.radius, c.z + std::sin(d.angle) * d.radius);
        d.id = m_Djinn.nextDevilId++;
        m_Djinn.devils.push_back(d);
    }
    out.djinn.devilsSummoned = true;
    out.ringsPlaced += n;
    ringsPlaced += (uint32_t)n;
}

bool BossAttacks::UpdateDustDevilsMove(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, in.dt / 0.3f);
    return m_MoveTime >= djinn.devilCastTime;
}

void BossAttacks::UpdateDevils(const Input& in, const GridWorld& grid, Output& out)
{
    const float spin = DirectX::XMConvertToRadians(djinn.devilSpinDeg);
    const float windup = (std::max)(djinn.devilWindup, 0.05f);
    const float life = (std::max)(djinn.devilLife, 0.1f);
    const float outro = (std::max)(djinn.devilOutro, 0.05f);
    for (auto it = m_Djinn.devils.begin(); it != m_Djinn.devils.end();)
    {
        DjinnDevil& d = *it;
        d.age += in.dt;
        const float active = d.age - windup;
        if (active >= life + outro)
        {
            it = m_Djinn.devils.erase(it);
            continue;
        }
        if (active <= 0.0f)
            d.presence = Clamp01(d.age / windup);
        else if (active < life)
        {
            // 回りながら輪を縮める（始めと終わりはゆっくり）
            const float u = active / life;
            const float s = u * u * (3.0f - 2.0f * u);
            d.radius = djinn.devilRadiusStart + (djinn.devilRadiusEnd - djinn.devilRadiusStart) * s;
            d.angle += spin * in.dt;
            d.presence = 1.0f;
        }
        else
        {
            d.leaving = true;
            d.presence = 1.0f - Clamp01((active - life) / outro);
        }
        d.pos = Ground(grid, d.center.x + std::cos(d.angle) * d.radius, d.center.z + std::sin(d.angle) * d.radius);

        // 当たり：回っている間だけ、1 本につき 1 回。背が高いので跳んでも devilReach までは当たる
        if (!d.hitDone && active > 0.0f && active < life && Flat(d.pos, in.player) <= djinn.devilHitRadius + kPlayerThick)
        {
            const float feetAbove = in.playerFeetY - d.pos.y;
            if (feetAbove <= djinn.devilReach && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::DustDevils;
                h.center = d.pos;
                h.radius = djinn.devilHitRadius;
                h.damage = djinn.devilDamage * in.damageMul;
                h.ranged = false;
                // 砂旋から外へ（真ん中なら回る向きへ）。シーンが砂旋と同じ高い吹き上げ（KnockKind::Wind）にする
                h.dir = FlatDir(d.pos, in.player, Vector2(-std::sin(d.angle), std::cos(d.angle)));
                out.hits.push_back(h);
                d.hitDone = true;
            }
        }
        ++it;
    }
}

// ============================================================
// 炎の息：始めた瞬間に向きを固め、溜め（腕を上げる。扇が育つ）→ breathTime 秒吐く。吐いている間、扇は
// breathTurnDeg 度 / 秒でプレイヤーへ向く（走り回り込めば外れる）。breathTick 秒毎に扇の中なら当たり
// ============================================================
void BossAttacks::StartDjinnBreath(const Input& in, const GridWorld& grid)
{
    m_Djinn.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Djinn.breathDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Djinn.breathTickLeft = 0.0f;
    m_Djinn.breathFired = false;
}

bool BossAttacks::UpdateDjinnBreath(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Djinn.breathDir;
    m_Djinn.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    if (m_MoveTime < djinn.breathWindup)
    {
        m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, in.dt / 0.35f);
        return false;
    }
    const float t = m_MoveTime - djinn.breathWindup;
    if (t >= djinn.breathTime)
        return t >= djinn.breathTime + djinn.breathRest;

    if (!m_Djinn.breathFired)
    {
        m_Djinn.breathFired = true;
        out.djinn.breathStarted = true;
    }
    const float want = YawOf(FlatDir(m_Djinn.foot, in.player, m_Djinn.breathDir));
    const float yaw = TurnToward(YawOf(m_Djinn.breathDir), want, DirectX::XMConvertToRadians(djinn.breathTurnDeg) * in.dt);
    m_Djinn.breathDir = DirOf(yaw);
    out.chargeDir = m_Djinn.breathDir;
    out.djinn.breathOn = true;
    out.djinn.breathYaw = yaw;
    m_Djinn.pose = 1.0f;

    m_Djinn.breathTickLeft -= in.dt;
    if (m_Djinn.breathTickLeft <= 0.0f)
    {
        m_Djinn.breathTickLeft += (std::max)(djinn.breathTick, 0.05f);
        const float half = DirectX::XMConvertToRadians(djinn.breathHalfDeg);
        const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
        if (InFan(m_Djinn.foot, m_Djinn.breathDir, half, djinn.breathRadius, in.player)
            && feetAbove <= djinn.breathReach && std::fabs(in.playerFeetY - m_Djinn.foot.y) < 4.0f)
        {
            Hit h;
            h.kind = BossMove::DjinnBreath;
            h.center = m_Djinn.foot;
            h.radius = djinn.breathRadius;
            h.damage = djinn.breathDamage * in.damageMul;
            h.ranged = false;
            h.dir = FlatDir(m_Djinn.foot, in.player, m_Djinn.breathDir);
            out.hits.push_back(h);
        }
    }
    return false;
}

// ============================================================
// 砂渡り：0 砂に沈む（止まる）→ 1 地面の下をプレイヤーの向こう側（Boss → プレイヤーの延長 shiftBehind m。歩けなければ
// 向きをずらす）へ走る（GPU の突進の道。一段目は飛ぶので壁を越える）→ 2 浮かび上がる → すぐ炎の息
// ============================================================
void BossAttacks::StartShift(const Input& in, const Vector3& target, BossMove follow, float sink, float speed, float maxTime, float rise)
{
    m_Djinn.shiftPhase = 0;
    m_Djinn.shiftT = 0.0f;
    m_Djinn.shiftSink = 0.0f;
    m_Djinn.shiftTarget = target;
    m_Djinn.shiftFollow = (int)follow;
    m_Djinn.shiftSinkTime = sink;
    m_Djinn.shiftSpeedNow = speed;
    m_Djinn.shiftMaxNow = maxTime;
    m_Djinn.shiftRiseTime = rise;
    m_ChargeDir = FlatDir(in.bossPos, target, Vector2(0.0f, 1.0f));
}

void BossAttacks::StartDjinnShift(const Input& in, const GridWorld& grid)
{
    const Vector2 n = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    Vector3 target = Ground(grid, in.player.x + n.x * 2.5f, in.player.z + n.y * 2.5f);
    static const float kTry[] = { 0.0f, 35.0f, -35.0f, 70.0f, -70.0f, 105.0f, -105.0f };
    for (float deg : kTry)
    {
        const Vector2 d = Rotate(n, DirectX::XMConvertToRadians(deg));
        const float x = in.player.x + d.x * djinn.shiftBehind, z = in.player.z + d.y * djinn.shiftBehind;
        int gx = 0, gz = 0;
        grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;
        target = Ground(grid, x, z);
        break;
    }
    StartShift(in, target, BossMove::DjinnBreath, djinn.shiftSink, djinn.shiftSpeed, djinn.shiftMaxTime, djinn.shiftRise);
    m_ChargeDir = n;
}

bool BossAttacks::UpdateDjinnShift(const Input& in, const GridWorld& grid, Output& out)
{
    (void)grid;
    m_Djinn.shiftT += in.dt;
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, m_Djinn.shiftTarget, m_ChargeDir);
    switch (m_Djinn.shiftPhase)
    {
    case 0:
        m_Djinn.shiftSink = Clamp01(m_Djinn.shiftT / (std::max)(m_Djinn.shiftSinkTime, 0.05f));
        if (m_Djinn.shiftT >= m_Djinn.shiftSinkTime) { m_Djinn.shiftPhase = 1; m_Djinn.shiftT = 0.0f; }
        return false;
    case 1:
    {
        m_Djinn.shiftSink = 1.0f;
        out.djinn.shiftTravel = true;
        // 着いた・時間切れ・通り過ぎた（リードバックの遅れで行き過ぎる）で止まる
        const Vector2 to(m_Djinn.shiftTarget.x - in.bossPos.x, m_Djinn.shiftTarget.z - in.bossPos.z);
        const bool passed = to.Dot(m_ChargeDir) < 0.0f && m_Djinn.shiftT > 0.2f;
        if (to.Length() <= 1.5f || m_Djinn.shiftT >= m_Djinn.shiftMaxNow || passed)
        {
            m_Djinn.shiftPhase = 2;
            m_Djinn.shiftT = 0.0f;
            out.djinn.shiftRose = true;
            out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
            return false;
        }
        m_ChargeDir = FlatDir(in.bossPos, m_Djinn.shiftTarget, m_ChargeDir);
        out.chargeDir = m_ChargeDir;
        out.chargeSpeed = m_Djinn.shiftSpeedNow;
        return false;
    }
    default:
        out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
        m_Djinn.shiftSink = 1.0f - Clamp01(m_Djinn.shiftT / (std::max)(m_Djinn.shiftRiseTime, 0.05f));
        if (m_Djinn.shiftT < m_Djinn.shiftRiseTime) return false;
        m_Djinn.shiftSink = 0.0f;
        if (m_Djinn.shiftFollow >= 0) m_DebugMove = (BossMove)m_Djinn.shiftFollow;   // 浮かび上がったらすぐ次（魔神 = 背中へ炎、骸の王 = 呪いの玉）
        return true;
    }
}

// ============================================================
// 砂の拳（二段目）：向きを固めて両腕を振り上げる（扇が育つ）→ 叩き付ける（腕を前下へ、扇の中で地面近くなら当たり）→ 少し止まる
// ============================================================
void BossAttacks::StartSandFist(const Input& in, const GridWorld& grid)
{
    m_Djinn.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Djinn.fistDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Djinn.fistHitDone = false;
}

bool BossAttacks::UpdateSandFist(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Djinn.fistDir;
    const float windup = (std::max)(djinn.fistWindup, 0.05f);
    if (m_MoveTime < windup)
    {
        m_Djinn.pose = Clamp01(m_MoveTime / windup);
        return false;
    }
    const float t = m_MoveTime - windup;
    if (prevTime < windup)
    {
        const float reach = djinn.fistRadius * 0.55f;
        out.djinn.fistSlammed = true;
        out.djinn.fistPos = Ground(grid, m_Djinn.foot.x + m_Djinn.fistDir.x * reach, m_Djinn.foot.z + m_Djinn.fistDir.y * reach);
        const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
        if (!m_Djinn.fistHitDone && feetAbove <= djinn.fistDodgeHeight && feetAbove > -2.0f
            && InFan(m_Djinn.foot, m_Djinn.fistDir, DirectX::XMConvertToRadians(djinn.fistHalfDeg), djinn.fistRadius, in.player))
        {
            Hit h;
            h.kind = BossMove::SandFist;
            h.center = out.djinn.fistPos;
            h.radius = djinn.fistRadius;
            h.damage = djinn.fistDamage * in.damageMul;
            h.ranged = false;
            h.dir = FlatDir(m_Djinn.foot, in.player, m_Djinn.fistDir);
            out.hits.push_back(h);
            m_Djinn.fistHitDone = true;
        }
        ++blasts;
    }
    // 腕：振り上げ（1）→ 叩き付け（1.3 = 前下）→ 同じ角度の 0.25 から下ろす（UpdateDjinnOutputs が技の後に続ける）
    const float slam = (std::max)(djinn.fistSlamTime, 0.02f);
    if (t < slam)
        m_Djinn.pose = 1.0f + (kPoseSlam - 1.0f) * (t / slam);
    else
        m_Djinn.pose = Approach((m_Djinn.pose > 1.0f) ? kPoseAfterSlam : m_Djinn.pose, 0.0f, 1.2f * in.dt);
    return t >= slam + djinn.fistRest;
}

// ============================================================
// 地面の警告：砂旋の足元の輪（育つ間は塗りが育ち、回る間は塗り）+ 縮んでいく輪の縁、炎の息・砂の拳の扇
// ============================================================
void BossAttacks::BuildDjinnVisuals(const GridWorld& grid)
{
    (void)grid;
    if (m_Type != BossType::Djinn) return;
    const float windup = (std::max)(djinn.devilWindup, 0.05f);
    bool ring = false;
    Vector3 ringCenter;
    float ringRadius = 0.0f;
    for (const DjinnDevil& d : m_Djinn.devils)
    {
        if (d.leaving) continue;
        const float active = d.age - windup;
        m_Visuals.push_back({ d.pos, djinn.devilHitRadius, (active <= 0.0f) ? Clamp01(d.age / windup) : 1.0f, 0.0f });
        if (active > 0.0f)
        {
            ring = true;
            ringCenter = d.center;
            ringRadius = d.radius;
        }
    }
    if (ring) m_Visuals.push_back({ ringCenter, (std::max)(ringRadius, 0.5f), -1.0f, 0.3f });

    if (m_Move == BossMove::DjinnBreath)
    {
        const bool winding = m_MoveTime < djinn.breathWindup;
        Visual v{ m_Djinn.foot, djinn.breathRadius, winding ? Clamp01(m_MoveTime / (std::max)(djinn.breathWindup, 0.05f)) : -1.0f, 0.0f };
        v.yaw = YawOf(m_Djinn.breathDir);
        v.shape = DirectX::XMConvertToRadians(djinn.breathHalfDeg);
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::SandFist && m_MoveTime < djinn.fistWindup)
    {
        Visual v{ m_Djinn.foot, djinn.fistRadius, Clamp01(m_MoveTime / (std::max)(djinn.fistWindup, 0.05f)), 0.0f };
        v.yaw = YawOf(m_Djinn.fistDir);
        v.shape = DirectX::XMConvertToRadians(djinn.fistHalfDeg);
        m_Visuals.push_back(v);
    }
}
