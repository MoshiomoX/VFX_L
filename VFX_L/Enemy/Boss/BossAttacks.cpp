// ============================================================
// BossAttacks.cpp
// 技の流れ（選ぶ → 出す → 終わったら間を空ける）と、輪の爆発。説明は BossAttacks.h
// 面毎の組み合わせ（Configure）と技を始める時の状態（StartMove）は BossAttacksConfig.cpp、
// 共通の技（重撃・投石雨・衝撃波・突進・召喚）の中身は BossAttacksCommon.cpp（2026-10-29 に分けた）、
// Boss 毎の技は BossAttacks<Sand / Scorpion / Knight / Wolf / Stag / Djinn / King / Beast>.cpp、調整パネルは BossAttacksPanel.cpp
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
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
    m_ChargeTossT = -1.0f;
    m_ChargeRecover = 0.0f;
    m_ChargeLanesPassed = 0;
    m_ChargeT0 = 0.0f;
    m_ChargeIndex = 0;
    m_ChargeCount = 1;
    m_TossHitDone = m_BoltFired = false;
    m_EnrageRoarDone = false;
    m_RoarLeft = 0.0f;
    m_RoarRingAge = -1.0f;
    m_StingHitDone = false;
    m_ClawHitDone = false;
    m_ChainCaught = false;
    m_PounceAir = false;
    m_ComboIndex = 0;
    m_ComboDashing = m_ComboHitDone = false;
    m_HowlHasteLeft = 0.0f;
    m_SummonPoints.clear();
    m_SpearDirs.clear();
    m_SpearHitDone = false;
    m_BurrowPhase = 0;
    m_BurrowPhaseTime = 0.0f;
    m_BurrowSink = 0.0f;
    m_SandField = QuicksandField{};
    m_SandAge = 0.0f;
    brain.Reset();
    // 砂嵐の魔神：一段目（浮いて）から
    m_Djinn = DjinnState{};
    m_King = KingState{};   // 骸の王の玉・床（2026-10-29）
    m_Djinn.hoverBase = djinn.hover;
    m_ForcePhase2 = false;
    if (m_Type == BossType::Djinn) ConfigureDjinnPhase(false);
    // 石の守護獣：一段目（飛んで）から。石化はプレイヤー側の状態なので引き継ぐ（2026-10-29）
    const float petrify = m_Beast.petrify, stone = m_Beast.stoneLeft;
    m_Beast = BeastState{};
    m_Beast.hoverBase = beast.hover;
    m_Beast.petrify = petrify;
    m_Beast.stoneLeft = stone;
    if (m_Type == BossType::StoneBeast) ConfigureBeastPhase(false);
}

