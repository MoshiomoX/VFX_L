// ============================================================
// SwarmTypes.h
// Shader/Common/SwarmCommon.hlsli と一対一で対応する定義。
//
// StructuredBuffer には反射が無い。ズレても警告は出ず、
// ただ結果が壊れるだけなので、必ず両方を同時に直すこと。
// static_assert でサイズだけは機械的に守る。
//
// ※state（生死）は本体の構造体に入れず、別の RWBuffer<uint> に置く。
//   SM5.0 の原子操作は RWByteAddressBuffer と
//   RWBuffer<uint> / RWStructuredBuffer<uint> にしか使えない。
//   スロットの取り合いに InterlockedCompareExchange が要るので、
//   生死だけは独立した uint 配列にする必要がある。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

namespace Swarm
{
    using DirectX::SimpleMath::Vector3;

    // 固定ステップ（Unity の FixedUpdate 既定値に合わせた）
    constexpr float kFixedStep = 0.02f;
    constexpr int   kMaxSubSteps = 4;   // コマ落ち時の死のスパイラル防止

    // プール容量。溢れたら生成しない（粒子の deadCount 護欄と同じ思想）
    constexpr uint32_t kMaxEnemies = 4096;
    constexpr uint32_t kMaxProjectiles = 8192;
    constexpr uint32_t kMaxOrbs = 4096;

    // 1フレームに受け付ける生成依頼の上限
    constexpr uint32_t kMaxSpawnEnemyPerFrame = 256;
    constexpr uint32_t kMaxSpawnProjPerFrame = 512;

    // 範囲攻撃。HLSL の SWARM_MAX_AREAS と一致させること
    constexpr uint32_t kMaxAreas = 256;
    constexpr uint32_t kMaxAreaDefs = 64;          // 0 番 = 無し
    constexpr uint32_t kMaxSpawnAreaPerFrame = 64;

    constexpr float kHpScale = 100.0f;
    inline uint32_t HpToFixed(float hp) { return (uint32_t)(hp * kHpScale + 0.5f); }
    inline float    HpFromFixed(uint32_t h) { return (float)h / kHpScale; }

    constexpr uint32_t kStateDead = 0;
    constexpr uint32_t kStateAlive = 1;

    // ============================================================
    // 本体データ（生死は別バッファ）
    // ============================================================
    struct Enemy
    {
        Vector3  position;
        uint32_t hp = 0;
        Vector3  velocity;
        float    moveSpeed = 3.5f;
        float    yaw = 0.0f;

        // ---- 将来のアニメーション用（今は誰も読まない）----
        // VAT（頂点アニメーションテクスチャ）方式を想定。
        // CS が animTime を進め、VS がテクスチャから引く形なら、
        // 骨の計算そのものが実行時に存在しなくなる
        float    animTime = 0.0f;
        uint32_t animIndex = 0;   // 0=待機 1=歩行 2=被弾（HitCS が書く。animTime が経過秒。VS が閃光に使う）
        float    attackCooldown = 0.0f;   // 接触攻撃の残り待ち時間。ContactCS だけが書く
    };
    static_assert(sizeof(Enemy) == 48, "SwarmEnemy layout mismatch");

    struct Projectile
    {
        Vector3  position;
        float    damage = 0.0f;
        Vector3  velocity;
        float    lifetime = 0.0f;
        float    radius = 0.25f;
        uint32_t vfxType = 0;      // index into SwarmVFXTable's recipe table
        uint32_t motion = 0;       // 運動表（Motion）の番号。生成依頼では bit31 = 曲線を左右反転
        float    pathT = 0.0f;     // 曲線上の位置 0..1。GPU が進める
    };
    static_assert(sizeof(Projectile) == 48, "SwarmProjectile layout mismatch");

