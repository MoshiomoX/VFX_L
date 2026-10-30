// ============================================================
// BossAttacksConfig.cpp
// 面毎の Boss の組み合わせ（Configure：技の有り / 無し・召喚する敵・体の色・前傾・体の大きさ）と召喚の種類の抽選。
// 2026-10-29 に BossAttacks.cpp（砂嵐の魔神を足して 530 行）から分けた。同日、石の守護獣を足して BossAttacks.cpp が 512 行に
// なったので、技を始める時の状態（StartMove）もここへ移した。説明は BossAttacks.h
// ============================================================
#include "Enemy/Boss/BossAttacks.h"
#include "Swarm/SwarmTypes.h"
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
}

// ============================================================
// 面毎の Boss。技の有り / 無しは BossBrain の rules[].enabled、他の数値（冷却・重み）は BossBrain の既定のまま
// ============================================================
void BossAttacks::Configure(BossType t)
{
    m_Type = t;
    const bool sand = (t == BossType::SandGuardian);
    const bool scorpion = (t == BossType::Scorpion);   // 砂漠 2 面目（2026-10-26）：毒針 / 鋏 + 潜砂（借用）+ 召喚。共通の 4 つは出さない
    const bool knight = (t == BossType::CryptKnight);  // 遺跡 1 面目（2026-10-26）：重撃 / 衝撃波 / 双鎖 / 鎧の召喚（投石雨・突進は出さない）
    const bool wolf = (t == BossType::Werewolf);       // 草原 2 面目（2026-10-26）：飛びかかり / 爪の三連 / 遠吠え だけ（共通は全部出さない）
    const bool stag = (t == BossType::Stag);           // 草原 3 面目（2026-10-29）：突進 / 角の振り上げ / 角の雷撃 だけ
    const bool isDjinn = (t == BossType::Djinn);         // 砂漠 3 面目（2026-10-29）：一段目 / 二段目で入れ替える（ConfigureDjinnPhase）
    const bool isKing = (t == BossType::BoneKing);       // 遺跡 3 面目（2026-10-29）：呪いの玉 / 大起尸 / 影へ消える / 呪いの床 だけ
    const bool isBeast = (t == BossType::StoneBeast);    // 遺跡 2 面目（2026-10-29）：一段目 / 二段目で入れ替える（ConfigureBeastPhase）
    const bool own = isDjinn || isKing || isBeast;       // 共通の技を全部切る Boss（魔神・守護獣は二段目で一部を点ける）
    brain.rules[(int)BossMove::Slam].enabled = !scorpion && !wolf && !stag && !own;
    brain.rules[(int)BossMove::RockRain].enabled = !scorpion && !knight && !wolf && !stag && !own;
    brain.rules[(int)BossMove::Shockwave].enabled = !scorpion && !wolf && !stag && !isKing && !isBeast;
    brain.rules[(int)BossMove::Charge].enabled = !scorpion && !knight && !wolf && !own;
    brain.rules[(int)BossMove::Summon].enabled = !wolf && !stag && !own;
    brain.rules[(int)BossMove::AntlerToss].enabled = stag;
    brain.rules[(int)BossMove::AntlerBolt].enabled = stag;
    brain.rules[(int)BossMove::Pounce].enabled = wolf;
    brain.rules[(int)BossMove::ClawCombo].enabled = wolf;
    brain.rules[(int)BossMove::Howl].enabled = wolf;
    brain.rules[(int)BossMove::SandSpear].enabled = sand;
    brain.rules[(int)BossMove::Burrow].enabled = sand || scorpion;
    brain.rules[(int)BossMove::Quicksand].enabled = sand;
    brain.rules[(int)BossMove::Sting].enabled = scorpion;
    brain.rules[(int)BossMove::ClawSweep].enabled = scorpion;
    brain.rules[(int)BossMove::ChainPull].enabled = knight;
    brain.rules[(int)BossMove::CurseOrbs].enabled = isKing;
    brain.rules[(int)BossMove::RaiseDead].enabled = isKing;
    brain.rules[(int)BossMove::ShadowStep].enabled = false;   // 骸の王は動かない（10-29 ユーザー）。技の中身は残してある（パネルで点けられる）
    brain.rules[(int)BossMove::CurseFloor].enabled = isKing;
    forceEnraged = false;
    // 技を出す距離：骸の王は真ん中から動かないので、プレイヤーがどこに居ても出す（他は 35m）
    range = isKing ? 1.0e4f : 35.0f;
    if (isDjinn) ConfigureDjinnPhase(false);   // 衝撃波・流沙・魔神の技（一段目 / 二段目）
    else
    {
        brain.rules[(int)BossMove::DustDevils].enabled = false;
        brain.rules[(int)BossMove::DjinnBreath].enabled = false;
        brain.rules[(int)BossMove::DjinnShift].enabled = false;
        brain.rules[(int)BossMove::SandFist].enabled = false;
    }
    if (isBeast) ConfigureBeastPhase(false);   // 石の守護獣の技・二段目の突進 / 投石雨
    else
    {
        brain.rules[(int)BossMove::BeastDive].enabled = false;
        brain.rules[(int)BossMove::PetrifyBreath].enabled = false;
        brain.rules[(int)BossMove::WingGust].enabled = false;
        brain.rules[(int)BossMove::TailSweep].enabled = false;
        brain.rules[(int)BossMove::SkyCrush].enabled = false;
        brain.rules[(int)BossMove::FeatherShards].enabled = false;
    }
    // 走りの前傾：雄鹿は臀を支点に 10°、大サソリは低い胴の真ん中を支点に 5°（頭が砂に刺さらない）、他は傾けない
    if (t == BossType::Stag) { leanPivotY = 0.5f; leanPivotZ = -0.6f; leanAngle = 0.17f; }
    else if (scorpion) { leanPivotY = -0.6f; leanPivotZ = 0.0f; leanAngle = 0.08f; }
    else if (isBeast) { leanPivotY = 0.3f; leanPivotZ = -0.55f; leanAngle = 0.12f; }   // 石の守護獣の突進（二段目）
    else { leanPivotY = leanPivotZ = leanAngle = 0.0f; }
    // 体の大きさ：狼人は猫背で背が低くなったので大きく（10-27「圧が無い、小さい」）
    // 骸の王は真ん中から動かない巨大な骸骨（2026-10-29 ユーザー「もっと大きく、プレイヤーから見て怪物だと感じるくらい」→ 3.5 → 5.5）
    // 石の守護獣は翼を広げて飛ぶ竜なので少し大きく（3.4）
    bodyScale = (t == BossType::Werewolf) ? 3.8f : isKing ? 5.5f : isBeast ? 3.4f : 3.0f;
    if (isKing)
    {
        // 骸の王：召喚の技は無い（大起尸は MobSpawner の骸骨）。体は飾りだけなので色は掛けない
        summonPicks[0] = { Swarm::kEnemyKindSkeleton, 1.0f };
        summonPicks[1] = { Swarm::kEnemyKindMob, 0.0f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else if (isBeast)
    {
        // 石の守護獣：召喚の技は無いが、念のため遺跡の石像ガーゴイル。体は飾りだけなので色は掛けない
        summonPicks[0] = { Swarm::kEnemyKindGargoyle, 1.0f };
        summonPicks[1] = { Swarm::kEnemyKindMob, 0.0f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else if (isDjinn)
    {
        // 砂嵐の魔神：召喚の技は無い（砂旋は技の見た目）が、念のため砂漠の敵。体は飾りだけなので色は掛けない
        summonPicks[0] = { Swarm::kEnemyKindSpikeBall, 0.5f };
        summonPicks[1] = { Swarm::kEnemyKindBurrower, 0.5f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else if (sand || scorpion)
    {
        // 砂の守護者：トゲ玉と砂潜り（砂漠の敵）を呼ぶ。体は自分の砂岩の皮（BossLooks）なので色は掛けない
        summonPicks[0] = { Swarm::kEnemyKindSpikeBall, 0.5f };
        summonPicks[1] = { Swarm::kEnemyKindBurrower, 0.5f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else if (knight)
    {
        // 墓守の騎士：呪いの鎧を呼ぶ（倒しても 1 回蘇る雑魚）。皮は鎧の鋼（BossLooks）
        summonPicks[0] = { Swarm::kEnemyKindArmor, 1.0f };
        summonPicks[1] = { Swarm::kEnemyKindMob, 0.0f };
        summonPicks[2] = { Swarm::kEnemyKindMob, 0.0f };
        bodyTint = { 1.0f, 1.0f, 1.0f };
    }
    else if (wolf)
    {
        // 月下の狼人：遠吠えで雑魚（群れ）を呼ぶ。体は飾りだけなので色は掛けない（狂暴の赤はシーンが bodyTint に入れる）
        summonPicks[0] = { Swarm::kEnemyKindMob, 0.8f };
        summonPicks[1] = { Swarm::kEnemyKindCharger, 0.2f };
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

// ============================================================
// 技を始める：技毎の始めの状態（向き・置く輪・湧く所）。2026-10-29 に BossAttacks.cpp（石の守護獣を足して 512 行）から移した
// ============================================================
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
        m_ChargeTossT = -1.0f;
        m_ChargeRecover = 0.0f;
        m_ChargeLanesPassed = 0;
        m_ChargeT0 = 0.0f;
        m_ChargeWindupNow = chargeWindup;
        m_ChargeIndex = 0;
        m_ChargeCount = (m_Type == BossType::Stag && Enraged(in)) ? (std::max)(1, chargeCountEnraged) : 1;
        break;
    case BossMove::AntlerToss:
        StartAntlerToss(in, grid);
        break;
    case BossMove::AntlerBolt:
        StartAntlerBolt(in, grid);
        out.boltCharging = true;
        out.ringsPlaced += m_BoltCountNow;
        break;
    case BossMove::Howl:   // 狼人の遠吠え：湧く所は召喚と同じ（吠え終わりに湧く）
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
    case BossMove::Sting:
        StartSting(in, grid);
        ++out.ringsPlaced;
        break;
    case BossMove::ClawSweep:
        StartClawSweep(in);
        break;
    case BossMove::ChainPull:
        StartChainPull(in, grid);
        out.ringsPlaced += 2;
        break;
    case BossMove::Pounce:
        StartPounce(in);
        ++out.ringsPlaced;
        break;
    case BossMove::ClawCombo:
        StartClawCombo(in, grid);
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
    // 砂嵐の魔神（BossAttacksDjinn.cpp）
    case BossMove::DustDevils:
        StartDustDevils(in, grid, out);
        break;
    case BossMove::DjinnBreath:
        StartDjinnBreath(in, grid);
        ++out.ringsPlaced;
        break;
    case BossMove::DjinnShift:
        StartDjinnShift(in, grid);
        out.djinn.shiftSank = true;
        break;
    case BossMove::SandFist:
        StartSandFist(in, grid);
        ++out.ringsPlaced;
        break;
    // 骸の王（BossAttacksKing.cpp）
    case BossMove::CurseOrbs:
        StartCurseOrbs(in);
        out.king.casting = true;
        break;
    case BossMove::RaiseDead:
        out.king.casting = true;
        out.king.raise = true;
        out.king.raiseCenter = Vector3(in.player.x, grid.SampleHeight(in.player.x, in.player.z), in.player.z);
        m_ChargeDir = FlatDir(in.bossPos, in.player, Vector2(0.0f, 1.0f));
        break;
    case BossMove::ShadowStep:
        StartShadowStep(in, grid);
        out.djinn.shiftSank = true;
        break;
    case BossMove::CurseFloor:
        StartCurseFloor(in, grid);
        out.king.casting = true;
        out.king.curseStarted = true;
        out.king.cursePos = m_King.curse.center;
        ++out.ringsPlaced;
        break;
    // 石の守護獣（BossAttacksBeast.cpp）
    case BossMove::BeastDive:
        StartBeastDive(in, grid);
        ++out.ringsPlaced;
        break;
    case BossMove::PetrifyBreath:
        StartPetrifyBreath(in, grid);
        ++out.ringsPlaced;
        break;
    case BossMove::WingGust:
        StartWingGust(in, grid);
        break;
    case BossMove::TailSweep:
        StartTailSweep(in, grid);
        ++out.ringsPlaced;
        break;
    case BossMove::SkyCrush:
        StartSkyCrush(in, grid);
        out.beast.crushRose = true;
        break;
    case BossMove::FeatherShards:
        StartFeatherShards(in, grid);
        ++out.ringsPlaced;
        break;
    default:
        break;
    }
}
