// ============================================================
// BossKing.h
// 遺跡 3 面目の Boss「骸の王」の数値と状態（2026-10-29、ユーザー「死霊の司教を 3 面目に、見た目はダークソウル 3 のウォルニール風に」→
// 名前は「骸の王」、技は方案表の死霊の司教のまま（間合いを取る術師）、手首の金の腕輪は見た目だけ）。中身は BossAttacksKing.cpp。
//   浮き・飛び・間合いの出力と「影へ消える」の沈み / 走り / 浮かびは砂嵐の魔神と同じ仕組み（BossAttacksDjinn.cpp。DjinnOutput を共用）
//   ・呪いの玉：両腕を上げて溜め → 暗い紫の玉を扇に放つ。玉はゆっくり（走りより遅く）プレイヤーを追い、触れると爆ぜる
//   ・大起尸：プレイヤーの周りの最近の死体（MobSpawner が覚えている倒した所）が一斉に骸骨で起き上がる。足りなければ周りから
//   ・影へ消える：闇に沈み、プレイヤーの周りの別の側（今の向きから ±stepTurnDeg）へ回り込んで浮かび上がる → すぐ呪いの玉
//     （10-29 ユーザー「動かなくていい」で既定は出さない。真ん中に湧いてその場から攻撃し続ける）
//   ・呪いの床：プレイヤーの足元に紫の渦。中では遅く・中心へ引かれ、地面に居る間 curseTick 秒毎に少し減る（跳べば効かない）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

// 調整値（Enemies パネルの「Bone King」。BossAttacks::king）
struct KingTune
{
    // ---- 浮き・間合い ----
    float hover = 0.0f;            // 闇の水溜まりに浸かって這う（浮かない）
    // 間合い（AICS）：0 = 取らない。10-29 ユーザー「動かなくていい、真ん中に湧いてずっと攻撃すればいい」→ 骸の王は真ん中から動かない
    // （StageDirector::bossAtCenter・速さ 0）。> 0 にすると術師のように下がる / 止まる
    float keepMin = 0.0f;
    float keepMax = 0.0f;

    // ---- 呪いの玉（CurseOrbs）----
    float orbWindup = 1.0f;        // 両腕を上げる溜め
    int   orbCount = 5;
    int   orbCountEnraged = 7;
    float orbFanDeg = 26.0f;       // 隣の玉との向きの開き（度）
    float orbSpeed = 4.5f;         // m/s（走り 5.5 より遅い = 走れば振り切れる）
    float orbTurnDeg = 100.0f;     // プレイヤーへ向きを変える速さ（度 / 秒）
    float orbLife = 7.0f;
    float orbHeight = 1.4f;        // 地面からの高さ（放った所は Boss の前 orbSpawnFront m・高さ +2.5m から降りてくる）
    float orbSpawnFront = 5.0f;    // 体が 5.5 倍で腕が 7m 先まで伸びるので、玉は腕の間から出す
    float orbHitRadius = 0.9f;     // + プレイヤーの太さ
    float orbDamage = 22.0f;       // 爆ぜた時（基準 × damageMul）
    float orbRest = 0.5f;

    // ---- 大起尸（RaiseDead）----
    float raiseWindup = 1.4f;      // 両腕を上げて吠える間（その間に地面の印が出て骸骨が起きる）
    float raiseRadius = 22.0f;     // プレイヤーからこの距離の死体を起こす
    int   raiseMax = 10;
    int   raiseMin = 5;            // 死体が足りなければ周りの地面から（raiseFillMin〜Max m）
    float raiseFillMin = 4.0f;
    float raiseFillMax = 8.0f;

    // ---- 影へ消える（ShadowStep）----
    float stepSink = 0.5f;
    float stepSpeed = 26.0f;
    float stepMaxTime = 1.3f;
    float stepDist = 11.0f;        // 浮かび上がる所 = プレイヤーからこの距離
    float stepTurnDeg = 110.0f;    // 今の向き（プレイヤーから見た Boss の方向）から回り込む角度
    float stepRise = 0.4f;

    // ---- 呪いの床（CurseFloor）----
    float curseWindup = 0.8f;      // 両腕を上げる。渦はこの間に広がる
    float curseRadius = 7.0f;
    float curseDuration = 6.0f;    // 広がってから残る秒（技が終わっても）
    float curseFade = 0.8f;
    float curseSlow = 0.65f;       // 中で地面に居る間の走りの速さの倍率（広がる間 curseWindup は効かない = 予告の間に出る）
    float cursePullEdge = 0.7f;    // 中心へ引く速さ m/s（縁 / 内側）。10-29 の最初の 1.0 / 2.2 + 遅さ 0.5 では外へ走っても抜けられなかった
    float cursePullCenter = 1.5f;
    float curseTick = 0.5f;        // この秒毎に
    float curseTickDamage = 6.0f;  // 地面に居れば減る（跳んでいれば当たらない、ノックバック無し）
    float curseReach = 0.5f;       // 足が地面からこれより上なら効かない
};

// 呪いの玉 1 つ（技の後も飛ぶ。BossAttacks::KingOrbs()）
struct KingOrb
{
    DirectX::SimpleMath::Vector3 pos;
    DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };
    float life = 7.0f;
    uint32_t id = 0;               // シーンが見た目を対応させる番号
};

// 呪いの床（技の後も残る）
struct KingCurse
{
    bool  active = false;
    DirectX::SimpleMath::Vector3 center;
    float radius = 0.0f;           // 今の半径（広がる途中は小さい）
    float strength = 0.0f;         // 0〜1
    float age = 0.0f;
    float tickLeft = 0.0f;
};

// シーンへの出力（BossAttacks::Output::king）
struct KingOutput
{
    bool orbsLaunched = false;     // 玉を放った瞬間（音）
    struct Blast { DirectX::SimpleMath::Vector3 pos; bool touched = false; };
    std::vector<Blast> orbBlasts;  // 玉が爆ぜた所（触れた / 寿命）
    // 大起尸：この瞬間に起こす（シーンが MobSpawner::RaiseCorpsesAround へ）
    bool raise = false;
    DirectX::SimpleMath::Vector3 raiseCenter;
    bool curseStarted = false;     // 呪いの床が出た瞬間（見た目・音）
    DirectX::SimpleMath::Vector3 cursePos;
    bool casting = false;          // 溜め（両腕を上げている）の始まり（詠唱の光）
};

// 技の途中の状態（BossAttacks の中だけ）
struct KingState
{
    std::vector<KingOrb> orbs;
    uint32_t nextOrbId = 1;
    bool orbsFired = false;
    KingCurse curse;
};
