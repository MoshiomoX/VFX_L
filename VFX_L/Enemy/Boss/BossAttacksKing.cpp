// ============================================================
// BossAttacksKing.cpp
// 遺跡 3 面目の Boss「骸の王」の技（2026-10-29）。説明・数値は BossKing.h。
// 浮き・飛び・間合い（UpdateDjinnOutputs）と「影へ消える」の沈み / 走り / 浮かび（UpdateDjinnShift）は BossAttacksDjinn.cpp を共用
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
    float Clamp01(float t) { return std::clamp(t, 0.0f, 1.0f); }
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }
    Vector2 DirOf(float yaw) { return Vector2(std::sin(yaw), std::cos(yaw)); }
    float Approach(float v, float target, float step)
    {
        return (v < target) ? (std::min)(v + step, target) : (std::max)(v - step, target);
    }
    float TurnToward(float a, float b, float maxStep)
    {
        float d = b - a;
        while (d > 3.1415927f) d -= 6.2831853f;
        while (d < -3.1415927f) d += 6.2831853f;
        return a + std::clamp(d, -maxStep, maxStep);
    }
    constexpr float kPlayerThick = 0.4f;
    constexpr size_t kMaxOrbs = 16;
}

// ============================================================
// 呪いの玉：止まって両腕を上げる（orbWindup）→ Boss の前から扇に玉を放つ。玉は UpdateKingOrbs が追わせる
// ============================================================
void BossAttacks::StartCurseOrbs(const Input& in)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_King.orbsFired = false;
}

bool BossAttacks::UpdateCurseOrbs(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    m_ChargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    out.chargeDir = m_ChargeDir;
    if (m_MoveTime < king.orbWindup)
    {
        m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, in.dt / 0.35f);
        return false;
    }
    if (!m_King.orbsFired)
    {
        m_King.orbsFired = true;
        out.king.orbsLaunched = true;
        const int n = (std::max)(1, Enraged(in) ? king.orbCountEnraged : king.orbCount);
        const float base = YawOf(m_ChargeDir);
        const float step = DirectX::XMConvertToRadians(king.orbFanDeg);
        const Vector3 foot = Ground(grid, in.bossPos.x, in.bossPos.z);
        for (int k = 0; k < n && m_King.orbs.size() < kMaxOrbs; ++k)
        {
            const Vector2 d = DirOf(base + ((float)k - 0.5f * (float)(n - 1)) * step);
            KingOrb o;
            o.pos = Ground(grid, foot.x + d.x * king.orbSpawnFront, foot.z + d.y * king.orbSpawnFront) + Vector3(0.0f, king.orbHeight + 2.5f, 0.0f);
            o.dir = d;
            o.life = king.orbLife;
            o.id = m_King.nextOrbId++;
            m_King.orbs.push_back(o);
        }
    }
    m_Djinn.pose = Approach(m_Djinn.pose, 0.0f, in.dt / 0.4f);
    return m_MoveTime >= king.orbWindup + king.orbRest;
}

// 玉：向きをプレイヤーへ orbTurnDeg 度 / 秒で回しながら orbSpeed で進む（地面の orbHeight 上を滑らかに）。触れたら爆ぜて当たり、寿命で消える
void BossAttacks::UpdateKingOrbs(const Input& in, const GridWorld& grid, Output& out)
{
    for (size_t i = 0; i < m_King.orbs.size();)
    {
        KingOrb& o = m_King.orbs[i];
        o.life -= in.dt;
        const float yaw = TurnToward(YawOf(o.dir), YawOf(FlatDir(o.pos, in.player, o.dir)), DirectX::XMConvertToRadians(king.orbTurnDeg) * in.dt);
        o.dir = DirOf(yaw);
        o.pos.x += o.dir.x * king.orbSpeed * in.dt;
        o.pos.z += o.dir.y * king.orbSpeed * in.dt;
        const float ground = grid.SampleHeight(o.pos.x, o.pos.z);
        o.pos.y += (ground + king.orbHeight - o.pos.y) * (std::min)(1.0f, 4.0f * in.dt);

        const bool touch = Flat(o.pos, in.player) <= king.orbHitRadius + kPlayerThick && std::fabs(in.player.y - o.pos.y) < 1.8f;
        if (touch || o.life <= 0.0f)
        {
            if (touch)
            {
                Hit h;
                h.kind = BossMove::CurseOrbs;
                h.center = Vector3(o.pos.x, ground, o.pos.z);
                h.radius = king.orbHitRadius;
                h.damage = king.orbDamage * in.damageMul;
                h.ranged = false;
                h.dir = FlatDir(o.pos, in.player, o.dir);
                out.hits.push_back(h);
            }
            out.king.orbBlasts.push_back({ o.pos, touch });
            m_King.orbs.erase(m_King.orbs.begin() + i);
            continue;
        }
        ++i;
    }
}

// ============================================================
// 大起尸：始めた瞬間に Output::king.raise（シーンが MobSpawner::RaiseCorpsesAround で印と骸骨を出す）。Boss は止まって両腕を上げ吠える
// ============================================================
bool BossAttacks::UpdateRaiseDead(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, in.dt / 0.3f);
    return m_MoveTime >= king.raiseWindup;
}

