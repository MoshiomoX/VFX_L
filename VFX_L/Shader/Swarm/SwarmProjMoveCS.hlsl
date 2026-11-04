// ============================================================
// SwarmProjMoveCS.hlsl
// 弾の積分の固定ステップ 1 回分。
//
// ここでの g_Step は常に同じ値。それが固定ステップの目的で、
// 結果がフレームレートに左右されない。
//
// 動き方は 3 種類（SwarmMotion.mode）：
//   STRAIGHT   : position += velocity * step。敵は一切見ない。
//   CURVE_ONCE : 生成時にロックした敵へ向けて組んだベジェに沿う。
//                終点はその敵が生きている間追いかける。
//                敵が死ぬ / カーブが終わる -> 今の速度のまま
//                まっすぐ飛び続ける。二度とロックしない。
//   TRACK      : 同じカーブだが、標的がいなくなると「自分」に一番近い
//                敵を探し、今いる所から今の向きで出ていく
//                新しいカーブを組む。
//
//   DROP       : 生成時に組んだ直線の経路に沿って、その時決めた着弾点へ
//                落ちる（メテオ）。下の分岐を参照。
//
// velocity には常にこのステップの本当の速度が入る（カーブでもそうでなくても）：
// EmitCS はこれに沿ってパーティクルを後ろへ散らし、トレイルもこれを読む。
// ============================================================
// 寿命切れ / 壁に当たった弾は範囲を残すことがある
#define SWARM_AREA_POOL_U u4
#define SWARM_AREA_STATE_U u5
#define SWARM_AREA_DEF_T t4
// そして飛んでいた向きを覚える（毒瓶の水たまりはその向きへ飛び散る）
#define SWARM_AREA_DIR_U u6
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<uint> terrain : register(t0);
StructuredBuffer<SwarmMotion> motions : register(t1);
StructuredBuffer<SwarmEnemy> enemies : register(t2);
Buffer<uint> enemyStates : register(t3);
StructuredBuffer<float> terrainHeight : register(t5);   // 坂（t4 = 範囲の定義）
Buffer<uint> projBoost : register(t6);   // この弾が残す範囲の威力 / 持続の倍率
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t7); // 種類：潜っている砂潜りは標的にしない（2026-10-17）
Buffer<uint> projTags : register(t8);   // 連鎖タグ。最上位ビット = 吸血の弾：残す範囲も当てた相手に印を付ける（2026-10-25）
uint SwarmStealExtra(uint i)
{
    return ((projTags[i] & SWARM_TAG_LIFESTEAL) != 0u) ? SWARM_AREA_EXTRA_LIFESTEAL : 0u;
}

RWStructuredBuffer<SwarmProjectile> projectiles : register(u0);
RWBuffer<uint> projStates : register(u1);
RWByteAddressBuffer counters : register(u2);
RWStructuredBuffer<SwarmProjPath> paths : register(u3);

