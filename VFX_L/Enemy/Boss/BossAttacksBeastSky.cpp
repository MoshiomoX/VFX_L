// ============================================================
// BossAttacksBeastSky.cpp
// 石の守護獣の追加の 2 技（2026-10-29、ユーザー「技が少ない、あと 2 つ」→ 天からの圧殺・石羽の散弾）。説明・数値は BossBeast.h。
// BossAttacksBeast.cpp（段の入れ替え・飛び・翼・石化・最初の 4 技）が 500 行を越えるので分けた
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
    float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }
    Vector2 Rotate(const Vector2& v, float rad)
    {
        const float c = std::cos(rad), s = std::sin(rad);
        return Vector2(v.x * c - v.y * s, v.x * s + v.y * c);
    }
    float Approach(float v, float target, float step)
    {
        return (v < target) ? (std::min)(v + step, target) : (std::max)(v - step, target);
    }

    constexpr float kPlayerThick = 0.4f;
    constexpr float kWingRaised = 1.3f;    // BossAttacksBeast.cpp と同じ角度
    constexpr float kWingTucked = 0.95f;
    constexpr float kWingBeat = -0.5f;
    constexpr float kWingFolded = 1.15f;
    constexpr float kShardLift = 1.0f;     // 羽根が飛ぶ高さ（地面の上 m。プレイヤーの胸）
    constexpr float kShardDrop = 2.5f;     // 撃った高さからこの距離の間に羽根の高さへ下りる（指数）
    constexpr float kShardMuzzle = 1.8f;   // 羽根が出る所（Boss の足元から前へ m）
}

// ============================================================
// 天からの圧殺：0 舞い上がる（影は Boss の足元から出てプレイヤーを追い始める）→ 1 影が crushFollowSpeed で追う（走りより速い）
// → 2 影が止まる（ここで影から走って出る）→ 3 落ちる → 叩き潰し（影の中で地面近くなら当たり）+ 共通の衝撃波（跳ぶ）
// → 4 地面に居る（体で殴る）→ 5 元の高さへ。体（GPU）はずっと影の真上を追う（比例の速さ。リードバックの遅れは小さい）
// ============================================================
void BossAttacks::StartSkyCrush(const Input& in, const GridWorld& grid)
{
    m_Beast.crushPhase = 0;
    m_Beast.crushT = 0.0f;
    m_Beast.shadow = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.moveHover = m_Beast.hoverBase;
    m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
}

