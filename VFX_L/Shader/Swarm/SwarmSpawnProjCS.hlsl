// ============================================================
// SwarmSpawnCS.hlsl
// 溜まった生成リクエストをプールの空きスロットに入れる。
//
// 1 リクエスト 1 スレッド。各スレッドが死んだスロットを探し、
// 状態バッファへの InterlockedCompareExchange で確保する。
//
// フリーリストではなく線形に走査する理由:
//   consume バッファは毎フレーム CopyStructureCount が要り、それで
//   コマンドキューが詰まる（パーティクルシステムで実測 ~0.076ms）。
//   1 フレームのリクエストは少ないので、走査のほうが安い。
//
// 全スレッドがスロット 0 を取り合わないよう、走査の開始位置を
// スレッドごとにずらす。
//
// 飛び方: リクエストは動きテーブルの番号（+ 左右反転ビット）を持つ。
// 曲線系のモードでは飛行経路を「ここで」一度だけ作る。杖先から
// プレイヤーに一番近い敵まで -- 武器が狙ったのと同じ敵
// （counters にはまだ前ステップのキーが残っている）。
// 標的がなければ弾はただまっすぐ飛ぶ。
//
// DROP（メテオ）: 同じ敵で着弾点を決める。弾は空中の開始点
// （c1、SwarmCommon.hlsli 参照）まで持ち上げ、地面に届くだけの寿命を
// 与える。標的がなければ杖先の 8m 先、
// プレイヤーの高さに落ちる。
// SWARM_SPAWN_AT_POS 付き（連鎖したメテオ）はリクエストの位置が
// そのまま着弾点（ファイアボール / ストーンショットが消えた所）で、
// プレイヤー側から飛んでくる。
// 洞窟の中（2026-10-03: 着弾点の地面が平原より低く、洞窟には
// 天井がある）では、着弾点の真上、天井の下から落とす。
// プレイヤー側から飛ばすと、洞窟の外から始まって
// 岩盤を突き抜けることがある。
//
// 確保したスロットには必ず連鎖タグ（0 = なし）を projTags に書くので、
// 再利用したスロットに古いタグが残らない。projBoost（残す範囲の威力 /
// 持続の倍率、SwarmBoostDamage 参照）も同じ。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmProjectile> spawnRequests : register(t0);
StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
Buffer<uint> enemyStates : register(t3);
StructuredBuffer<uint2> spawnExtra : register(t4);   // x = 連鎖タグ、y = SWARM_SPAWN_* フラグ
StructuredBuffer<float> terrainHeight : register(t5); // GridWorld::Heights（洞窟の判定用）
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t6); // FanTarget は狙えない敵を飛ばす

// 地面がこれ（m）より低い = 洞窟の床、天井の下
static const float kCaveFloorBelow = -1.0;
// そこでの落下の高さ / 後ろへのずらし（天井の下面は床から ~15m）
static const float kCaveDropHeight = 8.0;
static const float kCaveDropBack = 2.0;

RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWStructuredBuffer<SwarmProjPath> paths : register(u2);
RWByteAddressBuffer counters : register(u3);
RWBuffer<uint> projTags : register(u4);
RWBuffer<uint> projBoost : register(u5);   // y >> SWARM_SPAWN_BOOST_SHIFT: 残す範囲の威力 / 持続の倍率
// スロットのリボン（SwarmProjTrailCS、2026-10-25）: フレーム最後のステップで HitCS に消された弾は、トレイルのパスが
// その死を見る前にここで再利用される。以前は新しい弾のリボンが古い弾の点から始まっていた
RWStructuredBuffer<uint> trailState : register(u6);
RWStructuredBuffer<float> trailAge : register(u7);

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;
    uint g_ScanStart;
    uint2 _spawnPad;
};

// 曲線が両端の間のどこかで地面（+ 余裕）の下に潜るか。
// プロファイルが使う緩い曲線（c.y <= 射程の ~0.45）なら 4 点で足りる
bool PathHitsGround(SwarmProjPath p)
{
    [unroll]
    for (int i = 1; i <= 4; ++i)
    {
        float3 q = SwarmBezier(p, (float)i * 0.2);
        if (q.y < SwarmTerrainHeight(terrainHeight, q.xz) + 0.4)
            return true;
    }
    return false;
}