void BossAttacks::Update(const Input& in, const GridWorld& grid, Output& out)
{
    // 大サソリの毒はプレイヤー側の状態：Boss が居なくても減る（2026-10-26）
    if (m_PoisonLeft > 0.0f) m_PoisonLeft = (std::max)(0.0f, m_PoisonLeft - in.dt);
    UpdateBeastPetrify(in.dt, out);   // 石の守護獣の石化も同じ（2026-10-29）
    // Boss が居なくなったら（倒した・まだ呼んでいない）輪も帯も消す
    if (!in.bossAlive)
    {
        if (m_WasAlive || !m_Rings.empty() || m_WaveOn) Reset();
        UpdateDjinnOutputs(in, out);   // 砂嵐の魔神は門から出て来る演出の間も浮いている
        UpdateBeastOutputs(in, out);   // 石の守護獣も飛んで出て来る
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
            h.thunder = it->thunder;
            out.hits.push_back(h);
            ++blasts;
            it = m_Rings.erase(it);
        }
        else
            ++it;
    }

    UpdateWave(in, grid, out);
    UpdateQuicksandField(in.dt);
    if (m_Type == BossType::Djinn) UpdateDevils(in, grid, out);   // 砂嵐の魔神の砂旋（技の後も回る）
    if (m_Type == BossType::BoneKing)   // 骸の王の呪いの玉・呪いの床（技の後も残る）
    {
        UpdateKingOrbs(in, grid, out);
        UpdateKingCurse(in, grid, out);
    }
    if (m_Type == BossType::StoneBeast) UpdateBeastShards(in, grid, out);   // 石の守護獣の石羽（技の後も飛ぶ）
    if (m_Move != BossMove::None) UpdateMove(in, grid, out);
    if (m_Move != BossMove::Burrow) m_BurrowSink = 0.0f;   // 潜砂の途中で技が切れても地上へ戻す
    if (m_Move != BossMove::DjinnShift && m_Move != BossMove::ShadowStep) m_Djinn.shiftSink = 0.0f;
    out.bossBurrow = (std::max)(m_BurrowSink, m_Djinn.shiftSink);   // 魔神の砂渡りも同じ沈み（GPU は撃てなくなる）

    // ---- 四足の Boss の姿勢 → GPU の VS（BomberCB::bossChargePose。2026-10-26）：雄鹿 = 突進（BossAttacksStag.cpp）、
    //      大サソリ = 毒針の尾（BossAttacksScorpion.cpp）、狼人 = 飛びかかりの放物線（UpdatePounce が書く）。他の Boss は 0 のまま ----
    if (m_Type == BossType::Stag) UpdateStagPose(in, out);
    else if (m_Type == BossType::Scorpion) UpdateScorpionPose(out);
    m_LastEnraged = Enraged(in);
    if (m_HowlHasteLeft > 0.0f) m_HowlHasteLeft -= in.dt;

    // ---- 砂嵐の魔神の二段目：パネル・自動テストの「今すぐ」、咆哮を切ってある時は狂暴の線を切った所で ----
    if (HasPhases() && !InPhase2() && m_Move == BossMove::None
        && (m_ForcePhase2 || (!enrageRoar && in.bossHpRatio > 0.01f && in.bossHpRatio < enrageHpRatio)))
        EnterPhase2(out);
    m_ForcePhase2 = false;

    // ---- 狂暴の咆哮（2026-10-29）：HP が半分を切った後、技の切れ目で 1 回だけ止まって吠える。
    //      HP 0 は止めの一撃で折り返した値なので数えない。蘇った騎士（forceEnraged）・テストの狂暴では吠えない ----
    if (enrageRoar && !m_EnrageRoarDone && !forceEnraged && !testEnraged && m_Move == BossMove::None
        && in.bossHpRatio > 0.01f && in.bossHpRatio < enrageHpRatio)
    {
        m_EnrageRoarDone = true;
        m_RoarLeft = (std::max)(enrageRoarTime, 0.05f);
        m_RoarRingAge = 0.0f;
        m_RoarCenter = Ground(grid, in.bossPos.x, in.bossPos.z);
        out.enrageRoared = true;
        if (HasPhases() && !InPhase2()) EnterPhase2(out);   // 砂嵐の魔神・石の守護獣は吠えながら降りて二段目へ
    }
    if (m_RoarRingAge >= 0.0f)
    {
        m_RoarRingAge += in.dt;
        if (m_RoarRingAge > enrageRingTime) m_RoarRingAge = -1.0f;
    }
    if (m_RoarLeft > 0.0f)
    {
        // 吠えている間：その場で止まってプレイヤーの方を向き、体で殴らない。吠え終わったらすぐ次の技
        m_RoarLeft -= in.dt;
        out.chargeOn = true;
        out.chargeSpeed = 0.0f;
        out.chargeDir = FlatDir(in.bossPos, in.player, m_ChargeDir);
        out.noContact = true;
        if (m_RoarLeft <= 0.0f) m_Gap = (std::min)(m_Gap, 0.4f);
    }

    // ---- 次の技（技を出していない間・吠えていない間だけ間を数える）----
    if (m_Move == BossMove::None && m_RoarLeft <= 0.0f)
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

    UpdateDjinnOutputs(in, out);   // 砂嵐の魔神の浮き・間合い・腕の姿勢（他の Boss は全部 0）
    UpdateBeastOutputs(in, out);   // 石の守護獣の浮き・翼・体の回り
    BuildVisuals(in, grid);
}

