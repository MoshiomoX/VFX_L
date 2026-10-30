// ============================================================
// BossAttacksStag.cpp
// 草原 3 面目の Boss「雷鳴の雄鹿」だけの中身（2026-10-26）：
//   ・突進の姿勢（溜めで頭を下げ脚を開く、走りで前傾、当てた瞬間に頭を振り上げる、終わると頭が上がる）→ Output::chargePose
//     → BomberCB::bossChargePose → SwarmEnemyVS が首（mode 3）・脚（mode 4）・全身を動かす
//   ・突進で通り過ぎた道筋の輪に落ちる雷（stagThunderWarn 秒の予告 → Ring{ thunder }。見た目・音はシーン）
// 突進そのもの（溜め・走り・当たり）は BossAttacks.cpp の Charge。同日に BossAttacks.cpp（550 行）から分けた
// 2026-10-29（ユーザー「鹿に特殊攻撃の感じが無い」→ 専用の技。共通は突進だけ残す）：
//   ・狂暴の三連突進：突進が終わったら向きを取り直し、chargeRewindup 秒溜めて次へ（chargeCountEnraged 回）
//   ・角の振り上げ：向きを固めて頭を下げる（正面の扇）→ 角ですくい上げる。扇の中で地面近くなら当たり（吹き上げはシーン）
//   ・角の雷撃：向きを固めて頭を少し下げ、扇に並ぶ帯の予告 → 角から帯に沿って雷が一斉に走る（跳んでも当たる）
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "World/GridWorld.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector3;

namespace
{
    Vector2 FlatDir(const Vector3& from, const Vector3& to, const Vector2& fallback)
    {
        Vector2 d(to.x - from.x, to.z - from.z);
        const float l = d.Length();
        return (l > 1e-3f) ? d / l : fallback;
    }
    Vector3 Ground(const GridWorld& grid, float x, float z) { return Vector3(x, grid.SampleHeight(x, z), z); }
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }

    constexpr float kPlayerThick = 0.4f;   // プレイヤーの太さ（当たりの幅に足す）
    constexpr float kBoltAim = 0.7f;       // 角の雷撃の溜めで頭を下げる量（chargePose。1 = 突進の溜めと同じ）
}

void BossAttacks::UpdateStagPose(const Input& in, Output& out)
{
    float pose = 0.0f;
    if (m_Move == BossMove::Charge)
    {
        // 三連の 2 回目からの溜めは頭を下げたまま（間で頭を上げない）
        pose = (m_ChargeIndex > 0 || m_ChargeWindupNow <= 0.0f) ? 1.0f : (std::min)(1.0f, ChargeT() / m_ChargeWindupNow);
        if (m_ChargeTossT >= 0.0f)
        {
            // 当てた瞬間：頭を振り上げて戻す（1 → 1.3 → 1）
            m_ChargeTossT += in.dt;
            const float u = (std::min)(1.0f, m_ChargeTossT / (std::max)(stagTossTime, 0.02f));
            pose = 1.0f + 0.3f * std::sin(u * 3.14159265f);
            if (u >= 1.0f) m_ChargeTossT = -1.0f;
        }
    }
    else if (m_Move == BossMove::AntlerToss)
    {
        // 溜めで頭を下げ（0 → 1）、振り上げ（1 → 1.3 → 1、突進で当てた時より少し長く）、休みで戻る（1 → 0）
        const float t = m_MoveTime - tossWindup;
        const float up = (std::max)(stagTossTime * 1.5f, 0.05f);
        if (t < 0.0f)
            pose = (std::min)(1.0f, m_MoveTime / (std::max)(tossWindup, 0.02f));
        else if (t < up)
            pose = 1.0f + 0.3f * std::sin(t / up * 3.14159265f);
        else
            pose = (std::max)(0.0f, 1.0f - (t - up) / (std::max)(tossRest - up, 0.05f));
    }
    else if (m_Move == BossMove::AntlerBolt)
    {
        // 溜めで頭を少し下げて角を前へ向ける（角の縁の光も強くなる）、撃った後に戻る
        if (m_MoveTime < boltWindup)
            pose = kBoltAim * (std::min)(1.0f, m_MoveTime / (std::max)(boltWindup, 0.02f));
        else
            pose = kBoltAim * (std::max)(0.0f, 1.0f - (m_MoveTime - boltWindup) / (std::max)(boltRest, 0.05f));
    }
    else if (m_ChargeRecover > 0.0f)
    {
        // 突進が終わった：頭が上がる（1 → 0）
        m_ChargeRecover -= in.dt;
        pose = (std::max)(0.0f, m_ChargeRecover / (std::max)(stagRecoverTime, 0.02f));
    }
    out.chargePose = pose;
}

