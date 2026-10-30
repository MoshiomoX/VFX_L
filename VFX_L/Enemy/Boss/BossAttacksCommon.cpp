// ============================================================
// BossAttacksCommon.cpp
// 共通の技（重撃・投石雨・衝撃波・突進・召喚）の中身と、その予告の見た目。
// 2026-10-29 に BossAttacks.cpp（雄鹿の三連突進を足して 594 行）から分けた。中身は変えていない（break / done を return にしただけ）。
// 流れ（選ぶ → StartMove → UpdateMove → 間）は BossAttacks.cpp、Boss 毎の技は BossAttacks<Boss>.cpp
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
}

// 重撃・投石雨：間隔毎に輪を置く。全部置いたら終わり（輪の爆発は Update が見る）
bool BossAttacks::UpdateRingVolley(const Input& in, const GridWorld& grid, Output& out)
{
    const bool rain = (m_Move == BossMove::RockRain);
    m_PendingTimer -= in.dt;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    while (m_Pending > 0 && m_PendingTimer <= 0.0f)
    {
        Ring r;
        r.kind = m_Move;
        if (!rain)
        {
            // 重撃：置いた瞬間のプレイヤーの足元（動いていれば外れる）
            r.center = Ground(grid, in.player.x, in.player.z);
            r.radius = radius;
            r.warn = warnTime;
            r.damage = damage * in.damageMul;
        }
        else
        {
            // 投石雨：1 つ目は今の足元、残りは始めた時の足元の周り（円の中に一様に）
            Vector3 c = Ground(grid, in.player.x, in.player.z);
            if (m_Pending < rockCount)
            {
                const float a = u01(m_Rng) * 6.2831853f;
                const float d = std::sqrt(u01(m_Rng)) * rockSpread;
                c = Ground(grid, m_RainCenter.x + std::cos(a) * d, m_RainCenter.z + std::sin(a) * d);
            }
            r.center = c;
            r.radius = rockRadius;
            r.warn = rockWarn;
            r.damage = rockDamage * in.damageMul;
        }
        m_Rings.push_back(r);
        --m_Pending;
        ++out.ringsPlaced;
        ++ringsPlaced;
        m_PendingTimer += rain ? rockSpacing : spacing;
    }
    return m_Pending == 0;
}

// 衝撃波の溜め：その場で止まってプレイヤーの方を向く → 帯を出したら終わり（帯は UpdateWave が広げる）
bool BossAttacks::UpdateShockwaveMove(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    if (m_MoveTime < waveWindup) return false;
    m_WaveOn = true;
    m_WaveHitDone = false;
    m_WaveRadius = 0.0f;
    m_WaveCenter = Ground(grid, in.bossPos.x, in.bossPos.z);
    out.waveLaunched = true;
    out.wavePos = m_WaveCenter;
    return true;
}

// 突進：溜め → 走る（近くに居れば 1 回当たり）。雄鹿は道筋に雷、狂暴なら向きを取り直して次の突進（BossAttacksStag.cpp）
bool BossAttacks::UpdateChargeMove(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    // 時間はこの回の溜めの始めから（雄鹿の狂暴の三連では 2 回目・3 回目がある）
    const float ct = ChargeT(), prevCt = prevTime - m_ChargeT0;
    out.chargeOn = true;
    out.chargeDir = m_ChargeDir;
    if (ct < m_ChargeWindupNow)
    {
        out.chargeSpeed = 0.0f;   // 溜め：道筋の輪が育つ間は止まる
        return false;
    }
    if (prevCt < m_ChargeWindupNow) out.chargeGo = true;
    out.chargeSpeed = chargeSpeed;
    // 当たり：走っている Boss の近く（高さも近い）にプレイヤーが居れば 1 回だけ
    if (!m_ChargeHitDone && Flat(in.player, in.bossPos) < chargeHitRadius
        && std::fabs(in.playerFeetY - in.bossPos.y) < 3.0f)
    {
        Hit h;
        h.kind = BossMove::Charge;
        h.center = in.bossPos;
        h.radius = chargeHitRadius;
        h.damage = chargeDamage * in.damageMul;
        h.ranged = false;
        // 突進の向きに横へ弾く（真正面なら進む向き）
        const Vector2 side = FlatDir(in.bossPos, in.player, m_ChargeDir);
        h.dir = side + m_ChargeDir;
        if (h.dir.LengthSquared() > 1e-4f) h.dir.Normalize(); else h.dir = m_ChargeDir;
        out.hits.push_back(h);
        m_ChargeHitDone = true;
        if (m_Type == BossType::Stag) m_ChargeTossT = 0.0f;   // 角で跳ね上げる（頭の振り上げ、chargePose 1 → 1.3）
    }
    // 雷鳴の雄鹿：通り過ぎた道筋の輪に雷の予告（BossAttacksStag.cpp）
    if (m_Type == BossType::Stag) StagThunderLane(in, grid, out);
    const float dashTime = chargeLength / (std::max)(chargeSpeed, 1.0f);
    if (ct < m_ChargeWindupNow + dashTime) return false;
    if (m_Type == BossType::Stag && StagNextCharge(in, grid))
    {
        out.chargeSpeed = 0.0f;   // 次の突進の溜め（BossAttacksStag.cpp が向きを取り直した）
        out.chargeDir = m_ChargeDir;
        return false;
    }
    out.chargeOn = false;
    if (m_Type == BossType::Stag) m_ChargeRecover = stagRecoverTime;
    return true;
}

