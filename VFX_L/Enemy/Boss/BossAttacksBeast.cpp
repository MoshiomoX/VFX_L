// ============================================================
// BossAttacksBeast.cpp
// 遺跡 2 面目の Boss「石の守護獣」の技（2026-10-29）。説明・数値は BossBeast.h。
// 流れ（StartMove → UpdateMove → 間）は BossAttacks.cpp と同じ。ここは技毎の中身と、一段目 / 二段目の入れ替え・飛び・翼・石化。
// 10-29 に追加した 2 つ（天からの圧殺・石羽の散弾）は BossAttacksBeastSky.cpp
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
    float YawOf(const Vector2& d) { return std::atan2(d.x, d.y); }   // +Z が 0（SwarmSystem::WarnCircle と同じ）
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
    // [0, 2π) へ
    float Wrap2Pi(float a)
    {
        a = std::fmod(a, 6.2831853f);
        return (a < 0.0f) ? a + 6.2831853f : a;
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
    constexpr float kWingAuto = -9.0f;     // BeastState::wingWant：技が翼を決めない
    // 翼の角度（rad。0 = 横へ広げる、+ = 先を上へ）
    constexpr float kWingFolded = 1.15f;   // 降りている間：先を上へ立てて背に畳む（石像のガーゴイル）
    constexpr float kWingRaised = 1.3f;    // 突風・急降下の溜め：大きく振り上げる
    constexpr float kWingTucked = 0.95f;   // 滑空中：上へ畳む
    constexpr float kWingBeat = -0.5f;     // 突風の打ち下ろし
}

// ============================================================
// 一段目 / 二段目の入れ替え（Configure・Reset・咆哮）。石化の息は両方、他は段毎
// ============================================================
void BossAttacks::ConfigureBeastPhase(bool phase2)
{
    brain.rules[(int)BossMove::PetrifyBreath].enabled = true;
    brain.rules[(int)BossMove::BeastDive].enabled = !phase2;
    brain.rules[(int)BossMove::WingGust].enabled = !phase2;
    brain.rules[(int)BossMove::TailSweep].enabled = phase2;
    brain.rules[(int)BossMove::SkyCrush].enabled = !phase2;   // 10-29 追加の 2 つ：天からの圧殺は飛んでいる間だけ、石羽の散弾は両方
    brain.rules[(int)BossMove::FeatherShards].enabled = true;
    brain.rules[(int)BossMove::Charge].enabled = phase2;
    brain.rules[(int)BossMove::RockRain].enabled = phase2;
}

void BossAttacks::EnterPhase2(Output& out)
{
    if (m_Type == BossType::Djinn)
    {
        EnterDjinnPhase2(out);
        return;
    }
    m_Beast.phase2 = true;
    ConfigureBeastPhase(true);
    out.beast.landing = true;
}

