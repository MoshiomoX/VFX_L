// ============================================================
// BossAttacksScorpion.cpp
// 砂漠 2 面目の Boss「大サソリ」だけの技（2026-10-26。ユーザー「蝎子を先に」）。潜砂と召喚は砂の守護者から借りる。
//   毒針：向きを固めて帯の予告（溜め、尾を後ろへ反らす）→ stingSpeed で stingLength 突っ込む（尾を振り下ろす）→
//         突っ込んでいる間に Boss の近くに居れば 1 回刺さる（毒 = しばらく走りが遅い）→ その場で休む
//   鋏の薙ぎ払い：向きを固めて正面の扇の予告 → 振った瞬間、扇の中で地面近くに居れば 1 回当たり（吹き飛ばし）→ 休む
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
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }   // +Z が 0（SwarmSystem::WarnCircle と同じ）

    constexpr float kPlayerThick = 0.4f;   // プレイヤーの太さ（当たりの幅に足す）
}

// ============================================================
// 毒針
// ============================================================
void BossAttacks::StartSting(const Input& in, const GridWorld& grid)
{
    m_StingStart = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_StingHitDone = false;
}

bool BossAttacks::UpdateSting(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeDir = m_ChargeDir;
    out.chargeSpeed = 0.0f;
    if (m_MoveTime < stingWindup) return false;   // 溜め：向きを固めて止まる（帯の予告が育つ、尾が反る）
    if (prevTime < stingWindup) out.stingLunge = true;

    const float lungeT = stingLength / (std::max)(stingSpeed, 1.0f);
    const float t = m_MoveTime - stingWindup;
    if (t < lungeT)
    {
        out.chargeSpeed = stingSpeed;   // 突っ込み：GPU が m_ChargeDir へ真っ直ぐ動かす（壁は GPU が止める）
        if (!m_StingHitDone && Flat(in.player, in.bossPos) < stingHitRadius + kPlayerThick
            && std::fabs(in.playerFeetY - in.bossPos.y) < 3.0f)
        {
            Hit h;
            h.kind = BossMove::Sting;
            h.center = in.bossPos;
            h.radius = stingHitRadius;
            h.damage = stingDamage * in.damageMul;
            h.ranged = false;
            // 突っ込みの向きへ、少し横へ弾く（真正面なら進む向き）
            const Vector2 side = FlatDir(in.bossPos, in.player, m_ChargeDir);
            h.dir = side + m_ChargeDir;
            if (h.dir.LengthSquared() > 1e-4f) h.dir.Normalize(); else h.dir = m_ChargeDir;
            out.hits.push_back(h);
            m_StingHitDone = true;
            ++blasts;
        }
        return false;
    }
    return t >= lungeT + stingRest;   // 休み：その場で止まって尾を戻す
}

// ============================================================
// 鋏の薙ぎ払い
// ============================================================
void BossAttacks::StartClawSweep(const Input& in)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_ClawHitDone = false;
}

bool BossAttacks::UpdateClawSweep(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;   // 向きは始めた瞬間に固める（横へ逃げられる）
    if (m_MoveTime < clawWindup) return false;
    if (prevTime < clawWindup && !m_ClawHitDone)
    {
        m_ClawHitDone = true;
        out.clawSwung = true;
        out.clawDir = m_ChargeDir;
        const Vector2 d(in.player.x - in.bossPos.x, in.player.z - in.bossPos.z);
        const float dist = d.Length();
        if (dist <= clawRadius + kPlayerThick)
        {
            const Vector2 n = (dist > 1e-3f) ? d / dist : m_ChargeDir;
            const float cosHalf = std::cos(DirectX::XMConvertToRadians(clawHalfDeg));
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (n.Dot(m_ChargeDir) >= cosHalf && feetAbove <= clawDodgeHeight && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::ClawSweep;
                h.center = in.bossPos;
                h.radius = clawRadius;
                h.damage = clawDamage * in.damageMul;
                h.ranged = false;
                h.dir = n;   // Boss から外へ
                out.hits.push_back(h);
                ++blasts;
            }
        }
    }
    return m_MoveTime >= clawWindup + clawRest;
}

// 尾の姿勢 → Output::chargePose → VS の mode 3（flap.w × pose）：溜めで後ろへ反らし（負）、突っ込みで前へ振り下ろす（1）、休みで戻る
void BossAttacks::UpdateScorpionPose(Output& out)
{
    float pose = 0.0f;
    if (m_Move == BossMove::Sting)
    {
        if (m_MoveTime < stingWindup)
            pose = -0.45f * (std::min)(1.0f, m_MoveTime / (std::max)(stingWindup, 0.02f));
        else
        {
            const float lungeT = stingLength / (std::max)(stingSpeed, 1.0f);
            const float t = m_MoveTime - stingWindup;
            pose = (t < lungeT) ? 1.0f : (std::max)(0.0f, 1.0f - (t - lungeT) / (std::max)(stingRest, 0.05f));
        }
    }
    out.chargePose = pose;
}

// 予告の見た目：毒針 = 始点から向きへ stingLength の帯、鋏 = Boss の足元の扇
void BossAttacks::BuildScorpionVisuals(const Input& in, const GridWorld& grid)
{
    if (m_Move == BossMove::Sting && m_MoveTime < stingWindup)
    {
        Visual v;
        v.center = m_StingStart;
        v.radius = stingLength;
        v.progress = (stingWindup > 0.0f) ? m_MoveTime / stingWindup : 1.0f;
        v.yaw = YawOf(m_ChargeDir);
        v.shape = -stingWidth;
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::ClawSweep && m_MoveTime < clawWindup)
    {
        Visual v;
        v.center = Ground(grid, in.bossPos.x, in.bossPos.z);
        v.radius = clawRadius;
        v.progress = (clawWindup > 0.0f) ? m_MoveTime / clawWindup : 1.0f;
        v.yaw = YawOf(m_ChargeDir);
        v.shape = DirectX::XMConvertToRadians(clawHalfDeg);
        m_Visuals.push_back(v);
    }
}