// 突進の走りの間に毎フレーム：通り過ぎた道筋の輪に雷の予告を置く。回読みの位置は 2〜3 フレーム遅れるので、
// 走った秒 × 速さ（CPU の予測）で「何個目まで通ったか」を判る
void BossAttacks::StagThunderLane(const Input& in, const GridWorld& grid, Output& out)
{
    if (!stagThunder) return;
    const float ran = (ChargeT() - m_ChargeWindupNow) * chargeSpeed;
    const int n = (std::max)(1, (int)(chargeLength / (std::max)(chargeLaneSpacing, 0.5f)));
    while (m_ChargeLanesPassed < n)
    {
        const float s = chargeLaneSpacing * ((float)m_ChargeLanesPassed + 0.5f);
        if (ran < s) break;
        const float x = m_ChargeStart.x + m_ChargeDir.x * s, z = m_ChargeStart.z + m_ChargeDir.y * s;
        Ring r;
        r.center = Vector3(x, grid.SampleHeight(x, z), z);
        r.radius = stagThunderRadius;
        r.warn = (std::max)(stagThunderWarn, 0.05f);
        r.damage = stagThunderDamage * in.damageMul;
        r.kind = BossMove::Charge;
        r.thunder = true;
        m_Rings.push_back(r);
        out.thunderStruck = true;
        ++m_ChargeLanesPassed;
    }
}

// 狂暴の三連：突進が 1 回終わった所で、まだ回数が残っていれば向きを取り直して次の溜めへ
bool BossAttacks::StagNextCharge(const Input& in, const GridWorld& grid)
{
    if (m_ChargeIndex + 1 >= m_ChargeCount) return false;
    ++m_ChargeIndex;
    m_ChargeT0 = m_MoveTime;
    m_ChargeWindupNow = (std::max)(chargeRewindup, 0.05f);
    m_ChargeStart = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_ChargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    m_ChargeHitDone = false;
    m_ChargeLanesPassed = 0;
    m_ChargeTossT = -1.0f;
    return true;
}

// ============================================================
// 角の振り上げ
// ============================================================
void BossAttacks::StartAntlerToss(const Input& in, const GridWorld& grid)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_StagFoot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_TossHitDone = false;
}

bool BossAttacks::UpdateAntlerToss(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;   // 向きは始めた瞬間に固める（横・後ろへ逃げられる）
    if (m_MoveTime < tossWindup) return false;
    if (prevTime < tossWindup && !m_TossHitDone)
    {
        m_TossHitDone = true;
        out.antlerTossed = true;
        out.clawDir = m_ChargeDir;
        const Vector2 d(in.player.x - in.bossPos.x, in.player.z - in.bossPos.z);
        const float dist = d.Length();
        if (dist <= tossRadius + kPlayerThick)
        {
            const Vector2 n = (dist > 1e-3f) ? d / dist : m_ChargeDir;
            const float cosHalf = std::cos(DirectX::XMConvertToRadians(tossHalfDeg));
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (n.Dot(m_ChargeDir) >= cosHalf && feetAbove <= tossDodgeHeight && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::AntlerToss;
                h.center = in.bossPos;
                h.radius = tossRadius;
                h.damage = tossDamage * in.damageMul;
                h.ranged = false;
                h.dir = n;   // Boss から外へ（シーンが高く吹き上げる）
                out.hits.push_back(h);
                ++blasts;
            }
        }
    }
    return m_MoveTime >= tossWindup + tossRest;
}