// ============================================================
// 毎フレーム：浮き（二段目は landTime 秒で降りる。急降下は自分で高さを決める）・飛び・間合い・翼・頭の姿勢・体の回り
// ============================================================
void BossAttacks::UpdateBeastOutputs(const Input& in, Output& out)
{
    if (m_Type != BossType::StoneBeast) return;
    const bool flying = !m_Beast.phase2;
    const float target = flying ? beast.hover : 0.0f;
    const float rate = (std::max)(beast.hover, 0.1f) / (std::max)(beast.landTime, 0.05f);
    m_Beast.hoverBase = Approach(m_Beast.hoverBase, target, rate * in.dt);
    const bool ownHeight = (m_Move == BossMove::BeastDive || m_Move == BossMove::SkyCrush) && m_Beast.moveHover >= 0.0f;
    out.djinn.hover = ownHeight ? m_Beast.moveHover : m_Beast.hoverBase;
    out.djinn.fly = flying;
    out.beast.flying = flying;
    // 間合い：飛んでいる間、技を出していない間だけ（技の間は突進の指示 chargeOn が動きを決める）
    if (in.bossAlive && flying && m_Move == BossMove::None && !IsRoaring())
    {
        out.djinn.keepMin = beast.keepMin;
        out.djinn.keepMax = beast.keepMax;
    }
    // 飛んでいる間は体で殴らない（当たりのカプセルは地面のまま = 下を走り抜けるプレイヤーに当たってしまう）。天からの圧殺で降りている間は殴る
    const bool crushLanded = (m_Move == BossMove::SkyCrush && m_Beast.crushPhase == 4);
    if (flying && in.bossAlive && !crushLanded) out.noContact = true;

    // 頭の姿勢（chargePose → VS の首 = mode 3 が頭を前へ下げる、尾は持ち上がる）：技が書かない間は戻す。突進（二段目）は溜めで下げる
    const bool owned = (m_Move == BossMove::BeastDive || m_Move == BossMove::PetrifyBreath || m_Move == BossMove::WingGust
        || m_Move == BossMove::TailSweep || m_Move == BossMove::SkyCrush || m_Move == BossMove::FeatherShards);
    if (m_Move == BossMove::Charge)
        m_Beast.pose = Clamp01(ChargeT() / (std::max)(m_ChargeWindupNow, 0.05f));
    else if (IsRoaring())
        m_Beast.pose = Approach(m_Beast.pose, 0.35f, 2.0f * in.dt);
    else if (!owned)
        m_Beast.pose = Approach(m_Beast.pose, 0.0f, 2.5f * in.dt);
    out.chargePose = m_Beast.pose;

    // 翼：技が決めた角度へ、決めていなければ 飛ぶ = 羽ばたき / 降りている = 畳む / 吠える = 大きく広げて震わせる
    m_Beast.flapClock += in.dt * beast.flapSpeed;
    if (!owned) m_Beast.wingWant = kWingAuto;
    float want = m_Beast.wingWant;
    float wingRate = m_Beast.wingRate;
    if (want <= kWingAuto + 0.5f)
    {
        if (IsRoaring())
            want = 0.1f + 0.12f * std::sin(m_Beast.flapClock * 4.0f);
        else if (m_Beast.hoverBase > 0.3f)
            want = 0.2f + 0.55f * std::sin(m_Beast.flapClock);
        else
            want = kWingFolded + 0.04f * std::sin(m_Beast.flapClock * 0.3f);
        wingRate = 8.0f;
    }
    m_Beast.wing = Approach(m_Beast.wing, want, wingRate * in.dt);
    out.beast.wing = m_Beast.wing;
    if (m_Move != BossMove::TailSweep) m_Beast.spin = 0.0f;
    out.beast.spin = m_Beast.spin;
}

// ============================================================
// プレイヤーの石化：息に当たる度に溜まり、当たらなければ抜ける。満ちると少しの間は石（ほとんど動けない）→ 0 へ戻る
// ============================================================
void BossAttacks::UpdateBeastPetrify(float dt, Output& out)
{
    if (m_Beast.stoneLeft > 0.0f)
    {
        m_Beast.stoneLeft -= dt;
        if (m_Beast.stoneLeft <= 0.0f)
        {
            m_Beast.stoneLeft = 0.0f;
            m_Beast.petrify = 0.0f;
            out.beast.stoneBroke = true;
        }
    }
    else if (m_Beast.petrify > 0.0f)
        m_Beast.petrify = (std::max)(0.0f, m_Beast.petrify - beast.petrifyDecay * dt);
    out.beast.petrify = m_Beast.petrify;
    out.beast.stoneLeft = m_Beast.stoneLeft;
}

float BossAttacks::BeastPetrifySlow() const
{
    if (m_Beast.stoneLeft > 0.0f) return beast.petrifyLockSlow;
    return 1.0f - beast.petrifySlow * Clamp01(m_Beast.petrify);
}

// ============================================================
// 急降下：空中で止まって翼を振り上げる（向きを固め、帯が育つ）→ 帯に沿って低く滑空して突っ込む（GPU の突進の道。飛んでいるので壁も越える）
// → 元の高さへ戻る。滑空している Boss の近くに居れば当たり（体の殴りは飛んでいる間ずっと無し）
// ============================================================
void BossAttacks::StartBeastDive(const Input& in, const GridWorld& grid)
{
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.dir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Beast.diveLength = std::clamp(Flat(in.bossPos, in.player) + beast.diveOvershoot, beast.diveMinLength, beast.diveMaxLength);
    m_Beast.diveHitDone = false;
    m_Beast.moveHover = -1.0f;
    m_ChargeDir = m_Beast.dir;
}

