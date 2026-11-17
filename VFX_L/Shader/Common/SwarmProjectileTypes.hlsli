// ============================================================
// SwarmProjectileTypes.hlsli
// 弾の構造体（SwarmProjectile / SwarmMotion / SwarmProjPath）、弾道のモード、
// 生成リクエストの追加情報（SwarmSpawnProjExtra）、連鎖・貫通（SwarmProjChain）、範囲の強化、ベジェ曲線。
// ============================================================

#ifndef SWARM_PROJECTILE_TYPES_HLSLI
#define SWARM_PROJECTILE_TYPES_HLSLI

// ============================================================
// 弾：48 バイト
// ============================================================
struct SwarmProjectile
{
    float3 position;
    float damage;

    float3 velocity;
    float lifetime;

    float radius;
    uint vfxType; // SwarmVFXTable のレシピ表の添字
    uint motion; // ビット 0-15 = 弾道表の添字、16-30 = 大きさの倍率（SwarmProjScale）。
                 // 生成リクエストでは弾道表の添字だけ（他は SwarmSpawnProjExtra）
    float pathT; // 曲線を飛んでいる間のベジェパラメータ 0..1。< 0 = 飛ぶ前の停止の残り秒
};

// ============================================================
// 弾道
//
// SwarmMotion   : 弾のプロファイル 1 つにつき 1 行（エディタのデータ）。
//                 SwarmProjectileTypes.h の Swarm::Motion と一致させること。64 バイト。
// SwarmProjPath : 弾スロット 1 つにつき 1 つ、プールと同じ添字。
//                 この弾が飛んでいる 3 次ベジェ。生成時に GPU で作る
//                 （狙い直す時にも作り直す）。
//                 Swarm::ProjPath と一致させること。64 バイト。
//
// 制御点は射撃の座標系で書き、射撃の距離で拡縮するので、
// 1 本の曲線で近い標的にも遠い標的にも合う：
//     c.x = 銃口 -> 標的 に沿った割合
//     c.y = 横方向のずれ / 距離   （sideAngle だけ飛行軸まわりに回す。pi = 左右反転）
//     c.z = 上方向のずれ / 距離   （ワールドの上）
//
// DROP（メテオ）は c1 を別の意味で使う（単位 m）：
//     c1.x = 着弾点から見た開始点の高さ
//     c1.y = 開始点の水平オフセット（銃口の方へ戻る向き）
//            （なので水平から atan(c1.x / c1.y) の角度で落ちる）
// 着弾点は生成時（その時の敵の位置）に固定するので、
// 地面の警告リング（SwarmDropRingVS）はちょうど落ちる場所に出る。
// ============================================================
static const uint SWARM_MOTION_STRAIGHT = 0;
static const uint SWARM_MOTION_CURVE_ONCE = 1;
static const uint SWARM_MOTION_TRACK = 2;
static const uint SWARM_MOTION_DROP = 3;
// LOB（毒の魔法）：銃口から弧を描いて、生成時に一番近い敵が
// 立っていた場所へ投げる。CURVE と同じように経路を作る
// （c1 / c2 は射撃の座標系なので c.z で弧が持ち上がる）が、振る舞いは DROP と同じ：
// 途中で敵に当たらず、地形判定もせず、必ず p3 に落ちて
// そこに命中範囲を残す。
static const uint SWARM_MOTION_LOB = 4;
// プレイヤーの周りを回る：c1.x = rad/s、c1.y = 半径 m、c1.z = 高さオフセット m。path.sideAngle の角度から始める
static const uint SWARM_MOTION_ORBIT = 5;

bool SwarmMotionLands(uint mode)
{
    return mode == SWARM_MOTION_DROP || mode == SWARM_MOTION_LOB;
}

static const uint SWARM_NO_TARGET = 0xFFFFFFFFu;
static const uint SWARM_MOTION_INDEX_MASK = 0xFFFFu;