// ============================================================
// 角の雷撃
// ============================================================
void BossAttacks::StartAntlerBolt(const Input& in, const GridWorld& grid)
{
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_StagFoot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_BoltFired = false;
    m_BoltCountNow = (std::max)(1, Enraged(in) ? boltCountEnraged : boltCount);
}

bool BossAttacks::UpdateAntlerBolt(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;
    if (m_MoveTime < boltWindup) return false;
    if (prevTime < boltWindup && !m_BoltFired)
    {
        m_BoltFired = true;
        ++blasts;
        bool hit = false;
        const float base = YawOf(m_ChargeDir);
        const float fan = DirectX::XMConvertToRadians(boltFanDeg);
        const Vector2 rel(in.player.x - m_StagFoot.x, in.player.z - m_StagFoot.z);
        const float groundAtPlayer = grid.SampleHeight(in.player.x, in.player.z);
        const float feetAbove = in.playerFeetY - groundAtPlayer;
        for (int k = 0; k < m_BoltCountNow; ++k)
        {
            const float yaw = base + fan * ((float)k - (float)(m_BoltCountNow - 1) * 0.5f);
            const Vector2 d(std::sin(yaw), std::cos(yaw));
            Output::BoltLine l;
            l.start = m_StagFoot;
            l.end = Ground(grid, m_StagFoot.x + d.x * boltLength, m_StagFoot.z + d.y * boltLength);
            out.boltLines.push_back(l);
            if (hit) continue;
            // 当たり：帯の中（線からの横の距離・線に沿った長さ）。雷は高いので跳んでも当たる、別の段（台地の上下）は当たらない
            const float along = rel.Dot(d);
            const float cross = rel.x * d.y - rel.y * d.x;
            if (along < -kPlayerThick || along > boltLength + kPlayerThick) continue;
            if (std::fabs(cross) > boltWidth * 0.5f + kPlayerThick) continue;
            if (feetAbove > 3.0f || feetAbove < -2.0f || std::fabs(groundAtPlayer - m_StagFoot.y) > 3.0f) continue;
            Hit h;
            h.kind = BossMove::AntlerBolt;
            h.center = m_StagFoot;
            h.radius = boltWidth * 0.5f;
            h.damage = boltDamage * in.damageMul;
            h.ranged = false;
            // 線の横へ弾く（少し先へ）
            Vector2 side(d.y, -d.x);
            if (cross < 0.0f) side = -side;
            h.dir = side + d * 0.5f;
            h.dir.Normalize();
            out.hits.push_back(h);
            hit = true;
        }
    }
    return m_MoveTime >= boltWindup + boltRest;
}

// 予告の見た目：振り上げ = 足元の扇、雷撃 = 足元から扇に並ぶ帯
void BossAttacks::BuildStagVisuals(const GridWorld& grid)
{
    (void)grid;
    if (tossWarn && m_Move == BossMove::AntlerToss && m_MoveTime < tossWindup)
    {
        Visual v;
        v.center = m_StagFoot;
        v.radius = tossRadius;
        v.progress = (tossWindup > 0.0f) ? m_MoveTime / tossWindup : 1.0f;
        v.yaw = YawOf(m_ChargeDir);
        v.shape = DirectX::XMConvertToRadians(tossHalfDeg);
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::AntlerBolt && m_MoveTime < boltWindup)
    {
        const float base = YawOf(m_ChargeDir);
        const float fan = DirectX::XMConvertToRadians(boltFanDeg);
        for (int k = 0; k < m_BoltCountNow; ++k)
        {
            Visual v;
            v.center = m_StagFoot;
            v.radius = boltLength;
            v.progress = (boltWindup > 0.0f) ? m_MoveTime / boltWindup : 1.0f;
            v.yaw = base + fan * ((float)k - (float)(m_BoltCountNow - 1) * 0.5f);
            v.shape = -boltWidth;
            m_Visuals.push_back(v);
        }
    }
}