bool BossAttacks::UpdateBeastDive(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Beast.dir;
    const float windup = (std::max)(beast.diveWindup, 0.05f);
    const float dashTime = m_Beast.diveLength / (std::max)(beast.diveSpeed, 1.0f);
    if (m_MoveTime < windup)
    {
        m_Beast.pose = 0.6f * Clamp01(m_MoveTime / windup);
        m_Beast.wingWant = kWingRaised;
        m_Beast.wingRate = 3.0f;
        m_Beast.moveHover = -1.0f;
        return false;
    }
    const float t = m_MoveTime - windup;
    if (t < dashTime)
    {
        if (m_MoveTime - in.dt < windup) out.beast.diveGo = true;
        out.chargeSpeed = beast.diveSpeed;
        out.beast.diving = true;
        // 最初の 1/4 で低く降り、そのまま地面すれすれを滑る
        const float k = Smooth(t / (dashTime * 0.25f));
        m_Beast.moveHover = m_Beast.hoverBase + (beast.diveLowHover - m_Beast.hoverBase) * k;
        m_Beast.pose = 1.0f;
        m_Beast.wingWant = kWingTucked;
        m_Beast.wingRate = 10.0f;
        if (!m_Beast.diveHitDone && Flat(in.bossPos, in.player) <= beast.diveHitRadius + kPlayerThick)
        {
            const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
            if (feetAbove <= beast.diveReach && feetAbove > -2.0f)
            {
                Hit h;
                h.kind = BossMove::BeastDive;
                h.center = Ground(grid, in.bossPos.x, in.bossPos.z);
                h.radius = beast.diveHitRadius;
                h.damage = beast.diveDamage * in.damageMul;
                h.ranged = false;
                // 帯の横へ弾き出す（真ん中なら進む向き）
                const Vector2 side(-m_Beast.dir.y, m_Beast.dir.x);
                const Vector2 to(in.player.x - in.bossPos.x, in.player.z - in.bossPos.z);
                h.dir = (to.Dot(side) >= 0.0f) ? side : -side;
                h.dir = h.dir * 0.7f + m_Beast.dir * 0.3f;
                h.dir.Normalize();
                out.hits.push_back(h);
                out.beast.diveHit = true;
                m_Beast.diveHitDone = true;
            }
        }
        return false;
    }
    // 元の高さへ戻る（翼は羽ばたきへ）
    const float r = t - dashTime;
    m_Beast.moveHover = beast.diveLowHover + (m_Beast.hoverBase - beast.diveLowHover) * Smooth(r / (std::max)(beast.diveRise, 0.05f));
    m_Beast.pose = Approach(m_Beast.pose, 0.0f, 2.5f * in.dt);
    m_Beast.wingWant = 0.2f + 0.55f * std::sin(m_Beast.flapClock);
    m_Beast.wingRate = 8.0f;
    return r >= beast.diveRise;
}

// ============================================================
// 石化の息：始めた瞬間に向きを固め、溜め（頭を下げる。扇が育つ）→ breathTime 秒吐く。扇は breathTurnDeg 度 / 秒でプレイヤーへ向く。
// breathTick 秒毎に扇の中なら当たり（押さない）+ 石化が溜まる
// ============================================================
void BossAttacks::StartPetrifyBreath(const Input& in, const GridWorld& grid)
{
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.dir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Beast.breathTickLeft = 0.0f;
    m_Beast.breathFired = false;
}