    // ============================================================
    // 投射物の運動（飛び方）
    //
    // Motion   : 投射物プロファイル 1 個につき 1 行。編集器のデータがここへ入る。
    //            HLSL の SwarmMotion と同じ並び（48B）
    // ProjPath : 投射物スロットと同じ添字。今飛んでいる 3 次ベジェ。
    //            GPU が生成時（と再捕捉時）に組み立てる。CPU は中身を触らない（64B）
    //
    // 制御点は「銃口 → 標的」の座標系で、射距離に比例させて持つ。
    // 近くても遠くても同じ形の曲線になる：
    //     c.x = 銃口 → 標的 の何割の位置か
    //     c.y = 横へのずれ / 射距離（発射ごとに左右反転できる）
    //     c.z = 上へのずれ / 射距離（世界の上方向）
    // ============================================================
    constexpr uint32_t kMaxMotions = 64;
    constexpr uint32_t kMotionFlipBit = 0x80000000u;

    enum class MotionMode : uint32_t
    {
        Straight = 0,    // 直進。敵を一切見ない
        CurveOnce = 1,   // 1 回だけ捕捉して曲線で飛ぶ。標的が死んだら今の向きで直進
        Track = 2,       // 標的が死んでも、自分に一番近い敵を探して曲線を組み直す
    };

    struct Motion
    {
        uint32_t mode = 0;
        float    retargetRadius = 0.0f;   // Track の再捕捉半径。0 以下 = 無制限
        uint32_t hitArea = 0;             // 命中した場所に出す範囲（AreaDef の番号）。0 = 無し
        uint32_t hitAreaFlags = 0;        // bit0 = 寿命切れ・壁に当たった時も出す
        Vector3  c1 = { 0.33f, 0.0f, 0.0f };
        float    _pad1 = 0.0f;
        Vector3  c2 = { 0.66f, 0.0f, 0.0f };
        float    _pad2 = 0.0f;
    };
    static_assert(sizeof(Motion) == 48, "SwarmMotion layout mismatch");

    struct ProjPath
    {
        Vector3  p0;
        uint32_t target = 0xFFFFFFFFu;
        Vector3  p1;
        float    duration = 1.0f;
        Vector3  p2;
        float    sideSign = 1.0f;
        Vector3  p3;
        float    speed = 0.0f;
    };
    static_assert(sizeof(ProjPath) == 64, "SwarmProjPath layout mismatch");

    // ============================================================
    // 範囲攻撃（爆発・法環）
    //
    // Area    : 生きている範囲 1 個（48B）。HLSL の SwarmArea と同じ並び。
    //           単発と持続は同じ物：爆発 = 1 回だけ tick して、粒子が出終わるまで残る範囲
    // AreaDef : 雛形（32B）。GPU が自分で範囲を出す時（弾の命中）に引く表。0 番 = 無し
    //
    // 形は円盤：XZ の距離 <= radius かつ 高さの差 <= halfHeight（+ 雑魚のカプセル）
    // ============================================================
    constexpr uint32_t kAreaFollowPlayer = 1u;   // 中心が玩家に付いて動く
    constexpr uint32_t kAreaStun = 2u;           // tick で被弾硬直 + 閃光を入れる
    constexpr uint32_t kHitAreaOnExpire = 1u;    // Motion::hitAreaFlags

    struct Area
    {
        Vector3  center;
        float    radius = 3.0f;
        float    damage = 0.0f;          // 1 tick あたり
        float    timeLeft = 0.0f;
        float    tickInterval = 0.25f;
        float    tickTimer = 0.0f;       // 0 = 出た最初のステップで tick する
        float    halfHeight = 1.5f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;            // GPU 側で粒子を出す配方。0 = 出さない（CPU が VFX を再生する）
        uint32_t tickNow = 0;            // GPU が書く
    };
    static_assert(sizeof(Area) == 48, "SwarmArea layout mismatch");

    struct AreaDef
    {
        float    radius = 3.0f;
        float    halfHeight = 1.5f;
        float    damage = 0.0f;
        float    duration = 0.3f;
        float    tickInterval = 1.0e9f;
        uint32_t flags = 0;
        uint32_t vfxType = 0;
        uint32_t _pad = 0;
    };
    static_assert(sizeof(AreaDef) == 32, "SwarmAreaDef layout mismatch");
    
