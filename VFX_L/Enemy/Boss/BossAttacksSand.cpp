// ============================================================
// BossAttacksSand.cpp
// 砂漠の Boss「砂の守護者」だけの技（2026-10-24。ユーザーが選んだ 3 つ：砂の槍 / 潜砂奇襲 / 流沙）。
// 流れ（StartMove → UpdateMove → 間）は BossAttacks.cpp と同じ。ここは技毎の中身だけ
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

    constexpr float kPlayerThick = 0.4f;   // プレイヤーの太さ（当たりの幅に足す）
    constexpr float kSpearStartGap = 0.8f; // Boss の足元から最初のトゲまで（体の中から出さない）
}

// ============================================================
// 砂の槍：始めた瞬間の Boss → プレイヤーへ向きを決める（狂暴の後は扇に 3 本）。
// 溜め（止まって線上の輪が育つ）→ 前線が足元から spearSpeed で先へ。前線が通った所にプレイヤーが居れば 1 回当たり
// ============================================================
void BossAttacks::StartSandSpear(const Input& in, const GridWorld& grid)
{
    m_SpearStart = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_SpearDirs.clear();
    const int n = Enraged(in) ? (std::max)(1, spearLinesEnraged) : 1;
    const float step = DirectX::XMConvertToRadians(spearFanDeg);
    for (int k = 0; k < n; ++k)
        m_SpearDirs.push_back(Rotate(m_ChargeDir, ((float)k - (float)(n - 1) * 0.5f) * step));
    m_SpearHitDone = false;
}

bool BossAttacks::UpdateSandSpear(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    // 溜めの間も突き出している間も、その場で線の向きを向いて止まる
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;
    if (m_MoveTime < spearWindup) return false;

    if (prevTime < spearWindup)
    {
        for (const Vector2& d : m_SpearDirs)
        {
            Output::SpearLine l;
            l.start = Ground(grid, m_SpearStart.x + d.x * kSpearStartGap, m_SpearStart.z + d.y * kSpearStartGap);
            l.end = Ground(grid, m_SpearStart.x + d.x * spearLength, m_SpearStart.z + d.y * spearLength);
            out.spearLines.push_back(l);
        }
        out.spearSpeed = spearSpeed;
    }

    const float front = kSpearStartGap + (m_MoveTime - spearWindup) * spearSpeed;
    const float prevFront = kSpearStartGap + (std::max)(0.0f, prevTime - spearWindup) * spearSpeed;
    if (!m_SpearHitDone)
    {
        for (const Vector2& d : m_SpearDirs)
        {
            const Vector2 rel(in.player.x - m_SpearStart.x, in.player.z - m_SpearStart.z);
            const float s = rel.Dot(d);
            if (s < 0.0f || s > spearLength || s < prevFront - 0.5f || s > front + 0.5f) continue;
            const Vector2 lat = rel - d * s;
            if (lat.Length() > spearLaneRadius + kPlayerThick) continue;
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (feetAbove > spearDodgeHeight || feetAbove < -2.0f) continue;

            Hit h;
            h.kind = BossMove::SandSpear;
            h.center = Ground(grid, m_SpearStart.x + d.x * s, m_SpearStart.z + d.y * s);
            h.radius = spearLaneRadius;
            h.damage = spearDamage * in.damageMul;
            h.ranged = false;
            // 線から外へ弾く（真ん中に居たら線の向きへ）。上へもシーンが KnockKind::Spike で跳ね上げる
            h.dir = (lat.LengthSquared() > 1e-3f) ? lat / lat.Length() : d;
            out.hits.push_back(h);
            m_SpearHitDone = true;
            break;
        }
    }
    return front >= spearLength + 1.0f;
}

// ============================================================
// 潜砂奇襲：0 沈む（止まる）→ 1 地面の下でプレイヤーを追う（GPU の突進の道を使う：毎フレーム向きを替える）
//   → 2 止まって足元の輪（輪の中心は Boss の位置。リードバックの遅れが落ち着くまで追い直す）→ 3 飛び出して叩く
// ============================================================
void BossAttacks::StartBurrow(const Input& in)
{
    m_BurrowPhase = 0;
    m_BurrowPhaseTime = 0.0f;
    m_BurrowSink = 0.0f;
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
}