bool BossAttacks::UpdatePetrifyBreath(const Input& in, const GridWorld& grid, Output& out)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Beast.dir;
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.wingWant = (m_Beast.hoverBase > 0.3f) ? 0.2f + 0.3f * std::sin(m_Beast.flapClock * 0.8f) : kWingFolded;
    m_Beast.wingRate = 6.0f;
    if (m_MoveTime < beast.breathWindup)
    {
        m_Beast.pose = Approach(m_Beast.pose, 1.0f, in.dt / 0.4f);
        return false;
    }
    const float t = m_MoveTime - beast.breathWindup;
    if (t >= beast.breathTime)
        return t >= beast.breathTime + beast.breathRest;

    if (!m_Beast.breathFired)
    {
        m_Beast.breathFired = true;
        out.beast.breathStarted = true;
    }
    const float want = YawOf(FlatDir(m_Beast.foot, in.player, m_Beast.dir));
    const float yaw = TurnToward(YawOf(m_Beast.dir), want, DirectX::XMConvertToRadians(beast.breathTurnDeg) * in.dt);
    m_Beast.dir = DirOf(yaw);
    out.chargeDir = m_Beast.dir;
    out.beast.breathOn = true;
    out.beast.breathYaw = yaw;
    m_Beast.pose = 1.0f;

    m_Beast.breathTickLeft -= in.dt;
    if (m_Beast.breathTickLeft <= 0.0f)
    {
        m_Beast.breathTickLeft += (std::max)(beast.breathTick, 0.05f);
        const float half = DirectX::XMConvertToRadians(beast.breathHalfDeg);
        const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
        if (InFan(m_Beast.foot, m_Beast.dir, half, beast.breathRadius, in.player)
            && feetAbove <= beast.breathReach && std::fabs(in.playerFeetY - m_Beast.foot.y) < 4.0f)
        {
            Hit h;
            h.kind = BossMove::PetrifyBreath;
            h.center = m_Beast.foot;
            h.radius = beast.breathRadius;
            h.damage = beast.breathDamage * in.damageMul;
            h.ranged = false;
            h.noKnock = true;
            h.dir = FlatDir(m_Beast.foot, in.player, m_Beast.dir);
            out.hits.push_back(h);
            // 石化が溜まる（石の間はもう溜めない）
            if (m_Beast.stoneLeft <= 0.0f)
            {
                m_Beast.petrify += beast.petrifyPerHit;
                if (m_Beast.petrify >= 1.0f)
                {
                    m_Beast.petrify = 1.0f;
                    m_Beast.stoneLeft = (std::max)(beast.petrifyLockTime, 0.05f);
                    out.beast.petrified = true;
                }
            }
        }
    }
    return false;
}

// ============================================================
// 翼の突風：向きを固めて翼を大きく振り上げる（扇が育つ）→ 打ち下ろす瞬間に扇の中なら高く吹き飛ばす → 少し止まる
// ============================================================
void BossAttacks::StartWingGust(const Input& in, const GridWorld& grid)
{
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.dir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Beast.struck = false;
}

bool BossAttacks::UpdateWingGust(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Beast.dir;
    const float windup = (std::max)(beast.gustWindup, 0.05f);
    if (m_MoveTime < windup)
    {
        m_Beast.wingWant = kWingRaised;
        m_Beast.wingRate = 3.5f;
        m_Beast.pose = Approach(m_Beast.pose, 0.25f, in.dt);
        return false;
    }
    const float t = m_MoveTime - windup;
    if (prevTime < windup)
    {
        out.beast.gustBeat = true;
        out.beast.gustDir = m_Beast.dir;
        const float feetAbove = in.playerFeetY - m_Beast.foot.y;
        if (InFan(m_Beast.foot, m_Beast.dir, DirectX::XMConvertToRadians(beast.gustHalfDeg), beast.gustRadius, in.player)
            && feetAbove < 6.0f && feetAbove > -3.0f)
        {
            Hit h;
            h.kind = BossMove::WingGust;
            h.center = m_Beast.foot;
            h.radius = beast.gustRadius;
            h.damage = beast.gustDamage * in.damageMul;
            h.ranged = false;
            h.dir = FlatDir(m_Beast.foot, in.player, m_Beast.dir);
            out.hits.push_back(h);
        }
        ++blasts;
    }
    const float beat = (std::max)(beast.gustBeatTime, 0.02f);
    m_Beast.wingWant = (t < beat + 0.15f) ? kWingBeat : 0.2f + 0.55f * std::sin(m_Beast.flapClock);
    m_Beast.wingRate = (t < beat + 0.15f) ? (kWingRaised - kWingBeat) / beat : 6.0f;
    m_Beast.pose = Approach(m_Beast.pose, 0.0f, 1.5f * in.dt);
    return t >= beat + beast.gustRest;
}

// ============================================================
// 尾の薙ぎ払い（二段目）：止まって尾を持ち上げる（足元の輪が育つ）→ 体ごと一回転（BomberCB::bossSpin）。
// 尾の先がプレイヤーの向きを通り過ぎる瞬間に、輪の中で足が地面近くなら当たり（跳べば避けられる）
// ============================================================
void BossAttacks::StartTailSweep(const Input& in, const GridWorld& grid)
{
    m_Beast.foot = Ground(grid, in.bossPos.x, in.bossPos.z);
    m_Beast.dir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
    m_Beast.struck = false;
    m_Beast.spin = 0.0f;
}

