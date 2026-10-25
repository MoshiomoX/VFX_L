// ============================================================
// BossAttacks.cpp
// 技の流れ（選ぶ → 出す → 終わったら間を空ける）と、輪・衝撃波・突進の当たり。説明は BossAttacks.h
// 砂の守護者の技は BossAttacksSand.cpp、調整パネルは BossAttacksPanel.cpp
// ============================================================
#include "Enemy/BossAttacks.h"
#include "Swarm/SwarmTypes.h"
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

void BossAttacks::Reset()
{
    m_Rings.clear();
    m_Visuals.clear();
    m_Move = BossMove::None;
    m_MoveTime = 0.0f;
    m_Gap = firstDelay;
    m_WasAlive = false;
    m_DebugMove = BossMove::None;
    m_Pending = 0;
    m_PendingTimer = 0.0f;
    m_WaveOn = false;
    m_WaveHitDone = false;
    m_WaveRadius = 0.0f;
    m_ChargeHitDone = false;
    m_SummonPoints.clear();
    m_SpearDirs.clear();
    m_SpearHitDone = false;
    m_BurrowPhase = 0;
    m_BurrowPhaseTime = 0.0f;
    m_BurrowSink = 0.0f;
    m_SandField = QuicksandField{};
    m_SandAge = 0.0f;
    brain.Reset();
}