    struct Orb
    {
        Vector3  position;
        float    amount = 0.0f;
        Vector3  velocity;
        float    _pad = 0.0f;
    };
    static_assert(sizeof(Orb) == 32, "SwarmOrb layout mismatch");

    // ============================================================
    // 毎フレームの定数（b0）
    // ============================================================
    struct FrameCB
    {
        Vector3  playerPos;
        float    playerRadius = 0.4f;

        float    step = kFixedStep;
        uint32_t maxEnemies = kMaxEnemies;
        uint32_t maxProjectiles = kMaxProjectiles;
        uint32_t maxOrbs = kMaxOrbs;

        Vector3  gridOrigin;
        float    cellSize = 2.0f;

        uint32_t gridW = 0;
        uint32_t gridD = 0;
        uint32_t seed = 0;
        uint32_t playerAlive = 1;
    };
    static_assert(sizeof(FrameCB) == 64, "SwarmFrameCB layout mismatch");

    // ============================================================
    // 生成用の定数（b1）
    // ============================================================
    struct SpawnCB
    {
        uint32_t requestCount = 0;
        uint32_t scanStart = 0;    // 毎フレーム回して探索開始位置をずらす
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(SpawnCB) == 16, "SwarmSpawnCB layout mismatch");
    // ============================================================
// 雑魚 AI の調整値（b1）
// ChaseAISystem の public メンバをそのまま持ってきた。
// 雑魚は全員同じ体格なので半径もここ（本体の 48B を崩さない）
// ============================================================
    struct AICB
    {
        float separationRadius = 1.2f;
        float separationPower = 4.0f;
        float avoidPower = 6.0f;
        float lookAhead = 0.35f;        // 秒。速度 × これ で先読み距離

        float groundY = 0.9f;
        float enemyRadius = 0.4f;       // カプセルの半径
        float enemyCapsuleHalf = 0.5f;  // 直線部分の半分
        float velocityLag = 10.0f;      // 速度の追従係数。大きいほど機敏（∞ = 即時）

        float maxSpeedMul = 1.5f;       // 合成速度の上限 = moveSpeed × これ
        float turnSpeed = 12.0f;        // 旋回の上限（rad/s）
        float playerPushOut = 10.0f;    // 玩家に食い込んだ時に押し戻す強さ（1/s）
        float contactDamage = 10.0f;    // 接触1回のダメージ

        float attackInterval = 1.0f;    // 同じ雑魚が次に殴れるまでの秒数
        float playerCapsuleHalf = 0.5f; // 玩家カプセルの直線部の半分。シーンが毎フレーム入れる
        float hitStun = 0.08f;          // 被弾で止まる秒数（HitCS が animIndex=2 を立て、MoveCS が数える）
        float hitFlash = 3.0f;          // 被弾直後の頂点色の倍率。1 へ減衰。Bloom で光る
    };
    static_assert(sizeof(AICB) == 64, "SwarmAICB layout mismatch");

    // ============================================================
   // 経験値オーブの調整値（b2）
   // HitCS（生成）と OrbMoveCS（吸引・取得）の両方が読む
   // ============================================================
    struct OrbCB
    {
        float attractRadius = 4.0f;   // ここに入ると吸い寄せ開始
        float pickupRadius = 0.6f;    // ここまで来たら取得
        float accel = 30.0f;          // 吸い寄せの加速度
        float maxSpeed = 18.0f;

        float amount = 10.0f;         // 1個あたりの経験値（今は全敵共通）
        float orbY = 0.5f;            // オーブの浮遊高さ
        float _pad[2] = {};
    };
    static_assert(sizeof(OrbCB) == 32, "SwarmOrbCB layout mismatch");
    // ============================================================
  // 転送回収用（b1）
  // 生成キューの [offset, offset+count) を、玩家から minDist より
  // 遠い活き雑魚へ上書きする
  // ============================================================
    struct RecycleCB
    {
        uint32_t count = 0;
        uint32_t offset = 0;
        float    minDistSq = 0.0f;
        uint32_t _pad = 0;
    };
    static_assert(sizeof(RecycleCB) == 16, "SwarmRecycleCB layout mismatch");
}