// ------------------------------------------------------------
// 弾の生成リクエストに並ぶ追加情報（SwarmSpawnProjCS t4、リクエストと同じ添字）。
// Swarm::SpawnProjExtra と一致させること。32 バイト。
//   tag      : 連鎖タグ（基本魔法 -> 上級魔法、例：ファイアボール / ストーンショット -> メテオ）。
//              ビット k =「この弾が消えたら、杖の上級魔法 k をそこで発動してよい」。最上位ビット = 吸血の弾
//              （SWARM_TAG_LIFESTEAL）。スロットごとに projTags に保持する
//   flags    : SWARM_SPAWN_AT_POS = DROP / LOB は杖の狙い（SWARM_CNT_AIM_KEY）を狙わず、リクエストの位置に落ちる。
//              SWARM_SPAWN_FLIP = 曲線の横ずれを左右反転
//   roll     : 曲線の横ずれの回転（0..127 = 0..360 度。SpawnProjCS が SwarmProjPath.sideAngle にする）
//   boost    : 弾が残す範囲の強化（SwarmBoostDamage / SwarmBoostDuration）。スロットごとに projBoost に保持する
//   chains   : 連鎖のルーンで跳べる回数（0 = なし）
//   fanIndex : 出口魔法の扇の何発目か（0 = 1 発目）
// SwarmProjEndCS は、このステップで消えたタグ付きの弾をリングに書く
// （ヘッダ 16B：[0] = これまでに書いたイベントの総数、その後に
// SWARM_MAX_TRIGGER_EVENTS 個の 16B のエントリ：地面の位置 xyz + タグ）。
// CPU はリングをリードバックし、まだ見ていないエントリを取る。
// Swarm::kMaxTriggerEvents / kSpawnAtPos / kSpawnFlip と一致させること
// ------------------------------------------------------------
struct SwarmSpawnProjExtra
{
    uint tag;
    uint flags;
    uint roll;
    uint boost;
    uint chains;
    uint fanIndex;
    uint2 _pad;
};
static const uint SWARM_MAX_TRIGGER_EVENTS = 128u;
static const uint SWARM_SPAWN_AT_POS = 1u;
static const uint SWARM_SPAWN_FLIP = 2u;

// ------------------------------------------------------------
// 弾が残す範囲の強化（projBoost）。ビット 0-11 = 威力の倍率、
// ビット 12-23 = 持続の倍率、256 = 1.0（0 は 1.0 として読む）。
// HitCS / ProjMoveCS がそれを SwarmSpawnAreaFromDef に渡す。
// 威力 =「魔法威力」の能力カード、持続 = 魔力解放中に詠唱した弾
// （持続する範囲だけ長くなる）。
// Swarm::PackSpawnBoost と一致させること
// ------------------------------------------------------------

// ------------------------------------------------------------
// 連鎖のルーンと貫通：弾スロットごとの並列バッファ projChain（16 バイト）。
// Swarm::ProjChain と一致させること。SpawnProjCS が生成のたびに書く。
// 連鎖が残っている弾は、生きた敵に当たっても消えない。その後 ProjMoveCS が
// SWARM_CHAIN_RADIUS 内で一番近い別の敵を狙い、今の位置から新しい曲線を作る。
//   flags   : SWARM_CHAIN_ACTIVE   連鎖（または貫通）中の弾
//             SWARM_CHAIN_PENDING  HitCS がこのステップで何かに当たった。ProjMoveCS が次の敵を選ぶ（そしてこれを消す）
//             SWARM_CHAIN_PIERCE   貫通（黄金の矢）：跳ばずに通り抜ける
//   left    : 残りの跳躍（貫通）回数
//   lastHit : 最後に当たった敵のスロット（HitCS はスキップする。SWARM_NO_TARGET = なし）
// 回数は生成リクエストの SwarmSpawnProjExtra.chains で届く。SwarmSystem::SpawnProjectile(chainCount)
// ------------------------------------------------------------
struct SwarmProjChain
{
    uint flags;
    uint left;
    uint lastHit;
    uint _pad;
};
static const uint SWARM_CHAIN_ACTIVE = 1u;
static const uint SWARM_CHAIN_PENDING = 2u;
static const uint SWARM_CHAIN_PIERCE = 4u;
static const uint SWARM_CHAIN_MAX_LEFT = 255u;   // 跳躍回数の上限
static const float SWARM_CHAIN_RADIUS = 8.0;
SwarmProjChain SwarmMakeProjChain(uint flags, uint left, uint lastHit)
{
    SwarmProjChain c;
    c.flags = flags;
    c.left = left;
    c.lastHit = lastHit;
    c._pad = 0u;
    return c;
}
// 貫通（黄金の矢）：生きた敵に当たると、貫通を 1 つ減らし、当たった敵を覚えて（弾が通り抜ける
// 間はスキップ）弾は生き続けるが、狙い直しはしない：PENDING は立てず、
// ProjMoveCS は直進弾をまっすぐのまま飛ばす。
// 貫通するたびに威力を SWARM_PIERCE_FALLOFF 倍する。回数は
// プロファイルから（SwarmMotion.pierce）。連鎖のルーンも付くと、
// 貫通数を跳躍回数に足し、弾は跳ぶ（連鎖の挙動）
static const float SWARM_PIERCE_FALLOFF = 0.8;