bool BossAttacks::UpdateBurrow(const Input& in, const GridWorld& grid, Output& out)
{
    m_BurrowPhaseTime += in.dt;
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    m_ChargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    out.chargeDir = m_ChargeDir;

    switch (m_BurrowPhase)
    {
    case 0:
        m_BurrowSink = Clamp01(m_BurrowPhaseTime / (std::max)(burrowSinkTime, 0.05f));
        if (m_BurrowPhaseTime >= burrowSinkTime) { m_BurrowPhase = 1; m_BurrowPhaseTime = 0.0f; }
        return false;
    case 1:
        m_BurrowSink = 1.0f;
        out.burrowMound = true;
        out.chargeSpeed = burrowSpeed;
        if (Flat(in.player, in.bossPos) <= burrowStopDist || m_BurrowPhaseTime >= burrowMaxTravel)
        {
            out.chargeSpeed = 0.0f;
            m_BurrowPhase = 2;
            m_BurrowPhaseTime = 0.0f;
            m_BurrowRingCenter = Ground(grid, in.bossPos.x, in.bossPos.z);
            ++out.ringsPlaced;
            ++ringsPlaced;
        }
        return false;
    case 2:
        m_BurrowSink = 1.0f;
        out.burrowMound = true;
        m_BurrowRingCenter = Ground(grid, in.bossPos.x, in.bossPos.z);
        if (m_BurrowPhaseTime >= burrowWarn)
        {
            Hit h;
            h.kind = BossMove::Burrow;
            h.center = m_BurrowRingCenter;
            h.radius = burrowRadius;
            h.damage = burrowDamage * in.damageMul;
            h.ranged = true;
            out.hits.push_back(h);
            out.burrowEmerged = true;
            ++blasts;
            m_BurrowPhase = 3;
            m_BurrowPhaseTime = 0.0f;
        }
        return false;
    default:
        m_BurrowSink = 1.0f - Clamp01(m_BurrowPhaseTime / (std::max)(burrowRiseTime, 0.05f));
        if (m_BurrowPhaseTime < burrowRiseTime) return false;
        m_BurrowSink = 0.0f;
        return true;
    }
}

// ============================================================
// 流沙：吠えている間（sandWindup）に渦が Boss の足元から広がる。技はそこで終わり、渦は sandDuration 秒残る
// ============================================================
void BossAttacks::StartQuicksand(const Input& in, const GridWorld& grid)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_SandField.active = true;
    m_SandField.center = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_SandField.radius = 0.0f;
    m_SandField.strength = 0.0f;
    m_SandAge = 0.0f;
}

bool BossAttacks::UpdateQuicksandMove(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    return m_MoveTime >= sandWindup;
}

void BossAttacks::UpdateQuicksandField(float dt)
{
    if (!m_SandField.active) return;
    m_SandAge += dt;
    const float spread = Clamp01(m_SandAge / (std::max)(sandWindup, 0.05f));
    const float life = sandWindup + sandDuration;
    const float fade = Clamp01((life - m_SandAge) / (std::max)(sandFadeTime, 0.05f));
    m_SandField.radius = sandRadius * (1.0f - (1.0f - spread) * (1.0f - spread));
    m_SandField.strength = spread * fade;
    if (m_SandAge >= life) m_SandField = QuicksandField{};
}

// 中心へ引く速さは縁で sandPullEdge、半径の 0.3 より内側で sandPullCenter。渦の回り（風源と同じ反時計回り）にも少し流す
bool BossAttacks::QuicksandPull(const Vector3& feet, bool grounded, float& slowMul, Vector2& pull) const
{
    slowMul = 1.0f;
    pull = Vector2::Zero;
    const QuicksandField& f = m_SandField;
    if (!f.active || !grounded || f.strength <= 0.0f || f.radius <= 0.1f) return false;
    const Vector2 d(feet.x - f.center.x, feet.z - f.center.z);
    const float dist = d.Length();
    if (dist > f.radius || std::fabs(feet.y - f.center.y) > 3.0f) return false;   // 外・別の段（台地の上下）

    slowMul = 1.0f + (sandSlow - 1.0f) * f.strength;
    if (dist > 1e-3f)
    {
        const Vector2 outw = d / dist;
        const float u = Clamp01((dist / f.radius - 0.3f) / 0.7f);   // 0 = 内側、1 = 縁
        float speed = (sandPullCenter + (sandPullEdge - sandPullCenter) * u) * f.strength;
        speed *= Clamp01((dist - sandDeadRadius) / 0.6f);
        const Vector2 tang(-outw.y, outw.x);
        pull = -outw * speed + tang * (speed * 0.35f);
    }
    return true;
}

// ============================================================
// 地面の警告：砂の槍 = 線上の輪（前線がまだ届いていない所だけ）、潜砂 = 止まった所の輪、流沙 = 広がる間の縁
// ============================================================
void BossAttacks::BuildSandVisuals(const GridWorld& grid)
{
    if (m_Move == BossMove::SandSpear)
    {
        const float p = (spearWindup > 0.0f) ? (std::min)(1.0f, m_MoveTime / spearWindup) : 1.0f;
        const float front = kSpearStartGap + (std::max)(0.0f, m_MoveTime - spearWindup) * spearSpeed;
        const float spacing = (std::max)(spearLaneSpacing, 0.5f);
        const int n = (std::max)(1, (int)(spearLength / spacing));
        for (const Vector2& d : m_SpearDirs)
            for (int i = 0; i < n; ++i)
            {
                const float s = spacing * ((float)i + 0.5f);
                if (s < front) continue;
                m_Visuals.push_back({ Ground(grid, m_SpearStart.x + d.x * s, m_SpearStart.z + d.y * s), spearLaneRadius, p, 0.0f });
            }
    }
    if (m_Move == BossMove::Burrow && m_BurrowPhase == 2)
        m_Visuals.push_back({ m_BurrowRingCenter, burrowRadius,
            (burrowWarn > 0.0f) ? (std::min)(1.0f, m_BurrowPhaseTime / burrowWarn) : 1.0f, 0.0f });
    if (m_SandField.active && m_SandAge < sandWindup)
        m_Visuals.push_back({ m_SandField.center, (std::max)(m_SandField.radius, 0.5f), -1.0f, 0.0f });
}