// ============================================================
// 影へ消える：プレイヤーから見た Boss の向きを ±stepTurnDeg 回した所（プレイヤーから stepDist m）へ。歩けなければ回す角を減らす。
// 沈む / 走る / 浮かぶ は魔神の砂渡りと同じ（UpdateDjinnShift）、浮かんだらすぐ呪いの玉
// ============================================================
void BossAttacks::StartShadowStep(const Input& in, const GridWorld& grid)
{
    const Vector2 fromPlayer = FlatDir(in.player, in.bossPos, Vector2(0.0f, -1.0f));
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float side = (u01(m_Rng) < 0.5f) ? -1.0f : 1.0f;
    Vector3 target = Ground(grid, in.player.x + fromPlayer.x * king.stepDist, in.player.z + fromPlayer.y * king.stepDist);
    static const float kScale[] = { 1.0f, -1.0f, 0.7f, -0.7f, 0.45f, -0.45f };
    for (float s : kScale)
    {
        const float a = YawOf(fromPlayer) + side * s * DirectX::XMConvertToRadians(king.stepTurnDeg);
        const Vector2 d = DirOf(a);
        const float x = in.player.x + d.x * king.stepDist, z = in.player.z + d.y * king.stepDist;
        int gx = 0, gz = 0;
        grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;
        target = Ground(grid, x, z);
        break;
    }
    StartShift(in, target, BossMove::CurseOrbs, king.stepSink, king.stepSpeed, king.stepMaxTime, king.stepRise);
}

// ============================================================
// 呪いの床：始めた時のプレイヤーの足元に紫の渦（curseWindup の間に広がる）。技は広がり切ったら終わり、床は curseDuration 残る
// ============================================================
void BossAttacks::StartCurseFloor(const Input& in, const GridWorld& grid)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    KingCurse& c = m_King.curse;
    c = KingCurse{};
    c.active = true;
    c.center = Ground(grid, in.player.x, in.player.z);
    c.tickLeft = king.curseWindup;   // 広がっている間は減らない
}

bool BossAttacks::UpdateCurseFloorMove(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    m_Djinn.pose = Approach(m_Djinn.pose, 1.0f, in.dt / 0.3f);
    return m_MoveTime >= king.curseWindup;
}

void BossAttacks::UpdateKingCurse(const Input& in, const GridWorld& grid, Output& out)
{
    KingCurse& c = m_King.curse;
    if (!c.active) return;
    c.age += in.dt;
    const float spread = Clamp01(c.age / (std::max)(king.curseWindup, 0.05f));
    const float life = king.curseWindup + king.curseDuration;
    const float fade = Clamp01((life - c.age) / (std::max)(king.curseFade, 0.05f));
    c.radius = king.curseRadius * (1.0f - (1.0f - spread) * (1.0f - spread));
    c.strength = spread * fade;
    if (c.age >= life)
    {
        c = KingCurse{};
        return;
    }
    // じわじわ：中で地面に居れば curseTick 秒毎に少し（ノックバック無し）
    c.tickLeft -= in.dt;
    if (c.tickLeft > 0.0f) return;
    c.tickLeft += (std::max)(king.curseTick, 0.1f);
    const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
    if (Flat(c.center, in.player) <= c.radius && feetAbove <= king.curseReach && std::fabs(in.playerFeetY - c.center.y) < 3.0f)
    {
        Hit h;
        h.kind = BossMove::CurseFloor;
        h.center = c.center;
        h.radius = c.radius;
        h.damage = king.curseTickDamage * in.damageMul;
        h.ranged = false;
        h.noKnock = true;
        h.dir = FlatDir(c.center, in.player, Vector2(0.0f, 1.0f));
        out.hits.push_back(h);
    }
}

// 中では遅く、中心へ引く（縁 cursePullEdge → 半径の 0.3 より内側 cursePullCenter）。流沙と違って渦には流さない。広がる間は効かない
bool BossAttacks::CursePull(const Vector3& feet, bool grounded, float& slowMul, Vector2& pull) const
{
    slowMul = 1.0f;
    pull = Vector2::Zero;
    const KingCurse& c = m_King.curse;
    // 広がっている間（curseWindup）は予告だけ = その間に走って出られる
    if (!c.active || !grounded || c.age < king.curseWindup || c.strength <= 0.0f || c.radius <= 0.1f) return false;
    const Vector2 d(feet.x - c.center.x, feet.z - c.center.z);
    const float dist = d.Length();
    if (dist > c.radius || std::fabs(feet.y - c.center.y) > 3.0f) return false;
    slowMul = 1.0f + (king.curseSlow - 1.0f) * c.strength;
    if (dist > 0.6f)
    {
        const float u = Clamp01((dist / c.radius - 0.3f) / 0.7f);
        const float speed = (king.cursePullCenter + (king.cursePullEdge - king.cursePullCenter) * u) * c.strength;
        pull = -(d / dist) * speed;
    }
    return true;
}

// ============================================================
// 地面の警告：玉の足元の縁、呪いの床が広がる間の縁
// ============================================================
void BossAttacks::BuildKingVisuals(const GridWorld& grid)
{
    if (m_Type != BossType::BoneKing) return;
    for (const KingOrb& o : m_King.orbs)
        m_Visuals.push_back({ Ground(grid, o.pos.x, o.pos.z), king.orbHitRadius + 0.15f, -1.0f, 0.0f });
    const KingCurse& c = m_King.curse;
    if (c.active && c.age < king.curseWindup)
        m_Visuals.push_back({ c.center, (std::max)(king.curseRadius, 0.5f), Clamp01(c.age / (std::max)(king.curseWindup, 0.05f)), 0.0f });
}