float SwarmBoostDamage(uint boost)
{
    uint q = boost & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

float SwarmBoostDuration(uint boost)
{
    uint q = (boost >> 12) & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

// 生きている弾の大きさの倍率（1 = 作ったまま）
float SwarmProjScale(SwarmProjectile p)
{
    return SwarmUnpackScale((p.motion >> SWARM_SCALE_SHIFT) & 0x7FFFu);
}

struct SwarmMotion
{
    uint mode;
    float retargetRadius; // TRACK 専用。<= 0 = 無制限
    uint hitArea; // 弾が当たった所に生成する範囲の定義。0 = なし
    uint hitAreaFlags; // SWARM_HITAREA_ON_EXPIRE / SWARM_HITAREA_EASE_IN

    float3 c1;
    float baseRadius; // プロファイルの当たり判定の半径：これより大きい弾は大きく描く

    float3 c2;
    float freezeTime; // > 0：当たった敵をこの秒数だけ凍らせる（アイスランス）

    float holdTime; // 飛ぶ前に詠唱者の前で止まる秒（0 = 止まらない。ストーンショット）
    uint element;   // ダメージの属性（0 なし、1 炎、2 氷、3 雷、4 奥術、5 土、6 毒、7 風、8 光）。ダメージ数字の色だけに使う
    uint pierce;    // 最初の命中の後に貫通する敵の数（0..15、黄金の矢）
    uint _pad;
};

struct SwarmProjPath
{
    float3 p0;
    uint target; // 敵のスロット、SWARM_NO_TARGET = 直進中

    float3 p1;
    float duration; // p0 から p3 までの秒

    float3 p2;
    float sideAngle; // 横オフセットを飛行軸まわりに回す角度（ラジアン、1 発ごとに固定。0 = 右、pi = 左、それ以外 = ランダム。SwarmBuildPath）

    float3 p3;
    float speed; // 公称速度。曲線を離れた時に戻す
};

float3 SwarmBezier(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return c.p0 * (u * u * u)
         + c.p1 * (3.0 * u * u * t)
         + c.p2 * (3.0 * u * t * t)
         + c.p3 * (t * t * t);
}

float3 SwarmBezierTangent(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return (c.p1 - c.p0) * (3.0 * u * u)
         + (c.p2 - c.p1) * (6.0 * u * t)
         + (c.p3 - c.p2) * (3.0 * t * t);
}

// p0..p3 と duration を埋める。sideAngle / speed / target は呼び出し側が持つ。
// keepHeading：c1 を使わず `heading` に沿って出る（飛行中の狙い直し）ので、
// 新しい曲線の始まりで経路が折れない。
void SwarmBuildPath(inout SwarmProjPath path, SwarmMotion m,
                    float3 from, float3 to, float3 heading, bool keepHeading)
{
    float3 chord = to - from;
    float dist = length(chord);
    float3 fwd = (dist > 1e-4) ? chord / dist : heading;

    float3 up = float3(0, 1, 0);
    float3 side = cross(up, fwd);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-6) ? side * rsqrt(sideLenSq) : float3(1, 0, 0);

    // 横オフセット（c.y）は path.sideAngle（ラジアン）だけ飛行軸まわりに回す：
    // 0 = 右、pi = 左、それ以外 = 1 発ごとにランダムな方向。c.z はワールドの上のまま
    float3 lateral = side * cos(path.sideAngle) + cross(fwd, side) * sin(path.sideAngle);

    path.p0 = from;
    path.p3 = to;
    path.p1 = from + fwd * (m.c1.x * dist)
                   + lateral * (m.c1.y * dist)
                   + up * (m.c1.z * dist);
    path.p2 = from + fwd * (m.c2.x * dist)
                   + lateral * (m.c2.y * dist)
                   + up * (m.c2.z * dist);
    if (keepHeading)
        path.p1 = from + heading * (dist / 3.0);

    // 弧長 ≒ 弦と制御多角形の平均
    float len = 0.5 * (dist + length(path.p1 - path.p0)
                            + length(path.p2 - path.p1)
                            + length(path.p3 - path.p2));
    path.duration = max(len / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// DROP：開始点から着弾点への直線。ハンドルを線上に置き、
// 進んだ距離が s(t) = 0.4 t + 0.6 t^2 になるようにする：落ちるほど速くなる
// （着弾時に公称速度の 1.6 倍、平均すると公称速度）。
// path.speed は呼び出し側が設定すること。
void SwarmBuildDropPath(inout SwarmProjPath path, float3 start, float3 impact)
{
    float3 d = impact - start;
    path.p0 = start;
    path.p1 = start + d * (0.4 / 3.0);
    path.p2 = start + d * ((2.0 * 0.4 + 0.6) / 3.0);
    path.p3 = impact;
    path.duration = max(length(d) / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// SwarmMotion.hitAreaFlags。Swarm::kHitAreaOnExpire / kHitAreaEaseIn と一致させること
static const uint SWARM_HITAREA_ON_EXPIRE = 1u; // 寿命切れ / 壁に当たった時も範囲を生成する
// 加速（ストーンショット：岩が一瞬空中に止まり、大きく加速して飛ぶ）：曲線パラメータを 2 乗する
// （速度 0 で始まり、公称速度の 2 倍で終わる。合計時間は同じ）。停止は SwarmMotion.holdTime
// （SpawnProjCS が pathT に -holdTime を入れ、ProjMoveCS が 0 まで数え上げてから飛ばす）
static const uint SWARM_HITAREA_EASE_IN = 1u << 16u;
static const float SWARM_HOLD_FORWARD = 1.0; // 停止する弾は銃口からこれだけ前に生まれる（m）

#endif