// ============================================================
// 面毎の Boss。技の有り / 無しは BossBrain の rules[].enabled、他の数値（冷却・重み）は BossBrain の既定のまま
// ============================================================
void BossAttacks::Configure(BossType t)
{
    m_Type = t;
    const bool sand = (t == BossType::SandGuardian);
    brain.rules[(int)BossMove::SandSpear].enabled = sand;
    brain.rules[(int)BossMove::Burrow].enabled = sand;
    brain.rules[(int)BossMove::Quicksand].enabled = sand;
    if (sand)
    {
        // 砂の守護者：トゲ玉と砂潜り（砂漠の敵）を呼ぶ。体は自分の砂岩の皮（BossLooks）なので色は掛けない
        summonPicks[0] = { Swarm::kEnemyKindSpikeBall, 0.5f };
        summonPicks[1] = { Swarm::kEnemyKindBurrower, 0.5f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else
    {
        summonPicks[0] = { Swarm::kEnemyKindMob, 0.7f };
        summonPicks[1] = { Swarm::kEnemyKindBomber, 0.3f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };   // 苔むした岩の皮（BossLooks。2026-10-25 まではゾンビの皮 × 紫）
    }
}

uint32_t BossAttacks::PickSummonKind()
{
    float total = 0.0f;
    for (const SummonPick& p : summonPicks) total += (std::max)(p.weight, 0.0f);
    if (total <= 0.0f) return Swarm::kEnemyKindMob;
    std::uniform_real_distribution<float> u(0.0f, total);
    float pick = u(m_Rng);
    for (const SummonPick& p : summonPicks)
    {
        const float w = (std::max)(p.weight, 0.0f);
        if (w <= 0.0f) continue;
        if (pick < w) return p.kind;
        pick -= w;
    }
    return summonPicks[0].kind;
}

void BossAttacks::Update(const Input& in, const GridWorld& grid, Output& out)
{
    // Boss が居なくなったら（倒した・まだ呼んでいない）輪も帯も消す
    if (!in.bossAlive)
    {
        if (m_WasAlive || !m_Rings.empty() || m_WaveOn) Reset();
        return;
    }
    if (!m_WasAlive)
    {
        Reset();
        m_WasAlive = true;
    }
    brain.Tick(in.dt);

    // ---- 輪を育て、縁に届いた物は爆発 ----
    for (Ring& r : m_Rings)
    {
        const float before = r.age;
        r.age += in.dt;
        // 投石雨：爆発の rockFallTime 秒前に岩が空から落ち始める
        const float fall = r.warn - rockFallTime;
        if (r.kind == BossMove::RockRain && before < fall && r.age >= fall)
            out.rockFalls.push_back(r.center);
    }
    for (auto it = m_Rings.begin(); it != m_Rings.end();)
    {
        if (it->age >= it->warn)
        {
            Hit h;
            h.kind = it->kind;
            h.center = it->center;
            h.radius = it->radius;
            h.damage = it->damage;
            h.ranged = true;
            out.hits.push_back(h);
            ++blasts;
            it = m_Rings.erase(it);
        }
        else
            ++it;
    }

    UpdateWave(in, grid, out);
    UpdateQuicksandField(in.dt);
    if (m_Move != BossMove::None) UpdateMove(in, grid, out);
    if (m_Move != BossMove::Burrow) m_BurrowSink = 0.0f;   // 潜砂の途中で技が切れても地上へ戻す
    out.bossBurrow = m_BurrowSink;

    // ---- 次の技（技を出していない間だけ間を数える）----
    if (m_Move == BossMove::None)
    {
        const float dist = Flat(in.player, in.bossPos);
        const bool inRange = dist <= range;
        if (m_DebugMove != BossMove::None)
        {
            const BossMove m = m_DebugMove;
            m_DebugMove = BossMove::None;
            StartMove(m, in, grid, out);
        }
        else if (enabled && inRange)
        {
            m_Gap -= in.dt;
            if (m_Gap <= 0.0f)
            {
                const BossMove m = brain.Choose(dist, m_Rng);
                if (m != BossMove::None) StartMove(m, in, grid, out);
            }
        }
        else if (!inRange && m_Gap < 0.0f)
            m_Gap = 0.0f;   // 離れている間は溜めない（近付いた瞬間に 1 回だけ）
    }

    BuildVisuals(in, grid);
}

void BossAttacks::StartMove(BossMove m, const Input& in, const GridWorld& grid, Output& out)
{
    m_Move = m;
    m_MoveTime = 0.0f;
    brain.OnStarted(m, Enraged(in));
    out.started = m;
    ++volleys;
    if (m != BossMove::None) ++moveCounts[(int)m];

    switch (m)
    {
    case BossMove::Slam:
        m_Pending = (std::max)(1, count);
        m_PendingTimer = 0.0f;
        break;
    case BossMove::RockRain:
        m_Pending = (std::max)(1, rockCount);
        m_PendingTimer = 0.0f;
        m_RainCenter = Ground(grid, in.player.x, in.player.z);
        break;
    case BossMove::Shockwave:
        break;
    case BossMove::Charge:
        m_ChargeStart = Ground(grid, in.bossPos.x, in.bossPos.z);
        m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
        m_ChargeHitDone = false;
        break;
    case BossMove::Summon:
    {
        // Boss の周りに等間隔（少しずらす）。歩けないマスなら少し内側を試し、駄目なら飛ばす
        m_SummonPoints.clear();
        m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
        const int n = (std::max)(1, Enraged(in) ? summonCountEnraged : summonCount);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        const float base = u01(m_Rng) * 6.2831853f;
        for (int k = 0; k < n; ++k)
        {
            const float a = base + 6.2831853f * ((float)k + (u01(m_Rng) - 0.5f) * 0.5f) / (float)n;
            const float d = summonRingMin + (summonRingMax - summonRingMin) * u01(m_Rng);
            for (float s = 1.0f; s > 0.3f; s -= 0.25f)
            {
                const float x = in.bossPos.x + std::cos(a) * d * s, z = in.bossPos.z + std::sin(a) * d * s;
                int gx = 0, gz = 0;
                grid.WorldToCell(Vector3(x, 0.0f, z), gx, gz);
                if (!grid.IsWalkable(gx, gz)) continue;
                Output::Summon sp;
                sp.pos = Ground(grid, x, z);
                sp.kind = PickSummonKind();
                m_SummonPoints.push_back(sp);
                break;
            }
        }
        out.ringsPlaced += (int)m_SummonPoints.size();
        ringsPlaced += (uint32_t)m_SummonPoints.size();
        break;
    }
    case BossMove::SandSpear:
        StartSandSpear(in, grid);
        out.ringsPlaced += (int)m_SpearDirs.size();
        break;
    case BossMove::Burrow:
        StartBurrow(in);
        out.burrowSank = true;
        break;
    case BossMove::Quicksand:
        StartQuicksand(in, grid);
        out.quicksandStarted = true;
        out.quicksandPos = m_SandField.center;
        break;
    default:
        break;
    }
}

void BossAttacks::UpdateMove(const Input& in, const GridWorld& grid, Output& out)
{
    const float prevTime = m_MoveTime;
    m_MoveTime += in.dt;
    bool done = false;

    switch (m_Move)
    {
    case BossMove::Slam:
    case BossMove::RockRain:
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
        done = (m_Pending == 0);
        break;
    }

    case BossMove::Shockwave:
        // 溜め：その場で止まってプレイヤーの方を向く
        out.chargeOn = true;
        out.chargeSpeed = 0.0f;
        out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
        if (m_MoveTime >= waveWindup)
        {
            m_WaveOn = true;
            m_WaveHitDone = false;
            m_WaveRadius = 0.0f;
            m_WaveCenter = Ground(grid, in.bossPos.x, in.bossPos.z);
            out.waveLaunched = true;
            out.wavePos = m_WaveCenter;
            done = true;
        }
        break;

    case BossMove::Charge:
    {
        out.chargeOn = true;
        out.chargeDir = m_ChargeDir;
        if (m_MoveTime < chargeWindup)
        {
            out.chargeSpeed = 0.0f;   // 溜め：道筋の輪が育つ間は止まる
            break;
        }
        if (prevTime < chargeWindup) out.chargeGo = true;
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
        }
        const float dashTime = chargeLength / (std::max)(chargeSpeed, 1.0f);
        if (m_MoveTime >= chargeWindup + dashTime)
        {
            out.chargeOn = false;
            done = true;
        }
        break;
    }

    case BossMove::Summon:
        // 吠えている間はその場で止まってプレイヤーの方を向く → 時間が来たら輪から湧く
        out.chargeOn = true;
        out.chargeSpeed = 0.0f;
        out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
        if (m_MoveTime >= summonWindup)
        {
            out.summons = m_SummonPoints;
            summoned += (uint32_t)m_SummonPoints.size();
            m_SummonPoints.clear();
            out.chargeOn = false;
            done = true;
        }
        break;

    case BossMove::SandSpear:
        done = UpdateSandSpear(in, grid, out, prevTime);
        break;
    case BossMove::Burrow:
        done = UpdateBurrow(in, grid, out);
        break;
    case BossMove::Quicksand:
        done = UpdateQuicksandMove(in, out);
        break;

    default:
        done = true;
        break;
    }

    if (done) FinishMove(in);
}

void BossAttacks::FinishMove(const Input& in)
{
    m_Move = BossMove::None;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    if (Enraged(in))
        m_Gap = (u01(m_Rng) < comboChance) ? 0.3f : gapEnraged;
    else
        m_Gap = gap;
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

void BossAttacks::BuildVisuals(const Input& in, const GridWorld& grid)
{
    m_Visuals.clear();
    for (const Ring& r : m_Rings)
        m_Visuals.push_back({ r.center, r.radius, (r.warn > 0.0f) ? r.age / r.warn : 1.0f, 0.0f });
    if (m_Move == BossMove::Shockwave)
        m_Visuals.push_back({ Ground(grid, in.bossPos.x, in.bossPos.z), waveWindupRadius,
            (waveWindup > 0.0f) ? m_MoveTime / waveWindup : 1.0f, 0.0f });
    if (m_WaveOn)
        m_Visuals.push_back({ m_WaveCenter, m_WaveRadius, -1.0f, waveBand });
    if (m_Move == BossMove::Charge)
    {
        const float p = (chargeWindup > 0.0f) ? (std::min)(1.0f, m_MoveTime / chargeWindup) : 1.0f;
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
    BuildSandVisuals(grid);
}