void BossAttacks::UpdateMove(const Input& in, const GridWorld& grid, Output& out)
{
    const float prevTime = m_MoveTime;
    m_MoveTime += in.dt;
    bool done = false;

    switch (m_Move)
    {
    // 共通の技の中身は BossAttacksCommon.cpp
    case BossMove::Slam:
    case BossMove::RockRain:
        done = UpdateRingVolley(in, grid, out);
        break;
    case BossMove::Shockwave:
        done = UpdateShockwaveMove(in, grid, out);
        break;
    case BossMove::Charge:
        done = UpdateChargeMove(in, grid, out, prevTime);
        break;
    case BossMove::Summon:
        done = UpdateSummonMove(in, out);
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
    case BossMove::Sting:
        done = UpdateSting(in, grid, out, prevTime);
        break;
    case BossMove::ClawSweep:
        done = UpdateClawSweep(in, grid, out, prevTime);
        break;
    case BossMove::ChainPull:
        done = UpdateChainPull(in, out, prevTime);
        break;
    case BossMove::Pounce:
        done = UpdatePounce(in, grid, out, prevTime);
        break;
    case BossMove::ClawCombo:
        done = UpdateClawCombo(in, grid, out, prevTime);
        break;
    case BossMove::Howl:
        done = UpdateHowl(in, out);
        break;
    case BossMove::AntlerToss:
        done = UpdateAntlerToss(in, grid, out, prevTime);
        break;
    case BossMove::AntlerBolt:
        done = UpdateAntlerBolt(in, grid, out, prevTime);
        break;
    case BossMove::DustDevils:
        done = UpdateDustDevilsMove(in, out);
        break;
    case BossMove::DjinnBreath:
        done = UpdateDjinnBreath(in, grid, out);
        break;
    case BossMove::DjinnShift:
        done = UpdateDjinnShift(in, grid, out);
        break;
    case BossMove::SandFist:
        done = UpdateSandFist(in, grid, out, prevTime);
        break;
    case BossMove::CurseOrbs:
        done = UpdateCurseOrbs(in, grid, out);
        break;
    case BossMove::RaiseDead:
        done = UpdateRaiseDead(in, out);
        break;
    case BossMove::ShadowStep:   // 沈んで走って浮かぶのは魔神の砂渡りと同じ（BossAttacksDjinn.cpp）
        done = UpdateDjinnShift(in, grid, out);
        break;
    case BossMove::CurseFloor:
        done = UpdateCurseFloorMove(in, out);
        break;
    case BossMove::BeastDive:
        done = UpdateBeastDive(in, grid, out);
        break;
    case BossMove::PetrifyBreath:
        done = UpdatePetrifyBreath(in, grid, out);
        break;
    case BossMove::WingGust:
        done = UpdateWingGust(in, grid, out, prevTime);
        break;
    case BossMove::TailSweep:
        done = UpdateTailSweep(in, grid, out, prevTime);
        break;
    case BossMove::SkyCrush:
        done = UpdateSkyCrush(in, grid, out);
        break;
    case BossMove::FeatherShards:
        done = UpdateFeatherShards(in, grid, out);
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
    if (m_HowlHasteLeft > 0.0f) m_Gap *= howlGapMul;   // 狼人の遠吠えの後はしばらく間が短い
}

void BossAttacks::BuildVisuals(const Input& in, const GridWorld& grid)
{
    m_Visuals.clear();
    BuildCommonVisuals(in, grid);   // 共通の技の輪・帯・道筋（BossAttacksCommon.cpp）
    if (m_RoarRingAge >= 0.0f)
    {
        // 狂暴の咆哮：足元から広がる赤い輪（縁だけ。最初速く、後で緩む）
        const float u = (std::min)(1.0f, m_RoarRingAge / (std::max)(enrageRingTime, 0.05f));
        const float r = enrageRingMax * (1.0f - (1.0f - u) * (1.0f - u));
        m_Visuals.push_back({ m_RoarCenter, (std::max)(r, 0.3f), -1.0f, 1.2f });
    }
    BuildSandVisuals(grid);
    BuildScorpionVisuals(in, grid);
    BuildKnightVisuals(grid);
    BuildWolfVisuals(in, grid);
    BuildStagVisuals(grid);
    BuildDjinnVisuals(grid);
    BuildKingVisuals(grid);
    BuildBeastVisuals(grid);
}