bool BossAttacks::UpdateSkyCrush(const Input& in, const GridWorld& grid, Output& out)
{
    m_Beast.crushT += in.dt;
    const float t = m_Beast.crushT;
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_ChargeDir;
    // 影がプレイヤーを追う（舞い上がる間・追う間）
    if (m_Beast.crushPhase <= 1)
    {
        const Vector3 p = Ground(grid, in.player.x, in.player.z);
        Vector2 to(p.x - m_Beast.shadow.x, p.z - m_Beast.shadow.z);
        const float d = to.Length();
        const float step = beast.crushFollowSpeed * in.dt;
        if (d > step) to *= step / d;
        m_Beast.shadow = Ground(grid, m_Beast.shadow.x + to.x, m_Beast.shadow.z + to.y);
    }
    // 体は影の真上へ（落ちるまで）
    if (m_Beast.crushPhase <= 3)
    {
        const Vector2 to(m_Beast.shadow.x - in.bossPos.x, m_Beast.shadow.z - in.bossPos.z);
        const float d = to.Length();
        if (d > 0.05f)
        {
            m_ChargeDir = to / d;
            out.chargeDir = m_ChargeDir;
            out.chargeSpeed = (std::min)(d * 3.0f, 35.0f);
        }
    }
    auto next = [&]() { ++m_Beast.crushPhase; m_Beast.crushT = 0.0f; };
    switch (m_Beast.crushPhase)
    {
    case 0:   // 舞い上がる（強く羽ばたく）
        m_Beast.moveHover = m_Beast.hoverBase + (beast.crushHigh - m_Beast.hoverBase) * Smooth(t / (std::max)(beast.crushRise, 0.05f));
        m_Beast.wingWant = (std::sin(m_Beast.flapClock * 2.4f) > 0.0f) ? kWingRaised : kWingBeat;
        m_Beast.wingRate = 12.0f;
        if (t >= beast.crushRise) next();
        return false;
    case 1:   // 影が追う
        m_Beast.moveHover = beast.crushHigh;
        m_Beast.wingWant = 0.2f + 0.55f * std::sin(m_Beast.flapClock);
        m_Beast.wingRate = 8.0f;
        if (t >= beast.crushTrack) next();
        return false;
    case 2:   // 影が止まる（落ちる前の溜め：翼を振り上げる）
        m_Beast.moveHover = beast.crushHigh;
        m_Beast.wingWant = kWingRaised;
        m_Beast.wingRate = 5.0f;
        if (t >= beast.crushLock)
        {
            next();
            out.beast.crushFalling = true;
        }
        return false;
    case 3:   // 落ちる（加速）
    {
        const float k = Clamp01(t / (std::max)(beast.crushFall, 0.05f));
        m_Beast.moveHover = beast.crushHigh * (1.0f - k * k);
        m_Beast.wingWant = kWingTucked;
        m_Beast.wingRate = 10.0f;
        m_Beast.pose = 1.0f;
        if (t < beast.crushFall) return false;
        // 叩き潰し：影の中で地面近くなら当たり + 共通の衝撃波（跳べば避けられる）
        m_Beast.moveHover = 0.0f;
        const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
        if (Flat(m_Beast.shadow, in.player) <= beast.crushRadius + kPlayerThick && feetAbove <= beast.crushReach && feetAbove > -2.0f)
        {
            Hit h;
            h.kind = BossMove::SkyCrush;
            h.center = m_Beast.shadow;
            h.radius = beast.crushRadius;
            h.damage = beast.crushDamage * in.damageMul;
            h.ranged = false;
            h.dir = FlatDir(m_Beast.shadow, in.player, m_ChargeDir);
            out.hits.push_back(h);
            ++beastCrushHits;
        }
        m_WaveOn = true;
        m_WaveHitDone = false;
        m_WaveRadius = 0.0f;
        m_WaveCenter = m_Beast.shadow;
        out.waveLaunched = true;
        out.wavePos = m_Beast.shadow;
        out.beast.crushImpact = true;
        out.beast.crushPos = m_Beast.shadow;
        ++blasts;
        next();
        return false;
    }
    case 4:   // 地面に居る（翼を畳み、頭を上げる）
        m_Beast.moveHover = 0.0f;
        m_Beast.wingWant = kWingFolded;
        m_Beast.wingRate = 4.0f;
        m_Beast.pose = Approach(m_Beast.pose, 0.0f, 2.0f * in.dt);
        if (t >= beast.crushRest) next();
        return false;
    default:  // 元の高さへ
        m_Beast.moveHover = m_Beast.hoverBase * Smooth(t / (std::max)(beast.crushRiseBack, 0.05f));
        m_Beast.wingWant = 0.2f + 0.55f * std::sin(m_Beast.flapClock);
        m_Beast.wingRate = 8.0f;
        return t >= beast.crushRiseBack;
    }
}

// ============================================================
// 石羽の散弾：向きを固めて翼を振り上げる（1 回目の筋の予告が育つ）→ 翼を打ち下ろす度に扇に石の羽根。
// 奇数回目（2 回目）は半分ずらして 1 本少なく = 前の回の隙間を通る。羽根は技の後も UpdateBeastShards で飛ぶ
// ============================================================
void BossAttacks::StartFeatherShards(const Input& in, const GridWorld& grid)
{
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.dir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Beast.shardVolleyDone = 0;
    m_Beast.shardCountNow = (std::max)(1, Enraged(in) ? beast.shardCountEnraged : beast.shardCount);
}

bool BossAttacks::UpdateFeatherShards(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Beast.dir;
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    const float windup = (std::max)(beast.shardWindup, 0.05f);
    const int volleys = (std::max)(1, beast.shardVolleys);
    const float gap = (std::max)(beast.shardVolleyGap, 0.1f);
    const float spacing = DirectX::XMConvertToRadians(beast.shardSpacingDeg);
    while (m_Beast.shardVolleyDone < volleys && m_MoveTime >= windup + gap * (float)m_Beast.shardVolleyDone)
    {
        const int k = m_Beast.shardVolleyDone++;
        const bool offset = (k & 1) != 0;
        const int n = offset ? (std::max)(1, m_Beast.shardCountNow - 1) : m_Beast.shardCountNow;
        const float lift = (std::max)(0.0f, m_Beast.hoverBase) + 1.4f;   // 体の高さから撃つ
        for (int i = 0; i < n; ++i)
        {
            BeastShard s;
            s.dir = Rotate(m_Beast.dir, ((float)i - 0.5f * (float)(n - 1)) * spacing);
            const float x = m_Beast.foot.x + s.dir.x * kShardMuzzle, z = m_Beast.foot.z + s.dir.y * kShardMuzzle;
            s.startLift = lift;
            s.pos = Vector3(x, grid.SampleHeight(x, z) + kShardLift + lift, z);
            s.id = m_Beast.nextShardId++;
            m_Beast.shards.push_back(s);
        }
        out.beast.shardsFired = true;
        ++blasts;
    }
    // 翼：溜め = 振り上げ、撃った直後は打ち下ろし、次の回の前にまた振り上げる
    const float sinceFire = (m_Beast.shardVolleyDone > 0) ? m_MoveTime - (windup + gap * (float)(m_Beast.shardVolleyDone - 1)) : 99.0f;
    m_Beast.wingWant = (sinceFire < 0.18f) ? kWingBeat : (m_Beast.shardVolleyDone < volleys) ? kWingRaised
        : 0.2f + 0.55f * std::sin(m_Beast.flapClock);
    m_Beast.wingRate = (sinceFire < 0.18f) ? 14.0f : 6.0f;
    m_Beast.pose = Approach(m_Beast.pose, 0.3f, in.dt);
    return m_MoveTime >= windup + gap * (float)(volleys - 1) + beast.shardRest;
}