// 召喚：吠えている間はその場で止まってプレイヤーの方を向く → 時間が来たら輪から湧く
bool BossAttacks::UpdateSummonMove(const Input& in, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
    if (m_MoveTime < summonWindup) return false;
    out.summons = m_SummonPoints;
    summoned += (uint32_t)m_SummonPoints.size();
    m_SummonPoints.clear();
    out.chargeOn = false;
    return true;
}

// 衝撃波：帯を広げ、通った瞬間に足が地面近くならプレイヤーに当たり（1 回だけ）
void BossAttacks::UpdateWave(const Input& in, const GridWorld& grid, Output& out)
{
    if (!m_WaveOn) return;
    const float prev = m_WaveRadius;
    m_WaveRadius += waveSpeed * in.dt;
    if (!m_WaveHitDone)
    {
        const float d = Flat(in.player, m_WaveCenter);
        const float half = waveBand * 0.5f + 0.4f;   // 帯の半分 + プレイヤーの太さ
        if (d >= prev - half && d <= m_WaveRadius + half)
        {
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (feetAbove <= waveDodgeHeight && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::Shockwave;
                h.center = m_WaveCenter;
                h.radius = m_WaveRadius;
                h.damage = waveDamage * in.damageMul;
                h.ranged = false;
                h.dir = FlatDir(m_WaveCenter, in.player, Vector2(0.0f, 1.0f));
                out.hits.push_back(h);
                m_WaveHitDone = true;
            }
        }
    }
    if (m_WaveRadius >= waveMaxRadius) m_WaveOn = false;
}

// 予告の見た目：輪（重撃・投石雨・雄鹿の雷）、衝撃波の溜めの輪と帯、突進の道筋、召喚の輪
void BossAttacks::BuildCommonVisuals(const Input& in, const GridWorld& grid)
{
    for (const Ring& r : m_Rings)
        m_Visuals.push_back({ r.center, r.radius, (r.warn > 0.0f) ? r.age / r.warn : 1.0f, 0.0f });
    if (m_Move == BossMove::Shockwave)
        m_Visuals.push_back({ Ground(grid, in.bossPos.x, in.bossPos.z), waveWindupRadius,
            (waveWindup > 0.0f) ? m_MoveTime / waveWindup : 1.0f, 0.0f });
    if (m_WaveOn)
        m_Visuals.push_back({ m_WaveCenter, m_WaveRadius, -1.0f, waveBand });
    if (m_Move == BossMove::Charge)
    {
        const float p = (m_ChargeWindupNow > 0.0f) ? (std::min)(1.0f, ChargeT() / m_ChargeWindupNow) : 1.0f;
        const int n = (std::max)(1, (int)(chargeLength / (std::max)(chargeLaneSpacing, 0.5f)));
        for (int i = 0; i < n; ++i)
        {
            const float s = chargeLaneSpacing * ((float)i + 0.5f);
            m_Visuals.push_back({ Ground(grid, m_ChargeStart.x + m_ChargeDir.x * s, m_ChargeStart.z + m_ChargeDir.y * s),
                chargeLaneRadius, p, 0.0f });
        }
    }
    if (m_Move == BossMove::Summon)
    {
        const float p = (summonWindup > 0.0f) ? (std::min)(1.0f, m_MoveTime / summonWindup) : 1.0f;
        for (const Output::Summon& s : m_SummonPoints)
            m_Visuals.push_back({ s.pos, summonRingRadius, p, 0.0f });
    }
}
