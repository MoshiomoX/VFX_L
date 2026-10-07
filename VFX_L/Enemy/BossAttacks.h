// ============================================================
// BossAttacks.h
// 面の Boss の技。Boss の体は GPU の雑魚と同じなので、技の拍子と当たりは CPU が決める
// （GPU のリードバックの Boss の位置・HP を使う。2〜3 フレーム遅れる）。
//   2026-10-03：重撃（プレイヤーの足元に赤い輪）だけ
//   2026-10-07：投石雨・衝撃波・突進・召喚を足し、どれを出すかは BossBrain（距離 + 技ごとの冷却）が決める。
//     技が終わってから gap 秒（狂暴 gapEnraged）空けて次。狂暴の間は comboChance で間を空けずに次を出す
//   ・重撃 / 投石雨：輪が warnTime で縁まで育って爆発（Hit、ranged = true：中に居るかはシーンが高さ込みで見る）
//   ・衝撃波：溜め（Boss が止まり、足元に輪が育つ）→ 地面を這う帯が waveSpeed で外へ。帯が通る瞬間に
//     足が地面近くなら当たり（跳べば避けられる）
//   ・突進：Boss → プレイヤーの向きを決めて道筋に輪を並べ、溜め → chargeSpeed で一直線。Boss の近くに
//     プレイヤーが居れば当たり。走らせるのは GPU（Output::chargeOn → BomberCB の bossCharge*）
//   ・召喚：Boss が止まって吠え、周りに小さな輪 → summonWindup 秒後に輪から雑魚・自爆兵が湧く
//     （Output::summons。湧かせるのはシーン。同時上限は見ない＝分裂体と同じ）
// 見た目・音・被弾（無敵時間・ノックバック）はシーン（CollisionTestSceneBoss.cpp）
// ============================================================
#pragma once
#include "Enemy/BossBrain.h"
#include <SimpleMath.h>
#include <cstdint>
#include <random>
#include <vector>

class GridWorld;

class BossAttacks
{
public:
    // 爆発・当たり（シーンが被弾・見た目・音を出す）
    struct Hit
    {
        BossMove kind = BossMove::Slam;
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ（輪の中心・衝撃波の中心・突進中の Boss）
        float radius = 3.0f;
        float damage = 0.0f;
        bool  ranged = true;                   // true = 輪の爆発（中に居るかはシーンが見る）、false = もう当たった（衝撃波・突進）
        DirectX::SimpleMath::Vector2 dir = { 0.0f, 1.0f };   // ranged = false の時の吹き飛ばす向き
    };
    // 地面の警告の見た目（シーンが SwarmSystem::WarnCircle へ写す）
    struct Visual
    {
        DirectX::SimpleMath::Vector3 center;
        float radius = 1.0f;
        float progress = 0.0f;   // 負 = 縁だけ
        float band = 0.0f;       // > 0 = 幅 band の帯（衝撃波）
    };
    struct Input
    {
        float dt = 0.0f;
        bool  bossAlive = false;
        float bossHpRatio = 1.0f;
        DirectX::SimpleMath::Vector3 bossPos;
        DirectX::SimpleMath::Vector3 player;   // カプセルの中心
        float playerFeetY = 0.0f;
        float damageMul = 1.0f;                // MobSpawner の今のダメージ倍率
    };
    struct Output
    {
        std::vector<Hit> hits;
        std::vector<DirectX::SimpleMath::Vector3> rockFalls;   // 投石雨：岩が落ち始める所（落ちる見た目。rockFallTime 秒後に爆発）
        int  ringsPlaced = 0;                       // 置いた輪の数（警告の音）
        BossMove started = BossMove::None;          // この瞬間に始まった技（咆哮・溜めの見た目と音）
        bool waveLaunched = false;                  // 衝撃波が出た瞬間（地面を叩く見た目と音）
        DirectX::SimpleMath::Vector3 wavePos;
        bool chargeGo = false;                      // 突進が走り出した瞬間
        // GPU の Boss への指示（BomberCB の bossCharge*）。chargeOn の間、Boss は (dir × speed) で真っ直ぐ動く
        bool  chargeOn = false;
        DirectX::SimpleMath::Vector2 chargeDir = { 0.0f, 1.0f };
        float chargeSpeed = 0.0f;
        // 召喚：この瞬間に湧かせる所（地面の高さ）と自爆兵かどうか
        struct Summon { DirectX::SimpleMath::Vector3 pos; bool bomber = false; };
        std::vector<Summon> summons;
    };

    // ---- 共通（Enemies パネルの「Boss Attacks」）----
    bool  enabled = true;
    float firstDelay = 3.0f;        // 現れてから最初の技まで（秒）
    float gap = 2.5f;               // 技が終わってから次の技まで（秒）
    float gapEnraged = 1.5f;        // HP が enrageHpRatio を切った後
    float enrageHpRatio = 0.5f;
    float comboChance = 0.3f;       // 狂暴の間、技の直後に間を空けず次を出す確率
    float range = 35.0f;            // Boss とプレイヤーがこれより離れていたら技を出さない（洞の奥から外を叩かない）

    // ---- 重撃（Slam）----
    int   count = 3;                // 輪の数
    float spacing = 0.45f;          // 輪を置く間隔（秒）
    float warnTime = 1.2f;          // 輪が出てから爆発まで（秒）
    float radius = 3.0f;            // m
    float damage = 25.0f;           // 難度の倍率 1 の時。当たると × MobSpawner の今のダメージ倍率