void BossAttacks::UpdateBeastShards(const Input& in, const GridWorld& grid, Output& out)
{
    const float step = beast.shardSpeed * in.dt;
    for (auto it = m_Beast.shards.begin(); it != m_Beast.shards.end();)
    {
        BeastShard& s = *it;
        s.travelled += step;
        s.pos.x += s.dir.x * step;
        s.pos.z += s.dir.y * step;
        const float ground = grid.SampleHeight(s.pos.x, s.pos.z);
        s.pos.y = ground + kShardLift + s.startLift * std::exp(-s.travelled / kShardDrop);
        bool end = s.travelled >= beast.shardRange;
        if (!s.hitDone && Flat(s.pos, in.player) <= beast.shardHitRadius + kPlayerThick)
        {
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (feetAbove <= beast.shardReach && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::FeatherShards;
                h.center = s.pos;
                h.radius = beast.shardHitRadius;
                h.damage = beast.shardDamage * in.damageMul;
                h.ranged = false;
                h.dir = s.dir;
                out.hits.push_back(h);
                ++beastShardHits;
                s.hitDone = true;
                end = true;   // 当たった羽根は砕ける
            }
        }
        if (end)
        {
            out.beast.shardEnds.push_back(s.pos);
            it = m_Beast.shards.erase(it);
        }
        else
            ++it;
    }
}

// ============================================================
// 地面の警告：天からの圧殺の影（追う間は薄く、止まってから落ちるまでに塗りが育つ）、石羽の散弾の次の回の筋
// ============================================================
void BossAttacks::BuildBeastSkyVisuals()
{
    if (m_Type != BossType::StoneBeast) return;
    if (m_Move == BossMove::SkyCrush && m_Beast.crushPhase <= 3)
    {
        const float p = (m_Beast.crushPhase <= 1) ? 0.3f
            : (m_Beast.crushPhase == 2) ? 0.3f + 0.7f * Clamp01(m_Beast.crushT / (std::max)(beast.crushLock, 0.05f)) : 1.0f;
        m_Visuals.push_back({ m_Beast.shadow, beast.crushRadius, p, 0.0f });
    }
    if (m_Move == BossMove::FeatherShards)
    {
        const int volleys = (std::max)(1, beast.shardVolleys);
        const int k = m_Beast.shardVolleyDone;
        if (k < volleys)
        {
            const float windup = (std::max)(beast.shardWindup, 0.05f);
            const float gap = (std::max)(beast.shardVolleyGap, 0.1f);
            const float from = (k == 0) ? 0.0f : windup + gap * (float)(k - 1);
            const float at = windup + gap * (float)k;
            const float p = Clamp01((m_MoveTime - from) / (std::max)(at - from, 0.05f));
            const bool offset = (k & 1) != 0;
            const int n = offset ? (std::max)(1, m_Beast.shardCountNow - 1) : m_Beast.shardCountNow;
            const float spacing = DirectX::XMConvertToRadians(beast.shardSpacingDeg);
            for (int i = 0; i < n; ++i)
            {
                const Vector2 d = Rotate(m_Beast.dir, ((float)i - 0.5f * (float)(n - 1)) * spacing);
                Visual v{ m_Beast.foot, beast.shardRange, p, 0.0f };
                v.yaw = YawOf(d);
                v.shape = -2.0f * beast.shardHitRadius;
                m_Visuals.push_back(v);
            }
        }
    }
}
