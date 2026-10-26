// ============================================================
// BossAttacksWolf.cpp
// 草原 2 面目の Boss「月下の狼人」だけの技（2026-10-26、ユーザー「人形の半人半獣」→ 狼人）。共通の技は出さない。
//   飛びかかり：しゃがんで溜め（着地点 = プレイヤーを追う）→ 跳ぶ：GPU には地面を着地点へ真っ直ぐ走らせ（chargeOn、
//               残りの秒で着く速さ）、VS が chargePose（0 → 1）の放物線で体を持ち上げる（BomberCB::bossJumpHeight）→ 着地で
//               輪の中に当たり（跳べば避けられる）→ 少し止まる
//   爪の三連：短い帯の予告（向きを取り直し続け、最後に固める）→ 短く突っ込む（近くに居れば斬られる）× 3（狂暴で 5）
//   遠吠え：止まって吠える → 召喚と同じ輪から雑魚が湧き、howlHasteTime 秒は技の間が howlGapMul 倍
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
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }

    constexpr float kPlayerThick = 0.4f;
}

// ============================================================
// 飛びかかり
// ============================================================
void BossAttacks::StartPounce(const Input& in)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_PounceTarget = in.player;
    m_PounceAir = false;
}

bool BossAttacks::UpdatePounce(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;
    out.jumpHeight = pounceHeight;
    out.noContact = true;   // 走り抜ける体で殴らない（殴ると無敵が付いて着地の当たりが外れる）
    if (m_MoveTime < pounceWindup)
    {
        // 溜め：しゃがんで的（プレイヤー）を追う。跳ぶ瞬間の所が着地点
        m_PounceTarget = Ground(grid, in.player.x, in.player.z);
        m_ChargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
        out.chargeDir = m_ChargeDir;
        out.chargePose = 0.0f;
        return false;
    }
    if (!m_PounceAir)
    {
        m_PounceAir = true;
        out.pounceJump = true;
    }
    const float t = m_MoveTime - pounceWindup;
    if (t < pounceAirTime)
    {
        // 空中：残りの秒で着地点へ着く速さで地面を走らせる（回読みの位置は少し遅れるので毎フレーム取り直す）
        const float remain = pounceAirTime - t;
        const float dist = Flat(m_PounceTarget, in.bossPos);
        out.chargeDir = FlatDir(in.bossPos, m_PounceTarget, m_ChargeDir);
        out.chargeSpeed = (dist > 0.3f && remain > 0.02f) ? (std::min)(pounceMaxSpeed, dist / remain) : 0.0f;
        out.chargePose = t / pounceAirTime;
        return false;
    }
    if (prevTime < pounceWindup + pounceAirTime)
    {
        // 着地：輪の中に当たり（シーンが「中に居るか・跳んでいないか」を見る = 重撃と同じ）
        Hit h;
        h.kind = BossMove::Pounce;
        h.center = m_PounceTarget;
        h.radius = pounceRadius;
        h.damage = pounceDamage * in.damageMul;
        h.ranged = true;
        out.hits.push_back(h);
        ++blasts;
    }
    out.chargePose = 0.0f;
    return m_MoveTime >= pounceWindup + pounceAirTime + pounceRest;
}

// ============================================================
// 爪の三連
// ============================================================
void BossAttacks::StartClawCombo(const Input& in, const GridWorld& grid)
{
    m_ComboIndex = 0;
    m_ComboCount = (std::max)(1, Enraged(in) ? clawComboCountEnraged : clawComboCount);
    m_ComboPhaseTime = 0.0f;
    m_ComboDashing = false;
    m_ComboHitDone = false;
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_ChargeStart = Ground(grid, in.bossPos.x, in.bossPos.z);
}

bool BossAttacks::UpdateClawCombo(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    (void)prevTime;
    out.chargeOn = true;
    out.chargeDir = m_ChargeDir;
    out.chargeSpeed = 0.0f;
    m_ComboPhaseTime += in.dt;
    if (!m_ComboDashing)
    {
        // 溜め：向きは溜めの始めに固めてある（取り直し続けると横へ走っても追い付かれて避けられなかった）。帯の始点だけ追う
        m_ChargeStart = Ground(grid, in.bossPos.x, in.bossPos.z);
        out.chargeDir = m_ChargeDir;
        if (m_ComboPhaseTime >= clawComboWindup)
        {
            m_ComboDashing = true;
            m_ComboPhaseTime = 0.0f;
            m_ComboHitDone = false;
            out.wolfSlash = true;
            out.clawDir = m_ChargeDir;
        }
        return false;
    }
    const float dashT = clawDashLength / (std::max)(clawDashSpeed, 1.0f);
    if (m_ComboPhaseTime < dashT)
    {
        out.chargeSpeed = clawDashSpeed;
        if (!m_ComboHitDone && Flat(in.player, in.bossPos) < clawComboHitRadius + kPlayerThick
            && std::fabs(in.playerFeetY - in.bossPos.y) < 3.0f)
        {
            Hit h;
            h.kind = BossMove::ClawCombo;
            h.center = in.bossPos;
            h.radius = clawComboHitRadius;
            h.damage = clawComboDamage * in.damageMul;
            h.ranged = false;
            const Vector2 side = FlatDir(in.bossPos, in.player, m_ChargeDir);
            h.dir = side + m_ChargeDir;
            if (h.dir.LengthSquared() > 1e-4f) h.dir.Normalize(); else h.dir = m_ChargeDir;
            out.hits.push_back(h);
            m_ComboHitDone = true;
            ++blasts;
        }
        return false;
    }
    ++m_ComboIndex;
    m_ComboDashing = false;
    m_ComboPhaseTime = 0.0f;
    m_ChargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);   // 次の突っ込みの向きはここで固める
    return m_ComboIndex >= m_ComboCount;
}

// ============================================================
// 遠吠え（湧く所は StartMove の Summon と同じ所で決めてある）
// ============================================================
bool BossAttacks::UpdateHowl(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    if (m_MoveTime < howlWindup) return false;
    out.summons = m_SummonPoints;
    summoned += (uint32_t)m_SummonPoints.size();
    m_SummonPoints.clear();
    out.howled = true;
    m_HowlHasteLeft = howlHasteTime;
    out.chargeOn = false;
    return true;
}

// 予告の見た目：飛びかかりの着地点の輪（溜め + 空中で育つ）、三連の短い帯（溜めの間）、遠吠えの湧く所の小さな輪
void BossAttacks::BuildWolfVisuals(const Input& in, const GridWorld& grid)
{
    (void)in; (void)grid;
    if (m_Move == BossMove::Pounce)
    {
        const float total = pounceWindup + pounceAirTime;
        if (m_MoveTime < total)
            m_Visuals.push_back({ m_PounceTarget, pounceRadius, (total > 0.0f) ? m_MoveTime / total : 1.0f, 0.0f });
    }
    if (m_Move == BossMove::ClawCombo && !m_ComboDashing)
    {
        Visual v;
        v.center = m_ChargeStart;
        v.radius = clawDashLength;
        v.progress = (clawComboWindup > 0.0f) ? m_ComboPhaseTime / clawComboWindup : 1.0f;
        v.yaw = YawOf(m_ChargeDir);
        v.shape = -clawDashWidth;
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::Howl)
    {
        const float p = (howlWindup > 0.0f) ? (std::min)(1.0f, m_MoveTime / howlWindup) : 1.0f;
        for (const Output::Summon& s : m_SummonPoints)
            m_Visuals.push_back({ s.pos, summonRingRadius, p, 0.0f });
    }
}