// 出口魔法の扇の脇の弾はそれぞれ自分の敵を選ぶ（2026-10-31）ので、運ぶ魔法が 1 発目とは
// 別の所から始まる。この弾の方向まわりで一番近い敵（1 発目が取ったのとは別）。
// 狙いの反対側にいる敵は距離を 2 倍に数える。
// 該当する敵がいなければ 1 発目と同じ標的
static const float kFanRange = 20.0;
static const float kFanConeCos = 0.25;
uint FanTarget(uint first, float3 from, float3 dir)
{
    float2 aim = enemies[first].position.xz - from.xz;
    float2 shot = dir.xz;
    float side = aim.x * shot.y - aim.y * shot.x;
    shot /= max(length(shot), 1e-4);

    uint best = first;
    float bestScore = 1e9;
    [loop]
    for (uint j = 0; j < g_MaxEnemies; ++j)
    {
        if (j == first || enemyStates[j] == SWARM_DEAD || SwarmUntargetable(enemyExtra[j]))
            continue;
        float2 to = enemies[j].position.xz - from.xz;
        float d = length(to);
        if (d < 1.0 || d > kFanRange)
            continue;
        to /= d;
        float c = dot(to, shot);
        if (c < kFanConeCos)
            continue;
        float score = d / c;
        if ((aim.x * to.y - aim.y * to.x) * side < 0.0)
            score *= 2.0;
        if (score < bestScore)
        {
            bestScore = score;
            best = j;
        }
    }
    return best;
}

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmProjectile req = spawnRequests[id.x];
    uint2 extra = spawnExtra[id.x];

    // ---- 飛び方: リクエストのワードを分解してから経路を作る ----
    // sideSign は曲線の横ずれを飛行軸まわりに回す角度（ラジアン）:
    // 0 = 右、pi = 左（反転ビット）、それ以外 = 1 発ごとのランダムな方向
    // （ProjectileProfile Mirror::RandomAngle、extra.y の 7 ビット: 0..127 = 0..360 度。2026-10-06）
    float sideSign = ((req.motion & SWARM_MOTION_FLIP_BIT) != 0u) ? 3.14159265 : 0.0;
    sideSign += (float)((extra.y >> 1) & 0x7Fu) * (6.28318531 / 128.0);
    uint motionIdx = req.motion & SWARM_MOTION_INDEX_MASK;
    // リクエストのビット 16..19: 扇の何発目か（Swarm::kMotionFanShift。下でサイズ倍率に置き換わる）
    uint fanIndex = (req.motion >> 16u) & 0xFu;
    // 連鎖のルーン: リクエストの pathT に、この弾があと何体の敵へ跳べるかが入っている（2026-10-16）
    uint chains = (uint) clamp(req.pathT, 0.0, (float) SWARM_PROJ_CHAIN_LEFT_MASK);
    req.pathT = 0.0;

    SwarmMotion m = motions[motionIdx];
    // 貫通（2026-10-28、黄金の矢）: あと何体の敵を貫けるか。連鎖の跳躍もある場合は
    // 貫通数を跳躍数に足し、弾は跳ぶ（連鎖の挙動）
    uint pierce = SwarmMotionPierce(m);
    // 溜め: 飛ぶ前にこの時間だけ詠唱者の前で止まる（pathT < 0 = 残り秒。2026-10-16）。
    // 溜める弾は体の中ではなく、狙いの方向へ SWARM_HOLD_FORWARD m 先に生まれる
    float hold = SwarmMotionHold(m);
    req.pathT = -hold;
    if (hold > 0.0)
    {
        float aimLenSq = dot(req.velocity, req.velocity);
        if (aimLenSq > 1e-8)
            req.position += req.velocity * (rsqrt(aimLenSq) * SWARM_HOLD_FORWARD);
    }

    // サイズ倍率（拡大鏡アイテム）: プロファイルに対するこの弾の当たり半径の比。
    // 以降のどのシェーダーからも見えるよう motion のワードに入れておく
    float sizeScale = (m.baseRadius > 0.0) ? req.radius / m.baseRadius : 1.0;
    req.motion = motionIdx | (SwarmPackScale(sizeScale, 0x7FFFu) << SWARM_SCALE_SHIFT);

    float speed = length(req.velocity);

    SwarmProjPath path = (SwarmProjPath) 0;
    path.target = SWARM_NO_TARGET;
    path.sideSign = sideSign;
    path.speed = speed;
    path.duration = 1.0;

    if (SwarmMotionLands(m.mode) && speed > 0.01)
    {
        float3 fwd = float3(req.velocity.x, 0.0, req.velocity.z);
        float fwdLenSq = dot(fwd, fwd);
        fwd = (fwdLenSq > 1e-8) ? fwd * rsqrt(fwdLenSq) : float3(0, 0, 1);

        float3 impact = float3(req.position.x, g_PlayerPos.y, req.position.z) + fwd * 8.0;
        // 開始点: 上へ、そして杖先の側へ戻す（プレイヤーの肩越しに飛んでくる）
        float3 backFrom = req.position;
        if ((extra.y & SWARM_SPAWN_AT_POS) != 0u)
        {
            // 連鎖: 着弾点は CPU が決めてある
            impact = req.position;
            backFrom = g_PlayerPos;
        }
        else
        {
            uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
            if (key != SWARM_NO_TARGET_KEY)
            {
                uint slot = key & SWARM_SLOT_MASK;
                if (slot < g_MaxEnemies && enemyStates[slot] != SWARM_DEAD)
                {
                    impact = enemies[slot].position;
                    path.target = slot;
                }
            }
        }

        if (m.mode == SWARM_MOTION_LOB)
        {
            // 杖先から投げる（CPU が場所を決めたときはプレイヤーから）
            float3 from = ((extra.y & SWARM_SPAWN_AT_POS) != 0u) ? g_PlayerPos : req.position;
            SwarmBuildPath(path, m, from, impact, fwd, false);
            float3 tan0 = SwarmBezierTangent(path, 0.0);
            float tanLenSq = dot(tan0, tan0);
            req.position = from;
            req.velocity = (tanLenSq > 1e-8) ? tan0 * (rsqrt(tanLenSq) * speed) : fwd * speed;
        }
        else
        {
            float3 back = float3(backFrom.x - impact.x, 0.0, backFrom.z - impact.z);
            float backLenSq = dot(back, back);
            back = (backLenSq > 1e-8) ? back * rsqrt(backLenSq) : -fwd;
            bool inCave = SwarmTerrainHeight(terrainHeight, impact.xz) < kCaveFloorBelow;
            float backDist = inCave ? min(m.c1.y, kCaveDropBack) : m.c1.y;
            float upDist = inCave ? min(max(m.c1.x, 1.0), kCaveDropHeight) : max(m.c1.x, 1.0);
            float3 start = impact + back * backDist + float3(0.0, upDist, 0.0);

            SwarmBuildDropPath(path, start, impact);
            req.position = start;
            req.velocity = normalize(impact - start) * (speed * 0.4);
        }
        req.lifetime = max(req.lifetime, path.duration + 0.25);
    }
    else if (m.mode != SWARM_MOTION_STRAIGHT && m.mode != SWARM_MOTION_ORBIT && speed > 0.01)
    {
        uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
        if (key != SWARM_NO_TARGET_KEY)
        {
            uint slot = key & SWARM_SLOT_MASK;
            if (slot < g_MaxEnemies && enemyStates[slot] != SWARM_DEAD)
            {
                if (fanIndex > 0u)
                    slot = FanTarget(slot, req.position, req.velocity / speed);
                path.target = slot;
                SwarmBuildPath(path, m, req.position, enemies[slot].position,
                               req.velocity / speed, false);

                // ランダムに回した曲線は地面に潜ることがある（2026-10-06、ユーザー: 撃つ前に
                // 確認して）。曲線を何点か調べ、地形の下に潜るなら横ずれを反対向き（下 -> 上）に
                // 回す。それでも当たるなら水平な横向きに戻す
                if (PathHitsGround(path))
                {
                    path.sideSign += 3.14159265;
                    SwarmBuildPath(path, m, req.position, enemies[slot].position, req.velocity / speed, false);
                    if (PathHitsGround(path))
                    {
                        path.sideSign = 0.0;
                        SwarmBuildPath(path, m, req.position, enemies[slot].position, req.velocity / speed, false);
                    }
                }

                // 狙いの直線ではなく、曲線に沿って杖先を出る
                float3 tan0 = SwarmBezierTangent(path, 0.0);
                float tanLenSq = dot(tan0, tan0);
                if (tanLenSq > 1e-8)
                    req.velocity = tan0 * (rsqrt(tanLenSq) * speed);
            }
        }
    }

    // スレッドごとに別の範囲を取るよう、開始位置をばらけさせる
    uint start = (g_ScanStart + id.x * 97u) % g_MaxProjectiles;

    for (uint i = 0; i < g_MaxProjectiles; ++i)
    {
        uint slot = (start + i) % g_MaxProjectiles;

        uint prev;
        InterlockedCompareExchange(projStates[slot],
                                   SWARM_DEAD, SWARM_ALIVE, prev);

        if (prev == SWARM_DEAD)
        {
            // スロットを確保した。状態は交換でもう ALIVE になっている
            projectiles[slot] = req;
            paths[slot] = path;
            projTags[slot] = extra.x;
            projBoost[slot] = extra.y >> SWARM_SPAWN_BOOST_SHIFT;
            trailState[slot] = 0u;   // 新しいリボン（古い弾の点は捨てる）
            trailAge[slot] = 0.0;
            // 連鎖のルーン: 「生きていて連鎖中」の状態（HitCS / ProjMoveCS が読む。他からは「DEAD ではない」に見える）
            if (chains > 0u)
                projStates[slot] = SwarmProjChainState(chains + pierce, SWARM_PROJ_CHAIN_NO_LAST);
            else if (pierce > 0u)
                projStates[slot] = SwarmProjChainState(pierce, SWARM_PROJ_CHAIN_NO_LAST) | SWARM_PROJ_PIERCE;
            return;
        }
    }

    // プールが満杯: 捨てる。パーティクルシステムの deadCount ガードと同じ方針
    // -- 質は落としても、壊しはしない
}