// 一斉起爆（2026-11-01）：1 ステップだけ != 0。飛んでいる弾はすべて消えてこの範囲を残す
cbuffer ProjDetonateCB : register(b1)
{
    uint g_DetonateArea;
    uint3 _detonatePad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    if (projStates[i] == SWARM_DEAD)
        return;

    SwarmProjectile p = projectiles[i];

    SwarmMotion m = motions[p.motion & SWARM_MOTION_INDEX_MASK];
    bool areaOnExpire = (m.hitArea != 0u) && ((m.hitAreaFlags & SWARM_HITAREA_ON_EXPIRE) != 0u);

    // ---- 起爆：落下中のメテオ / 投げた瓶は対象外（爆発が空中に浮いてしまう）。プレイヤーの近くにまだいる弾も
    // 対象外：遅い機雷が詠唱者の目の前で弾けて無駄になる ----
    float3 fromPlayer = p.position - g_PlayerPos;
    if (g_DetonateArea != 0u && !SwarmMotionLands(m.mode) && dot(fromPlayer, fromPlayer) > 9.0)
    {
        SwarmSpawnAreaFromDefX(g_DetonateArea, p.position, i, SwarmProjScale(p), projBoost[i], p.velocity.xz, SwarmStealExtra(i));
        projStates[i] = SWARM_DEAD;
        return;
    }

    // ---- 寿命 ----
    p.lifetime -= g_Step;
    if (p.lifetime <= 0.0)
    {
        if (areaOnExpire)
            SwarmSpawnAreaFromDefX(m.hitArea, p.position, i, SwarmProjScale(p), projBoost[i], p.velocity.xz, SwarmStealExtra(i));
        projStates[i] = SWARM_DEAD;
        return;
    }

    // ---- 停止：生成された場所に浮いたまま（pathT < 0 = 残り秒。2026-10-16） ----
    if (p.pathT < 0.0)
    {
        p.pathT += g_Step;
        if (p.pathT < 0.0)
        {
            // velocity は狙った向きのまま残す（直線の弾は停止の後にそれを使い、
            // 本体のパーティクルはその向きで標的の方を向く。EmitCS が小さくする）
            projectiles[i].lifetime = p.lifetime;
            projectiles[i].pathT = p.pathT;
            uint holdCount;
            counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, holdCount);
            return;
        }
        p.pathT = 0.0;
        // 生成時の速度は経路（カーブ）か弾本体（直線）に残してある
    }
    bool easeIn = (m.hitAreaFlags & SWARM_HITAREA_EASE_IN) != 0u;

    // ---- 周回（2026-11-01）：半径まで飛び出し、その後プレイヤーの周りを回る。地形判定なし：プレイヤーについて回る ----
    if (m.mode == SWARM_MOTION_ORBIT)
    {
        p.pathT += g_Step;
        float orbitAngle = paths[i].sideSign + p.pathT * m.c1.x;
        float orbitRadius = min(p.pathT * 14.0, m.c1.y);
        float3 orbitPos = g_PlayerPos + float3(cos(orbitAngle) * orbitRadius, m.c1.z, sin(orbitAngle) * orbitRadius);
        projectiles[i].velocity = (orbitPos - p.position) / g_Step;
        projectiles[i].position = orbitPos;
        projectiles[i].lifetime = p.lifetime;
        projectiles[i].pathT = p.pathT;
        uint orbitCount;
        counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, orbitCount);
        return;
    }

    // ---- 連鎖のルーン（2026-10-16）：前のステップで HitCS が敵に当て、弾を生かしたまま残した。
    // 届く範囲で一番近い「別の」生きている敵に狙いを付け、ここから今の向きで出ていく
    // 新しいカーブを組む。届く範囲に誰もいなければ、手放してそのまま飛ぶ
    // （TRACK の弾は自分でまた探す。HitCS は最後に当てた敵を飛ばし続ける） ----
    uint state = projStates[i];
    // 貫通する弾（黄金の矢、2026-10-28）は同じ状態を使うが、狙い直さない：直線のまま進む
    bool chained = (state & SWARM_PROJ_CHAINED) != 0u && (state & SWARM_PROJ_PIERCE) == 0u;
    if ((state & SWARM_PROJ_CHAIN_PENDING) != 0u)
    {
        projStates[i] = state & ~SWARM_PROJ_CHAIN_PENDING;
        uint lastHit = state & SWARM_PROJ_CHAIN_LAST_MASK;
        float bestSq = SWARM_CHAIN_RADIUS * SWARM_CHAIN_RADIUS;
        uint best = SWARM_NO_TARGET;
        for (uint j = 0; j < g_MaxEnemies; ++j)
        {
            if (j == lastHit || enemyStates[j] == SWARM_DEAD || SwarmUntargetable(enemyExtra[j]))
                continue;
            float3 d = enemies[j].position - p.position;
            float dSq = dot(d, d);
            if (dSq < bestSq)
            {
                bestSq = dSq;
                best = j;
            }
        }
        SwarmProjPath chainPath = paths[i];
        if (best != SWARM_NO_TARGET)
        {
            float vLenSq = dot(p.velocity, p.velocity);
            float3 heading = (vLenSq > 1e-8) ? p.velocity * rsqrt(vLenSq) : float3(0, 0, 1);
            chainPath.target = best;
            if (chainPath.speed <= 0.01)
                chainPath.speed = max(sqrt(vLenSq), 1.0);
            SwarmBuildPath(chainPath, m, p.position, enemies[best].position, heading, true);
            p.pathT = 0.0;
            p.lifetime = max(p.lifetime, chainPath.duration + 0.25);
        }
        else
        {
            chainPath.target = SWARM_NO_TARGET;
        }
        paths[i] = chainPath;
    }

    // ---- DROP / LOB：生成時に決めた着弾点へ落ちる ----
    // 途中で敵には当たらず（HitCS は DROP を飛ばす）、地形判定もしない：
    // 通行判定は 2D なので、木や台地の上を飛ぶと壁に当たったことに
    // なってしまう。必ず p3 に着地して、そこに範囲を残す。
    if (SwarmMotionLands(m.mode))
    {
        SwarmProjPath drop = paths[i];
        p.pathT += g_Step / drop.duration;
        float3 dropPos = SwarmBezier(drop, min(p.pathT, 1.0));
        p.velocity = (dropPos - p.position) / g_Step;
        p.position = dropPos;

        if (p.pathT >= 1.0)
        {
            if (m.hitArea != 0u)
                SwarmSpawnAreaFromDefX(m.hitArea, drop.p3, i, SwarmProjScale(p), projBoost[i], p.velocity.xz, SwarmStealExtra(i));   // 弧の最後の向き = 投げた向き
            projStates[i] = SWARM_DEAD;
            return;
        }

        projectiles[i].position = p.position;
        projectiles[i].velocity = p.velocity;
        projectiles[i].lifetime = p.lifetime;
        projectiles[i].pathT = p.pathT;
        uint dropCount;
        counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, dropCount);
        return;
    }

    bool onCurve = false;

    // 連鎖中の直線の弾（黄金の矢 + 連鎖のルーン）は連鎖が組んだカーブに沿う
    if (m.mode != SWARM_MOTION_STRAIGHT || chained)
    {
        SwarmProjPath path = paths[i];
        bool pathDirty = false;

        // ---- 標的を見失ったか ----
        if (path.target != SWARM_NO_TARGET)
        {
            if (path.target >= g_MaxEnemies || enemyStates[path.target] == SWARM_DEAD || SwarmUntargetable(enemyExtra[path.target]))
            {
                path.target = SWARM_NO_TARGET;
                pathDirty = true;

                // 向きはそのまま、速さを公称値に戻す
                float lostLenSq = dot(p.velocity, p.velocity);
                if (lostLenSq > 1e-8)
                    p.velocity *= rsqrt(lostLenSq) * path.speed;
            }
        }

        // ---- TRACK：新しい標的を探す。弾に一番近いもの ----
        if (path.target == SWARM_NO_TARGET && m.mode == SWARM_MOTION_TRACK)
        {
            float bestSq = (m.retargetRadius > 0.0)
                         ? m.retargetRadius * m.retargetRadius : 1e30;
            uint best = SWARM_NO_TARGET;

            for (uint j = 0; j < g_MaxEnemies; ++j)
            {
                if (enemyStates[j] == SWARM_DEAD || SwarmUntargetable(enemyExtra[j]))
                    continue;
                float3 d = enemies[j].position - p.position;
                float dSq = dot(d, d);
                if (dSq < bestSq)
                {
                    bestSq = dSq;
                    best = j;
                }
            }

            if (best != SWARM_NO_TARGET)
            {
                float vLenSq = dot(p.velocity, p.velocity);
                float3 heading = (vLenSq > 1e-8) ? p.velocity * rsqrt(vLenSq) : float3(0, 0, 1);

                path.target = best;
                SwarmBuildPath(path, m, p.position, enemies[best].position, heading, true);
                p.pathT = 0.0;
                pathDirty = true;
            }
        }

        // ---- カーブに沿う ----
        if (path.target != SWARM_NO_TARGET)
        {
            // 終点は標的に乗せる。2 つ目のハンドルも一緒に動かし、
            // 標的が動いても到着の形が崩れないようにする
            float3 newEnd = enemies[path.target].position;
            path.p2 += newEnd - path.p3;
            path.p3 = newEnd;

            p.pathT += g_Step / path.duration;
            float t = min(p.pathT, 1.0);
            if (easeIn)
                t = t * t; // 止まった状態から始まり、公称速度の 2 倍で着く

            float3 newPos = SwarmBezier(path, t);
            p.velocity = (newPos - p.position) / g_Step;
            p.position = newPos;
            onCurve = true;
            pathDirty = true;

            // 当たらずにカーブが終わった：手放して公称速度で
            // まっすぐ飛ぶ。TRACK は次のステップでまた探す
            if (p.pathT >= 1.0)
            {
                float3 tan1 = SwarmBezierTangent(path, 1.0);
                float tanLenSq = dot(tan1, tan1);
                if (tanLenSq > 1e-8)
                    p.velocity = tan1 * (rsqrt(tanLenSq) * path.speed);
                path.target = SWARM_NO_TARGET;
            }
        }

        if (pathDirty)
            paths[i] = path;
    }

    // ---- 直進 ----
    // 重力なし：弾はまっすぐ飛ぶ（CPU 版と同じ）
    if (!onCurve)
        p.position += p.velocity * g_Step;

    // ---- 地形 ----
    // 塞がったセルに入ると消える。グリッドの精度は 2m だが、
    // 壁もどうせ 2m の厚さがあるので、すり抜けはしない。
    // 坂は高さを持つ通行可能なセル：その面より下を飛ぶのも
    // 当たり扱い（弾がブロックの中に入ってしまうので）
    // 塞がったセルが止めるのは、空いたセルからその中へ飛び込んだ弾だけ。
    // 中で生まれた弾は、そこを出るまで飛び続ける：プレイヤーは
    // 岩 / 箱の上に立てる（そのセルは雑魚にとっては塞がっている）ので、
    // 以前はそこから詠唱した弾がすべて最初のステップで消えていた
    // （2026-10-07）。壁には空いたセルから入るので、壁は今まで通り弾を止める
    bool wasFree = SwarmIsWalkable(terrain, projectiles[i].position);
    if ((wasFree && !SwarmIsWalkable(terrain, p.position))
        || p.position.y < SwarmTerrainHeight(terrainHeight, p.position.xz))
    {
        // 壁の中ではなく、最後に空いていた位置で弾ける
        if (areaOnExpire)
            SwarmSpawnAreaFromDefX(m.hitArea, projectiles[i].position, i, SwarmProjScale(p), projBoost[i], p.velocity.xz, SwarmStealExtra(i));
        projStates[i] = SWARM_DEAD;
        return;
    }

    projectiles[i].position = p.position;
    projectiles[i].velocity = p.velocity;
    projectiles[i].lifetime = p.lifetime;
    projectiles[i].pathT = p.pathT;
    uint prevCount;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_PROJ, 1u, prevCount);
}
