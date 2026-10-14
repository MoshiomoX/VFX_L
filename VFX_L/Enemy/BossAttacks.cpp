// ============================================================
// BossAttacks.cpp
// 技の流れ（選ぶ → 出す → 終わったら間を空ける）と、輪・衝撃波・突進の当たり。説明は BossAttacks.h
// ============================================================
#include "Enemy/BossAttacks.h"
#include "World/GridWorld.h"
#include "imgui.h"

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
    brain.Reset();
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
    if (m_Move != BossMove::None) UpdateMove(in, grid, out);

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
                sp.bomber = (u01(m_Rng) < summonBomberChance);
                m_SummonPoints.push_back(sp);
                break;
            }
        }
        out.ringsPlaced += (int)m_SummonPoints.size();
        ringsPlaced += (uint32_t)m_SummonPoints.size();
        break;
    }
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

    default:
        done = true;
        break;
    }

    if (done)
    {
        m_Move = BossMove::None;
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        if (Enraged(in))
            m_Gap = (u01(m_Rng) < comboChance) ? 0.3f : gapEnraged;
        else
            m_Gap = gap;
    }
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
}

void BossAttacks::DrawImGui()
{
    ImGui::TextColored(ImVec4(0.85f, 0.4f, 1.0f, 1), "Boss Attacks");
    ImGui::Text("move %s (%.1f s)  next in %.1f s  rings %d  wave %s  | volleys %u blasts %u",
        BossMoveName(m_Move), m_MoveTime, m_Gap, (int)m_Rings.size(), m_WaveOn ? "on" : "off", volleys, blasts);
    ImGui::Checkbox("Enabled##boss", &enabled);
    ImGui::DragFloat("First Delay (s)##boss", &firstDelay, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Gap (s)##boss", &gap, 0.1f, 0.0f, 60.0f);
    ImGui::DragFloat("Gap Enraged (s)##boss", &gapEnraged, 0.1f, 0.0f, 60.0f);
    ImGui::SliderFloat("Enrage HP Ratio##boss", &enrageHpRatio, 0.0f, 1.0f);
    ImGui::SliderFloat("Combo Chance (enraged)##boss", &comboChance, 0.0f, 1.0f);
    ImGui::DragFloat("Range (m)##boss", &range, 0.5f, 1.0f, 200.0f);
    if (ImGui::Button("Slam Now")) QueueMove(BossMove::Slam);
    ImGui::SameLine();
    if (ImGui::Button("Rock Rain Now")) QueueMove(BossMove::RockRain);
    ImGui::SameLine();
    if (ImGui::Button("Shockwave Now")) QueueMove(BossMove::Shockwave);
    ImGui::SameLine();
    if (ImGui::Button("Charge Now")) QueueMove(BossMove::Charge);
    ImGui::SameLine();
    if (ImGui::Button("Summon Now")) QueueMove(BossMove::Summon);

    if (ImGui::TreeNode("Choosing (BossBrain)"))
    {
        brain.DrawImGui();
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Slam"))
    {
        ImGui::DragInt("Rings", &count, 1, 1, 12);
        ImGui::DragFloat("Spacing (s)", &spacing, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("Warn Time (s)", &warnTime, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Radius (m)", &radius, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Damage (base)", &damage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Rock Rain"))
    {
        ImGui::DragInt("Rocks", &rockCount, 1, 1, 40);
        ImGui::DragFloat("Spacing (s)", &rockSpacing, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Warn Time (s)", &rockWarn, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Radius (m)", &rockRadius, 0.05f, 0.3f, 10.0f);
        ImGui::DragFloat("Spread (m)", &rockSpread, 0.1f, 0.0f, 40.0f);
        ImGui::DragFloat("Damage (base)", &rockDamage, 0.5f, 0.0f, 500.0f);
        ImGui::DragFloat("Fall Time (s)", &rockFallTime, 0.01f, 0.0f, 2.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Shockwave"))
    {
        ImGui::DragFloat("Windup (s)", &waveWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Windup Ring (m)", &waveWindupRadius, 0.1f, 0.5f, 20.0f);
        ImGui::DragFloat("Speed (m/s)", &waveSpeed, 0.1f, 1.0f, 60.0f);
        ImGui::DragFloat("Max Radius (m)", &waveMaxRadius, 0.5f, 2.0f, 100.0f);
        ImGui::DragFloat("Band (m)", &waveBand, 0.05f, 0.1f, 10.0f);
        ImGui::DragFloat("Dodge Height (m)", &waveDodgeHeight, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Damage (base)", &waveDamage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Charge"))
    {
        ImGui::DragFloat("Windup (s)", &chargeWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Speed (m/s)", &chargeSpeed, 0.5f, 1.0f, 80.0f);
        ImGui::DragFloat("Length (m)", &chargeLength, 0.5f, 1.0f, 100.0f);
        ImGui::DragFloat("Lane Ring (m)", &chargeLaneRadius, 0.05f, 0.2f, 10.0f);
        ImGui::DragFloat("Lane Spacing (m)", &chargeLaneSpacing, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Hit Radius (m)", &chargeHitRadius, 0.05f, 0.5f, 10.0f);
        ImGui::DragFloat("Damage (base)", &chargeDamage, 0.5f, 0.0f, 500.0f);
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Summon"))
    {
        ImGui::DragFloat("Windup (s)", &summonWindup, 0.05f, 0.0f, 5.0f);
        ImGui::DragInt("Count", &summonCount, 1, 1, 30);
        ImGui::DragInt("Count Enraged", &summonCountEnraged, 1, 1, 30);
        ImGui::SliderFloat("Bomber Chance", &summonBomberChance, 0.0f, 1.0f);
        ImGui::DragFloat2("Ring Min / Max (m)", &summonRingMin, 0.1f, 0.5f, 30.0f);
        ImGui::DragFloat("Spawn Ring (m)", &summonRingRadius, 0.05f, 0.2f, 5.0f);
        ImGui::TreePop();
    }
}