bool BossAttacks::UpdateTailSweep(const Input& in, const GridWorld& grid, Output& out, float prevTime)
{
    out.chargeOn = true;
    out.chargeSpeed = 0.0f;
    out.chargeDir = m_Beast.dir;
    m_Beast.wingWant = kWingFolded;
    m_Beast.wingRate = 4.0f;
    const float windup = (std::max)(beast.sweepWindup, 0.05f);
    if (m_MoveTime < windup)
    {
        m_Beast.pose = 0.7f * Clamp01(m_MoveTime / windup);
        return false;
    }
    const float spinTime = (std::max)(beast.sweepTime, 0.05f);
    const float t = m_MoveTime - windup;
    if (prevTime < windup) out.beast.sweepStart = true;
    if (t < spinTime)
    {
        const float before = 6.2831853f * Smooth((prevTime - windup) / spinTime);
        m_Beast.spin = 6.2831853f * Smooth(t / spinTime);
        // 尾は体の後ろ（向き + π）。今のフレームで尾が通り過ぎた角度にプレイヤーが居れば
        if (!m_Beast.struck && Flat(m_Beast.foot, in.player) <= beast.sweepRadius + kPlayerThick)
        {
            const float tailBefore = YawOf(m_Beast.dir) + 3.1415927f + (std::max)(before, 0.0f);
            const float step = m_Beast.spin - (std::max)(before, 0.0f);
            const float playerYaw = YawOf(FlatDir(m_Beast.foot, in.player, m_Beast.dir));
            // VS は yaw に spin を足して回す。yaw は +Z = 0 から +X へ増える向き（YawOf と同じ）
            if (Wrap2Pi(playerYaw - tailBefore) <= step + 0.05f)
            {
                const float feetAbove = in.playerFeetY - grid.SampleHeight(in.player.x, in.player.z);
                m_Beast.struck = true;   // 跳んで避けても、通り過ぎたらもう当たらない
                if (feetAbove <= beast.sweepDodgeHeight && feetAbove > -2.0f)
                {
                    Hit h;
                    h.kind = BossMove::TailSweep;
                    h.center = m_Beast.foot;
                    h.radius = beast.sweepRadius;
                    h.damage = beast.sweepDamage * in.damageMul;
                    h.ranged = false;
                    h.dir = FlatDir(m_Beast.foot, in.player, m_Beast.dir);
                    out.hits.push_back(h);
                }
            }
        }
        m_Beast.pose = 0.7f;
        return false;
    }
    m_Beast.spin = 0.0f;
    m_Beast.pose = Approach(m_Beast.pose, 0.0f, 2.0f * in.dt);
    return t >= spinTime + beast.sweepRest;
}

// ============================================================
// 地面の警告：急降下の帯、石化の息・翼の突風の扇、尾の薙ぎ払いの輪
// ============================================================
void BossAttacks::BuildBeastVisuals(const GridWorld& grid)
{
    (void)grid;
    if (m_Type != BossType::StoneBeast) return;
    if (m_Move == BossMove::BeastDive)
    {
        const bool winding = m_MoveTime < beast.diveWindup;
        Visual v{ m_Beast.foot, m_Beast.diveLength, winding ? Clamp01(m_MoveTime / (std::max)(beast.diveWindup, 0.05f)) : -1.0f, 0.0f };
        v.yaw = YawOf(m_Beast.dir);
        v.shape = -beast.diveWidth;
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::PetrifyBreath)
    {
        const bool winding = m_MoveTime < beast.breathWindup;
        Visual v{ m_Beast.foot, beast.breathRadius, winding ? Clamp01(m_MoveTime / (std::max)(beast.breathWindup, 0.05f)) : -1.0f, 0.0f };
        v.yaw = YawOf(m_Beast.dir);
        v.shape = DirectX::XMConvertToRadians(beast.breathHalfDeg);
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::WingGust && m_MoveTime < beast.gustWindup)
    {
        Visual v{ m_Beast.foot, beast.gustRadius, Clamp01(m_MoveTime / (std::max)(beast.gustWindup, 0.05f)), 0.0f };
        v.yaw = YawOf(m_Beast.dir);
        v.shape = DirectX::XMConvertToRadians(beast.gustHalfDeg);
        m_Visuals.push_back(v);
    }
    if (m_Move == BossMove::TailSweep && m_MoveTime < beast.sweepWindup + beast.sweepTime)
    {
        const bool winding = m_MoveTime < beast.sweepWindup;
        m_Visuals.push_back({ m_Beast.foot, beast.sweepRadius, winding ? Clamp01(m_MoveTime / (std::max)(beast.sweepWindup, 0.05f)) : -1.0f, 0.0f });
    }
    BuildBeastSkyVisuals();
}
