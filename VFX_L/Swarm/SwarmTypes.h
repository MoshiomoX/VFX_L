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

    // ============================================================
    // 雑魚の種類（Enemy 本体の 48B は変えず、スロットと同じ添字の並行バッファに持つ）
    //   生成依頼では Enemy::animIndex に種類を入れて運ぶ（SpawnEnemyCS / RecycleCS が
    //   並行バッファへ移して animIndex を 0 に戻す。弾の motion の bit31 と同じ流儀）
    // ============================================================
    constexpr uint32_t kEnemyKindMob = 0;      // 普通の雑魚（接触で殴る）
    constexpr uint32_t kEnemyKindBomber = 1;   // 自爆兵（接触で点火 → fuseTime 秒後に爆発）
    constexpr uint32_t kEnemyKindElite = 2;    // 精英（大きい雑魚。BomberCB の elite* で体格・接触ダメージ・経験値を倍にする）
    constexpr uint32_t kEnemyKindBoss = 3;     // 面の Boss（BomberCB の boss*。怯まない。HP と位置は BossInfo で CPU へ）
    constexpr uint32_t kEnemyKindGhost = 4;    // 最終波の幽霊（2026-09-30）：雑魚の HP、速い、壁も台地も素通り、半透明の青白
    // 描画リストの数（種類毎に貼図を替えて描く）。精英は雑魚と同じ網格・貼図なので雑魚のリストで描き、
    // 大きさと色は VS が種類を見て変える
    constexpr uint32_t kEnemyKinds = 3;
    // 描画リストの添字（種類 → リスト。精英 / Boss は雑魚のリスト）
    constexpr uint32_t kDrawListMob = 0;
    constexpr uint32_t kDrawListBomber = 1;
    constexpr uint32_t kDrawListGhost = 2;    // 最後に alpha blend で描く

    struct EnemyExtra
    {
        uint32_t kind = kEnemyKindMob;
        float    fuse = 0.0f;   // 点火からの秒数。0 = 未点火（点火した瞬間に 1 ステップ分が入る）
    };
    static_assert(sizeof(EnemyExtra) == 8, "SwarmEnemyExtra layout mismatch");

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

    // ============================================================
    // 誘発（基礎魔法 → 高級魔法。火球・石弾が消えた所に隕石が落ちる）
    //   生成依頼に並行の uint2 を付ける（Projectile の 48B は変えない）:
    //     x = 誘発タグ。bit k = 「この弾が消えたら、杖の k 番の高級魔法がそこで撃てる」
    //     y = kSpawnAtPos：Drop 型の着弾点を依頼の position にする（最寄りの敵を捕捉しない）
    //   タグはスロット毎の projTags に残り、SwarmProjEndCS が「このステップで消えたタグ付きの弾」を
    //   環（先頭 16B = 今までに書いた総数、以降 16B × kMaxTriggerEvents）へ書く。CPU は回読して差分を取る。
    //   HLSL の SWARM_MAX_TRIGGER_EVENTS / SWARM_SPAWN_AT_POS と一致させること
    // ============================================================
    constexpr uint32_t kMaxTriggerEvents = 128;
    constexpr uint32_t kSpawnAtPos = 1u;

    // ============================================================
    // 死んだ敵の砕け散り（2026-10-02）
    //   SwarmCorpseTrackCS が「前のフレームは生きていて今は DEAD」の槽を見つけ、その時の位置・向き・種類を
    //   環（kMaxCorpses 枠、先頭は RAW の通し番号）へ 1 つ書く。描画（SwarmCorpseVS）は部品毎に
    //   (seed, 経過時間) の式で飛び散らせる。状態は持たない。HLSL の SwarmCorpse / SWARM_MAX_CORPSES と一致
    // ============================================================
    constexpr uint32_t kMaxCorpses = 512;
    struct Corpse
    {
        Vector3  position;          // 死んだ時の位置（雑魚の position = 地面 + groundY）
        float    yaw = 0.0f;
        float    dir[2] = {};       // 飛ばす向き（xz の単位。玩家から離れる向き）
        float    birth = 0.0f;      // 死んだ時刻（SwarmSystem の時計 m_AnimClock）。0 = 空き
        uint32_t kind = 0;
        uint32_t seed = 0;
        uint32_t cause = 0;         // 予約：死因（火・毒・雷で死に方を変える時用。今は 0）
        float    _pad[2] = {};
    };
    static_assert(sizeof(Corpse) == 48, "SwarmCorpse layout mismatch");

    // ============================================================
    // 弾が出す範囲（命中・着弾の hitArea）への倍率（2026-10-02）
    //   y の bit 8〜19 = 威力の倍率、bit 20〜31 = 持続の倍率（どちらも 256 = 1.0、0 は 1.0 扱い）。
    //   SwarmSpawnProjCS がスロット毎の projBoost へ写し、範囲を出す所（HitCS / ProjMoveCS）が掛ける。
    //   威力 = 能力アップ「魔法威力」、持続 = 魔力解放中に撃った弾（持続する範囲だけ伸びる）。
    //   HLSL の SWARM_SPAWN_BOOST_SHIFT / SwarmBoostDamage / SwarmBoostDuration と一致させること
    // ============================================================
    constexpr uint32_t kSpawnBoostShift = 8u;
    inline uint32_t PackSpawnBoost(float damageMul, float durationMul)
    {
        auto q = [](float v) -> uint32_t
            {
                const float c = (v < 1.0f / 256.0f) ? 1.0f / 256.0f : (v > 4095.0f / 256.0f ? 4095.0f / 256.0f : v);
                return (uint32_t)(c * 256.0f + 0.5f);
            };
        return (q(damageMul) | (q(durationMul) << 12)) << kSpawnBoostShift;
    }

    struct TriggerEvent
    {
        Vector3  position;    // 弾が消えた所の地面（地形の高さ）
        uint32_t tag = 0;
    };
    static_assert(sizeof(TriggerEvent) == 16, "SwarmTriggerEvent layout mismatch");

    enum class MotionMode : uint32_t
    {
        Straight = 0,    // 直進。敵を一切見ない
        CurveOnce = 1,   // 1 回だけ捕捉して曲線で飛ぶ。標的が死んだら今の向きで直進
        Track = 2,       // 標的が死んでも、自分に一番近い敵を探して曲線を組み直す
        // 隕石: 撃った時に一番近い敵の位置を着弾点に決め、空から斜めに落ちる（途中で敵に当たらない）。
        // c1 の意味が違う（m 単位）: c1.x = 着弾点からの高さ、c1.y = 銃口側へ戻した水平距離。
        // 落ちる所には警告の輪（SwarmSystem::dropRing）
        Drop = 3,
        // 投げ上げ（2026-10-01、毒）: 撃った時に一番近い敵の位置を着弾点に決め、銃口から山なりに投げる。
        // 道筋は曲線と同じ c1 / c2（c.z で持ち上げる）、振る舞いは Drop と同じ（途中で当たらず、地形も見ず、着弾点に hitArea）
        Lob = 4,
    };

    struct Motion
    {
        uint32_t mode = 0;
        float    retargetRadius = 0.0f;   // Track の再捕捉半径。0 以下 = 無制限
        uint32_t hitArea = 0;             // 命中した場所に出す範囲（AreaDef の番号）。0 = 無し
        uint32_t hitAreaFlags = 0;        // bit0 = 寿命切れ・壁に当たった時も出す
        Vector3  c1 = { 0.33f, 0.0f, 0.0f };
        float    baseRadius = 0.0f;       // profile の当たり半径。これより大きく撃たれた弾は見た目も大きく（拡大鏡）。0 = 倍率 1
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
    constexpr uint32_t kAreaCapsule = 4u;        // 胶囊（光線）：中心 → 終点の線分の周り。起点 / 終点 / 半径は毎ステップ BeamCB から（2026-09-30）
    constexpr uint32_t kAreaShake = 8u;          // 出た時に鏡頭を揺らす（爆発。2026-10-03：命中の火花・死んだ時の土煙・毒の池まで揺らしていた）
    // 生まれた範囲を GPU の VFX 配方（Area::vfxType）毎に数える数（SwarmLiquidTrackCS、音を鳴らす用。2026-10-03）。
    // 配方番号がこれ以上の物は最後の枠にまとめる
    constexpr uint32_t kAreaBirthKinds = 127u;
    constexpr uint32_t kAreaBeamShift = 8u;      // (flags >> 8) & 0xF = 光線のチャンネル（BeamCB の添字）
    // 減速（2026-10-01、毒の池）: (flags >> 12) & 0xF = q、tick の度に中の敵を q / 15 だけ遅くする（精英・Boss は半分）
    constexpr uint32_t kAreaSlowShift = 12u;
    constexpr uint32_t kMaxBeams = 4;            // 同時に出せる光線（HLSL の SWARM_MAX_BEAMS と同じ）
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
        float contactDamage = 15.0f;    // 接触1回のダメージ（Megabonk 1 面の雑魚 8〜25）

        float attackInterval = 1.0f;    // 同じ雑魚が次に殴れるまでの秒数
        float playerCapsuleHalf = 0.5f; // 玩家カプセルの直線部の半分。シーンが毎フレーム入れる
        float hitStun = 0.08f;          // 被弾で止まる秒数（HitCS が animIndex=2 を立て、MoveCS が数える）
        float hitFlash = 3.0f;          // 被弾直後の頂点色の倍率。1 へ減衰。Bloom で光る
    };
    static_assert(sizeof(AICB) == 64, "SwarmAICB layout mismatch");

    // ============================================================
    // 自爆兵の調整値（ContactCS は b3、雑魚の VS は b5）
    // 爆発のダメージは玩家だけ。雑魚には入らない（見た目の範囲は威力 0）
    // ============================================================
    struct BomberCB
    {
        float    fuseTime = 1.0f;       // 点火から爆発までの秒数
        float    triggerMargin = 0.15f; // 接触（半径の和）+ これ以内で点火
        float    blastRadius = 2.5f;    // 爆発の瞬間に玩家（カプセル）がこの中なら被弾
        float    blastDamage = 30.0f;     // Megabonk の Boomer と同じ

        uint32_t blastArea = 0;         // 爆発の見た目に出す範囲（AreaDef の番号）。0 = 出さない
        float    swell = 0.35f;         // VS: 爆発直前の膨らみ（1 + これ 倍まで）
        float    flashGain = 4.0f;      // VS: 点滅の明るさ（Bloom で光る）
        float    _pad = 0.0f;

        // ---- 精英（kEnemyKindElite）。種類の規則を読む pass はこの CB を持っているので相乗りする ----
        float    eliteScale = 2.2f;       // 体格（模型・当たり / 接触の半径・HP 条の高さ）
        float    eliteDamageMul = 40.0f / 15.0f;   // 接触ダメージの倍率（Megabonk の小ボス 40 / 雑魚 15）
        float    eliteExpMul = 20.0f;     // 落とす経験値オーブの倍率（1 個で雑魚 20 体分）
        float    _elitePad = 0.0f;

        // ---- 面の Boss（kEnemyKindBoss）----
        float    bossScale = 3.0f;           // 4 だと目の前に来た時に画面の大半を塞ぐ
        float    bossDamageMul = 40.0f / 15.0f;   // Megabonk の面 Boss 22〜40 / 雑魚 15
        float    bossExpMul = 100.0f;
        float    _bossPad = 0.0f;

        // ---- 最終波の幽霊（kEnemyKindGhost）----
        float    ghostHover = 0.7f;          // VS: 地面からこれだけ浮く（判定の位置は地面のまま）
        float    ghostAlpha = 0.55f;         // VS: 頂点 alpha（alpha blend で描く）
        float    ghostGlow = 1.6f;           // VS: 青白の色に掛ける HDR の倍率
        float    ghostDamageMul = 1.0f;      // 接触ダメージの倍率
    };
    static_assert(sizeof(BomberCB) == 80, "SwarmBomberCB layout mismatch");

    // ============================================================
    // Boss の様子（GPU → CPU。SwarmEnemyCompactCS が毎フレーム書き、staging で回読）。
    // Boss HP 条・画面外の目印・倒したかの判定に使う（回読なので 2〜3 フレーム古い）
    // ============================================================
    struct BossInfo
    {
        uint32_t alive = 0;    // 生きている Boss の数
        uint32_t hp = 0;       // 固定小数（HpFromFixed で戻す）。複数居たら最後に書いた 1 体
        uint32_t maxHp = 0;
        uint32_t _pad = 0;
        float    pos[3] = {};
        float    _pad2 = 0.0f;
    };
    static_assert(sizeof(BossInfo) == 32, "SwarmBossInfo layout mismatch");

    // ============================================================
    // 玩家が受けた打撃の向き（GPU → CPU、2026-10-01 ノックバック用）。
    // SwarmContactCS が殴られた / 爆発を受けた度に「敵（爆心）→ 玩家」の単位ベクトル × 1000 と回数を足す。
    // counters と同じく GPU 上で永久に累加し、CPU は前回値との差分を取る（消さない。読み損ねても取りこぼさない）
    // ============================================================
    struct PlayerHitInfo
    {
        int32_t  meleeX = 0, meleeZ = 0;   // 近接の向きの和（× kHitDirScale）
        uint32_t meleeCount = 0;
        int32_t  blastX = 0, blastZ = 0;   // 爆発の向きの和
        uint32_t blastCount = 0;
        uint32_t _pad[2] = {};
    };
    static_assert(sizeof(PlayerHitInfo) == 32, "SwarmPlayerHitInfo layout mismatch");
    constexpr float kHitDirScale = 1000.0f;   // = SwarmContactCS の HIT_DIR_SCALE

    // ============================================================
   // 経験値オーブの調整値（b2）
   // HitCS（生成）と OrbMoveCS（吸引・取得）の両方が読む
   // ============================================================
    struct OrbCB
    {
        float attractRadius = 5.0f;   // ここに入ると吸い寄せ開始（Megabonk の拾う範囲 5）
        float pickupRadius = 0.6f;    // ここまで来たら取得
        float accel = 30.0f;          // 吸い寄せの加速度
        float maxSpeed = 18.0f;

        float amount = 10.0f;         // 1個あたりの経験値（今は全敵共通）
        float orbY = 0.5f;            // オーブの浮遊高さ
        float _pad[2] = {};
    };
    static_assert(sizeof(OrbCB) == 32, "SwarmOrbCB layout mismatch");

    // ============================================================
    // 光線（胶囊型の範囲）の起点 / 終点（b3、AreaTickCS）。CPU が毎フレーム書く。
    // start.w = 半径、end.w = 1 の間だけ生きる（0 にすると GPU 側の範囲が消える）
    // ============================================================
    struct BeamCB
    {
        DirectX::SimpleMath::Vector4 start[kMaxBeams];
        DirectX::SimpleMath::Vector4 end[kMaxBeams];
    };
    static_assert(sizeof(BeamCB) == 128, "SwarmBeamCB layout mismatch");

    // ============================================================
    // 光線の標的（b4、SwarmBeamTargetCS、2026-10-01）。チャンネル毎:
    //   origin.w = asfloat(cmd)（0 = 未使用 / 1 = 開始: seek に一番近い敵を捕まえる / 2 = 追跡: 捕まえた敵が
    //   死んだら・遠くへ行ったら、射程内で光線の向きに一番近い敵へ乗り換える）、dir.w = 射程、seek.w = asfloat(serial)
    // 結果は 32B × チャンネル（slot, serial, valid, pad, pos.xyz, pad）を staging で回読
    // ============================================================
    enum : uint32_t { kBeamTargetIdle = 0, kBeamTargetStart = 1, kBeamTargetTrack = 2 };
    struct BeamTargetCB
    {
        DirectX::SimpleMath::Vector4 origin[kMaxBeams];
        DirectX::SimpleMath::Vector4 dir[kMaxBeams];
        DirectX::SimpleMath::Vector4 seek[kMaxBeams];
    };
    static_assert(sizeof(BeamTargetCB) == 192, "SwarmBeamTargetCB layout mismatch");
    struct BeamTarget
    {
        uint32_t slot = 0xFFFFFFFFu;
        uint32_t serial = 0;
        uint32_t valid = 0;
        uint32_t _pad = 0;
        float    pos[3] = {};
        float    _pad2 = 0.0f;
    };
    static_assert(sizeof(BeamTarget) == 32, "SwarmBeamTarget layout mismatch");
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