    // ---- 投石雨（RockRain）----
    int   rockCount = 9;            // 岩の数（1 つ目はプレイヤーの足元、残りは周りのばらばらな所）
    float rockSpacing = 0.12f;      // 輪を置く間隔（秒）
    float rockWarn = 1.1f;          // 輪が出てから落ちるまで（秒）
    float rockRadius = 2.0f;
    float rockSpread = 7.0f;        // プレイヤーの周りのこの半径の中に散らす（m）
    float rockDamage = 18.0f;
    float rockFallTime = 0.45f;     // 爆発のこれだけ前に岩が空から落ち始める（BossRockFall.json の落ちる時間と合わせる）

    // ---- 衝撃波（Shockwave）----
    float waveWindup = 0.9f;        // 溜め（Boss が止まって足元の輪が育つ）
    float waveWindupRadius = 4.0f;  // 溜めの輪の半径
    float waveSpeed = 11.0f;        // 帯が広がる速さ m/s
    float waveMaxRadius = 28.0f;
    float waveBand = 1.4f;          // 帯の幅 m
    float waveDodgeHeight = 0.6f;   // 足が地面からこれより上なら当たらない（跳べば避けられる）
    float waveDamage = 22.0f;

    // ---- 突進（Charge）----
    float chargeWindup = 1.1f;      // 溜め（Boss が止まって道筋の輪が育つ）
    float chargeSpeed = 20.0f;      // m/s
    float chargeLength = 24.0f;     // 走る距離 m（壁に当たれば GPU が止める）
    float chargeLaneRadius = 1.4f;  // 道筋の輪の半径
    float chargeLaneSpacing = 2.4f; // 道筋の輪の間隔 m
    float chargeHitRadius = 2.6f;   // Boss の中心からこの距離にプレイヤーが居れば当たり
    float chargeDamage = 35.0f;

    // ---- 召喚（Summon）----
    float summonWindup = 1.4f;       // 吠えてから湧くまで（Boss は止まる）
    int   summonCount = 6;           // 湧く数
    int   summonCountEnraged = 9;    // HP が enrageHpRatio を切った後
    float summonBomberChance = 0.3f; // 1 体ごとに自爆兵になる確率
    float summonRingMin = 3.5f;      // Boss の周りのこの距離の間に置く（m）
    float summonRingMax = 7.0f;
    float summonRingRadius = 1.1f;   // 湧く所の輪の半径

    BossBrain brain;

    void Reset();
    void Update(const Input& in, const GridWorld& grid, Output& out);

    const std::vector<Visual>& Visuals() const { return m_Visuals; }
    BossMove Current() const { return m_Move; }

    // パネルのボタン・自動テスト：次の Update で（距離・間隔・冷却を見ずに）この技を出す
    void QueueMove(BossMove m) { m_DebugMove = m; }
    void QueueVolley() { QueueMove(BossMove::Slam); }
    // 自動テストの記録用（累計）
    uint32_t volleys = 0, ringsPlaced = 0, blasts = 0, summoned = 0;
    uint32_t moveCounts[(int)BossMove::Count] = {};

    void DrawImGui();

private:
    struct Ring
    {
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ
        float radius = 3.0f;
        float age = 0.0f;
        float warn = 1.0f;
        float damage = 0.0f;
        BossMove kind = BossMove::Slam;
    };

    void StartMove(BossMove m, const Input& in, const GridWorld& grid, Output& out);
    void UpdateMove(const Input& in, const GridWorld& grid, Output& out);
    void UpdateWave(const Input& in, const GridWorld& grid, Output& out);
    void BuildVisuals(const Input& in, const GridWorld& grid);
    bool Enraged(const Input& in) const { return in.bossHpRatio < enrageHpRatio; }

    std::vector<Ring>   m_Rings;
    std::vector<Visual> m_Visuals;
    std::mt19937 m_Rng{ 20261007u };

    BossMove m_Move = BossMove::None;   // 今出している技
    float    m_MoveTime = 0.0f;         // その技を始めてから
    float    m_Gap = 0.0f;              // 次の技まで（技を出していない間だけ減る）
    bool     m_WasAlive = false;
    BossMove m_DebugMove = BossMove::None;

    // 重撃・投石雨：まだ置いていない輪
    int   m_Pending = 0;
    float m_PendingTimer = 0.0f;
    DirectX::SimpleMath::Vector3 m_RainCenter;   // 投石雨を始めた時のプレイヤーの足元

    // 衝撃波（技が終わった後も帯は広がり続ける）
    bool  m_WaveOn = false;
    bool  m_WaveHitDone = false;
    float m_WaveRadius = 0.0f;
    DirectX::SimpleMath::Vector3 m_WaveCenter;

    // 突進
    DirectX::SimpleMath::Vector3 m_ChargeStart;   // 溜めを始めた時の Boss（地面の高さ）
    DirectX::SimpleMath::Vector2 m_ChargeDir = { 0.0f, 1.0f };
    bool  m_ChargeHitDone = false;

    // 召喚：湧く所（StartMove で決める）
    std::vector<Output::Summon> m_SummonPoints;
};
