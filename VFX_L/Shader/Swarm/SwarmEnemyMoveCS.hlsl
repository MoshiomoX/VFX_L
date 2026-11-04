// ============================================================
// SwarmEnemyMoveCS.hlsl
// 敵の積分の固定ステップ 1 回分。SwarmEnemyAICS の後に走るので、
// どの位置が動く前にも速度は確定している。
//
// y は g_GroundY + 地形の高さに貼り付く。ただし崖から踏み出した後
// （下にいるプレイヤーへ向かう時は許可、2026-10-01）は、着地するまで
// kFallGravity で落ちる（velocity.y。書くのはここだけ）。
// 高さ場（GridWorld::Heights、グリッドのセル 1 つに SWARM_HEIGHT_SUB 個）は
// 平地で 0、坂（台形のブロック）では歩ける面の高さなので、敵はそれに
// 沿って坂を登る。箱と壁はそこが 0 のままで、代わりに通行グリッドで
// 塞ぐ。
// yaw はラジアンで持つ（VS がそのまま sin/cos する）。
//
// リードバック用に生きている敵の数も数える。
// ============================================================
#define SWARM_BOMBER_CB_REG b3   // 種類ごとの体格（エリート / ボスは壁との間隔を大きく取る）
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<float> terrainHeight : register(t1);
StructuredBuffer<uint> terrain : register(t2); // 通行グリッド（壁沿いの滑り / 壁からの脱出用）
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3); // 種類: 幽霊は壁を無視する
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);
// ノックバック（突風、2026-10-12）: xy = プレイヤーから離れる向きの速度、z = 残り秒。
// AreaDamageCS が書く。ここでこのステップの移動に足し（AI の速度に足すと、
// その平滑化で 0.25 秒の一押しが数 m に伸びてしまう）、減衰させる
RWStructuredBuffer<float4> enemyPush : register(u2);
// トゲ玉の向き（四元数、2026-10-25）: 毎ステップの転がりをここで掛け合わせ、SwarmSpikeBallVS がそれで
// 描く。0 = 一度も転がっていない（SwarmSpikeBallRoll がスロットの静止姿勢を返す）。生成時にリセットしない: 玉の
// 向きに意味は無いので、使い回されたスロットは前の玉が止まった姿勢から始めるだけ
RWStructuredBuffer<float4> enemyRoll : register(u3);
// 単純に一番近い敵（2026-10-27）: SWARM_CNT_NEAREST_KEY と同じ競争を実距離で行い、ボス / エリートの
// 優先度は付けない。+0 キー（AimResolveCS が読んで SWARM_NO_TARGET_KEY に戻す）、+4 位置、+16 距離。射程の短い魔法
// （突風、アイススパイクの線）はこれで狙う。杖は優先度付きの方で狙う（SwarmCounters は満杯）
RWByteAddressBuffer closeTarget : register(u4);
// 壁登り（2026-10-29、ユーザー:「どの敵も壁を登れるようにしないと、誰も届かない場所ができる」）。
// x = 登っている秒数（0 = 登っていない。< 0 = 上面に乗り移る間の -（あと歩く m）、2026-11-02）、
// y = 登る先の上面の高さ（地形空間）、zw = 壁面へ向かう向き。
// 書くのはこのパスだけ（PushCS が読む: 登っている体は押さない）。スロットが死んでいる間はクリア
RWStructuredBuffer<float4> enemyClimb : register(u5);

// プレイヤーからこの m 以内の敵をスマートカメラ用に数える（FollowCamera::SetSituation）
static const float kCrowdRadius = 10.0;

// リードバック用に敵を数え、「プレイヤーに一番近い敵」の競争に参加させる
// （XZ 距離のみ。このパイプラインの他の所と同じ）
void CountAlive(uint i, float3 pos)
{
    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ENEMIES, 1u, prev);
    float3 dp = pos - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);
    // キーは SwarmAimKeyDist で並ぶ（雑魚が近くにいなければボス / エリートが先、2026-10-26）。AimResolveCS が
    // 勝者の実距離を CPU 用に書き戻す
    float keyDist = SwarmAimKeyDist(enemyExtra[i].kind, dist);
    uint key = (asuint(keyDist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK);
    if (!SwarmUntargetable(enemyExtra[i]))   // 砂に潜った砂潜り / 砂旋は標的にしない（2026-10-17）
    {
        counters.InterlockedMin(SWARM_CNT_NEAREST_KEY, key, prev);
        closeTarget.InterlockedMin(0, (asuint(dist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK), prev);
        // プレイヤー周りの混み具合（スマートカメラが引く、2026-11-02）: +20 で数え、AimResolveCS が
        // リードバック用に +24 へ移し、次のステップ用に +20 をクリアする
        if (dist < kCrowdRadius)
            closeTarget.InterlockedAdd(20, 1u, prev);
    }
}

// 壁 / 崖面に触れた体をゆっくり押し戻す速さ（m/s）
static const float kWallPushOut = 2.0;
static const float kWallPushOutFalling = 4.0;

// 台地から落ちる（2026-10-01）: m/s^2、戦闘シーンの重力と同じ
static const float kFallGravity = 25.0;
// 1 ステップで地面がこれだけ下がっても敵は地面に貼り付いたまま
// （坂を下ると 1 ステップ約 0.05m 下がる。崖の縁から踏み出すと双線形のぼかしで
// 1 ステップ約 0.5m 下がるので、そちらは落下を始める）
static const float kSnapDown = 0.25;

// from -> to の 1 ステップ: 中心が途中に崖の無い歩けるセルに着き、
// （bodyCheck）そこで体の円が何にも触れない。
// 壁面から抜け出す時（!bodyCheck）は崖の判定に生の高さを使う
// （SwarmRawSlopeOk: 双線形の方だと離れる一歩を許さない）。
// allowDrop（プレイヤーが下にいる）: 下りの崖は通し、体の下の低い地面は
// 数えないので、敵は縁から歩いて落ちる。
// footH = 体の高まりを測る基準になる足の高さ（地形空間）
bool StepOk(float2 from, float2 to, float radius, bool bodyCheck, bool allowDrop, float footH)
{
    bool ok = SwarmIsWalkable(terrain, float3(to.x, 0.0, to.y));
    if (ok)
        ok = bodyCheck ? (SwarmStepHeightOk(terrainHeight, from, to, allowDrop) || SwarmRawStepOk(terrainHeight, from, to, allowDrop))
                       : SwarmRawSlopeOk(terrainHeight, from, to);
    if (ok && bodyCheck)
        ok = (SwarmBodyContact(terrain, terrainHeight, to, radius, !allowDrop, footH).z == 0.0);
    return ok;
}

// 角を回り込む横移動: 体の判定付きの StepOk だが、中心の坂は生の高さで見る。
// 壁面の隣では双線形の地面がぼけて持ち上がる（壁面の手前 0.25m）ので、
// 壁面から離れる一歩が SwarmStepHeightOk には急な下りに見え、横移動が
// 拒否されていた（soak 2026-10-03: 坂の側面の横で敵が動けなくなった）
bool SideStepOk(float2 from, float2 to, float radius, bool allowDrop, float footH)
{
    return SwarmIsWalkable(terrain, float3(to.x, 0.0, to.y))
        && SwarmRawSlopeOk(terrainHeight, from, to)
        && SwarmBodyContact(terrain, terrainHeight, to, radius, !allowDrop, footH).z == 0.0;
}

// 体の円の下の崖: 生の高さセルの最高と最低がこれだけ違う
static const float kEdgeCliff = 0.8;

// ---- 壁登り（2026-10-29） ----
// 雑魚用の地形（SwarmSystemClimb.cpp）はフィールド内の障害物（岩、木の幹、遺跡の壁、柱）の上面を高さとして持たせ、
// AI が登りたい壁面へ押し込むのを許す（ProbeBlocked の allowClimb）。その後の下のステップがほとんど動かず、
// 体の段差の余裕より高い壁面が前にそびえていたら、敵は登り始める:
// 壁面に張り付いたまま y だけが g_ClimbSpeed で上がり、上に着いたら乗り移る。手を離して（Settle で落ちる）のは、
// その向きに行きたくなくなった時（フローフィールドが向きを変えた / 凍った）、押された時、壁面がもう
// 真正面に無い時（群れに動かされた）、kClimbMaxTime 登り続けた時
static const float kClimbMinRise = SWARM_BODY_CLIFF_RISE;   // これより低い段差は普通に歩いて越える
static const float kClimbAhead = 0.3;                       // 体からこれだけ先で壁面を探す
static const float kClimbMaxTime = 10.0;

// 前方の壁面の上端（生の高さセル）: 進行方向と直角に並べた 3 つの探針の最高値。どれも歩けない時は -1e30
// （外周は登る壁面ではない）
float FaceTopAhead(float2 from, float2 dir, float radius)
{
    float2 side = float2(-dir.y, dir.x) * (radius * 0.6);
    float2 p = from + dir * (radius + kClimbAhead);
    float top = -1e30;
    [unroll]
    for (int k = -1; k <= 1; ++k)
    {
        float2 q = p + side * (float) k;
        if (SwarmIsWalkable(terrain, float3(q.x, 0.0, q.y)))
            top = max(top, SwarmTerrainHeightRaw(terrainHeight, q));
    }
    return top;
}

// y: 地面に貼り付くか、地面へ落ちる（velocity.y = 落下速度。書くのはこのパスだけ）。
// 縁から落ちる時（allowDrop）: 体の円の下で一番高い地面が敵を支えるので、
// 台地の縁の双線形のぼかしに沈み込むことも、体の半分がまだ台地の上にあるうちに
// 落ち始めることもない（以前は落ちる途中で壁面が体を
// 切り裂いていた）。円全体が縁を越えたら落ちる
void Settle(inout SwarmEnemy e, float radius, bool allowDrop)
{
    float ground = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);
    // 支えは落下が許されているかどうかに関係なく効かせる（2026-10-20、以前は allowDrop の時だけ）: 台地の縁に
    // 立った敵がそこの双線形のくぼみに沈み込んでいた（最大で壁面の半分）
    {
        float feet = e.position.y - g_GroundY;
        float hi = SwarmTerrainHeightRaw(terrainHeight, e.position.xz), lo = hi;
        [unroll]
        for (int k = 0; k < 8; ++k)
        {
            float a = (float) k * 0.7853982;
            float h = SwarmTerrainHeightRaw(terrainHeight, e.position.xz + float2(cos(a), sin(a)) * radius);
            hi = max(hi, h);
            lo = min(lo, h);
        }
        // まだ縁の上に立っている（足が高い側と同じ高さ、または中心自身の
        // 生のセルが高い側: 縁の双線形のくぼみに生成された / 押し込まれた時、足は
        // 台地より下から始まり、放っておくとそのままになる、2026-10-20）。壁面に
        // 触れているだけの崖下の体は除く
        float rawHere = SwarmTerrainHeightRaw(terrainHeight, e.position.xz);
        if (hi - lo > kEdgeCliff && (feet >= hi - kSnapDown || rawHere >= hi - SWARM_BODY_CLIFF_RISE))
            ground = max(ground, g_GroundY + hi);
    }
    if (e.position.y > ground + kSnapDown)
    {
        e.velocity.y -= kFallGravity * g_Step;
        e.position.y = max(e.position.y + e.velocity.y * g_Step, ground);
        if (e.position.y <= ground)
            e.velocity.y = 0.0;   // 着地
    }
    else
    {
        e.position.y = ground;
        e.velocity.y = 0.0;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
    {
        enemyClimb[i] = float4(0, 0, 0, 0);   // 使い回されたスロットが登っている状態で始まらないように
        return;
    }

    SwarmEnemy e = enemies[i];
    float4 climb = enemyClimb[i];
    // ---- 固定エリートの瞬間移動（影の剣士、2026-10-25）: CPU が指定した地点の地面へ、プレイヤーの方を向いて ----
    if (SwarmIsFixedElite(enemyExtra[i].kind))
    {
        int row = SwarmEliteEntry(i);
        if (row >= 0 && g_EliteCmd[row].z == SWARM_ELITE_CMD_TELEPORT)
        {
            float2 t = g_EliteMove[row].xy;
            float2 toP = g_PlayerPos.xz - t;
            float3 tp = float3(t.x, SwarmTerrainHeight(terrainHeight, t) + g_GroundY, t.y);
            enemies[i].position = tp;
            enemies[i].velocity = float3(0, 0, 0);
            enemyClimb[i] = float4(0, 0, 0, 0);
            if (dot(toP, toP) > 1e-6)
                enemies[i].yaw = atan2(toP.x, toP.y);
            CountAlive(i, tp);   // このステップも生きている（2026-10-25: 以前は途中の return で数え漏れていた）
            return;
        }
    }
    float2 startXZ = e.position.xz;   // このステップの開始位置（トゲ玉は実際に動いた分だけ転がる）
    // 壁に対する体の半径と、下りの崖を通れるか（プレイヤーが下にいる）
    float radius = SwarmBodyWallRadius(enemyExtra[i].kind);
    bool allowDrop = SwarmDropAllowed(terrainHeight, e.position.xz, e.position.y - g_GroundY);

    // ノックバック（突風）: このステップの一押し。SWARM_PUSH_TIME かけて減衰する。速度に乗せて
    // 下の積分を通し（壁 / 崖の判定込み）、向きを決める前に取り除く
    float2 pushV = float2(0.0, 0.0);
    {
        float4 pk = enemyPush[i];
        if (pk.z > 0.0)
        {
            pushV = pk.xy * saturate(pk.z / SWARM_PUSH_TIME);
            pk.z -= g_Step;
            if (pk.z <= 0.0)
                pk = float4(0.0, 0.0, 0.0, 0.0);
            enemyPush[i] = pk;
        }
    }
    e.velocity.xz += pushV;

    if (e.animIndex == 2u && dot(pushV, pushV) < 1e-4)
    {
        // ---- 被弾の硬直: 位置と向きを保ち、時計だけ進める ----
        // HitCS が当てた時に animTime = 0 にしている
        e.animTime += g_Step;
        if (e.animTime >= g_HitStun)
        {
            e.animIndex = 0u;
            e.animTime = 0.0;
        }
        if (climb.x == 0.0)
            Settle(e, radius, allowDrop);   // 空中で被弾: 落ち続ける（登っている / 乗り移っている時に被弾: 壁面にぶら下がったまま）
    }
    else
    {
        // ---- 積分 ----
        // AI の硬い遮断はセル単位の先読みで、プレイヤーからの押し /
        // 分離がまだ速度を壁へ運ぶことがあるので、ステップそのものを
        // ここで確かめる（2026-09-30、以前は敵が木や壁の中に入り込んでいた）:
        // 塞がれたセルや崖の向こうで終わるステップは、空いている方の
        // 軸だけに減らし（壁沿いに滑る）、どちらも駄目なら
        // 捨てる。
        // 既に塞がれたセルの中にいる敵（押し込まれた、そこに生成された）は、
        // 代わりに一番近い空きセルへ歩かせ、途中の壁は無視する
        // （そこから逃げ出そうとしているのだから）。
        float2 from = e.position.xz;
        if (SwarmFliesThrough(enemyExtra[i]))   // 砂旋（2026-10-17）と飛んでいる石像ガーゴイル（2026-10-25）は幽霊のように飛ぶ
        {
            // 幽霊: まっすぐ飛び、壁を抜け、台地の上も越える（y は下の地面に沿ったまま）
            e.position.xz = from + e.velocity.xz * g_Step;
        }
        else if (climb.x < 0.0)
        {
            // ---- 上面への乗り移り（2026-11-02、ユーザー: 以前は体が 1 ステップで半径分前へ跳んでいた）:
            //      y は上面の高さのまま、中心が上面に立つまで（または予定の距離を使い切るまで）xz を
            //      登る速さで前へ歩かせる。その後は Settle の縁の支えで上に留まる ----
            float2 cdir = climb.zw;
            float remaining = -climb.x;
            float d = min(g_ClimbSpeed * g_Step, remaining);
            float2 next = from + cdir * d;
            remaining -= d;
            e.position.xz = next;
            e.position.y = g_GroundY + climb.y;
            e.velocity.y = 0.0;
            bool onTop = SwarmIsWalkable(terrain, float3(next.x, 0.0, next.y))
                      && SwarmTerrainHeightRaw(terrainHeight, next) >= climb.y - 0.3;
            climb = (remaining <= 1e-3 || onTop) ? float4(0, 0, 0, 0) : float4(-remaining, climb.y, cdir);
        }
        else if (climb.x > 0.0)
        {
            // ---- 壁を登る（2026-10-29）: 壁面に張り付き、y だけが上がる。上に着いたら乗り移る ----
            float2 cdir = climb.zw;
            float feet = e.position.y - g_GroundY;
            float top = FaceTopAhead(from, cdir, radius);
            bool hold = dot(e.velocity.xz - pushV, cdir) > 0.25 * max(e.moveSpeed, 0.5)   // まだその向きへ行きたい
                     && dot(pushV, pushV) < 1.0                                          // 押されていない
                     && climb.x < kClimbMaxTime
                     && top > feet - 0.05;                                               // 壁面がまだ真正面にある
            if (!hold)
                climb = float4(0, 0, 0, 0);   // 手を離す: 下の Settle で落ちる
            else
            {
                climb.y = top;
                feet = min(feet + g_ClimbSpeed * g_Step, top);
                climb.x += g_Step;
                e.position.y = g_GroundY + feet;
                e.velocity.y = 0.0;
                if (feet >= top - 1e-3)
                {
                    // 上に着いた: 一気に跳び移らず、次からのステップ（上の分岐）で歩いて乗り移る
                    float2 onto = from + cdir * (radius + kClimbAhead + 0.1);
                    if (SwarmIsWalkable(terrain, float3(onto.x, 0.0, onto.y)))
                        climb = float4(-(radius + kClimbAhead + 0.1), top, cdir);
                    else
                        climb = float4(0, 0, 0, 0);
                }
            }
        }
        else if (!SwarmIsWalkable(terrain, e.position))
        {
            float2 best = from;
            float bestD = 1e30;
            int gx = (int) floor((from.x - g_GridOrigin.x) / g_CellSize);
            int gz = (int) floor((from.y - g_GridOrigin.z) / g_CellSize);
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                {
                    int cx = gx + dx, cz = gz + dz;
                    if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                        continue;
                    if (terrain[cz * g_GridW + cx] == 0u)
                        continue;
                    float2 c = float2(g_GridOrigin.x + (cx + 0.5) * g_CellSize,
                                      g_GridOrigin.z + (cz + 0.5) * g_CellSize);
                    float d = dot(c - from, c - from);
                    if (d < bestD) { bestD = d; best = c; }
                }
            if (bestD < 1e29)
            {
                float2 dir = best - from;
                float len = length(dir);
                float stepLen = min(len, max(e.moveSpeed, 2.0) * g_Step);
                if (len > 1e-4)
                    e.position.xz = from + dir / len * stepLen;
            }
        }
        else
        {
            // 中心だけでなく体の円も壁や崖面から離しておく
            // （2026-10-01、SwarmBodyContact を参照）。既に触れている体
            // （群れに押し込まれた、そこに生成された）は、壁に沿う向きの
            // 速度は保ったまま kWallPushOut でゆっくり抜け出す。接触が
            // 打ち消し合う時（両側から挟まれた）は、以前どおり中心だけを
            // 判定する。
            // 高まりは足から測る: 地面の上 = 中心の下の生の地面（以前
            // どおり）。踏み出し / 落下中 = 実際の足（後ろの台地は足がそれより
            // 下へ沈むまで壁ではなく、沈んだら落ちる途中で体を壁面から離す）
            // 落下が許されている時だけ: 崖下では双線形の地面が足も
            // 持ち上げるので、下りを数えると低い側も触れてしまい、押し出しを
            // 打ち消して体が壁面に張り付く（自動テスト clip / climb）
            float rawHere = SwarmTerrainHeightRaw(terrainHeight, from);
            float feet = e.position.y - g_GroundY;
            float footH = allowDrop ? max(rawHere, feet) : rawHere;
            bool airborne = (feet > SwarmTerrainHeight(terrainHeight, from) + kSnapDown);   // 落下中（Settle）
            float3 touch = SwarmBodyContact(terrain, terrainHeight, from, radius, !allowDrop, footH);
            bool easeOut = (touch.z > 0.0 && dot(touch.xy, touch.xy) > 0.01);
            bool bodyCheck = (touch.z == 0.0);

            float2 v = e.velocity.xz;
            if (easeOut)
            {
                float2 outDir = -normalize(touch.xy);
                float into = -dot(v, outDir);   // > 0 = 壁へ向かう
                if (into > 0.0)
                    v += outDir * into;
                // 崖面の横を落ちている: 速く離す。でないと落ちる途中で壁面が体を切り裂く
                v += outDir * (airborne ? kWallPushOutFalling : kWallPushOut);
            }

            float2 next = from + v * g_Step;
            float2 nx = float2(next.x, from.y);
            float2 nz = float2(from.x, next.y);
            // 両方の軸にちゃんと速度がある（片方の軸にわずかしか無いと、その軸の滑りが進まないまま
            // 「成功」し、下の横移動が走らなかった: soak 2026-10-03）
            float sp = length(v);
            bool both = (abs(v.x) > 0.15 * sp) && (abs(v.y) > 0.15 * sp);
            // ---- 登り始め（2026-10-29）。滑り / 横移動より先に判定する（そちらだと壁面に沿って歩かせて
            //      しまう）: ステップが拒否され、AI が行きたい方向の真正面に、歩きで越えられる段差より高い
            //      壁面がそびえている。AI は登るつもりの壁面にしか速度を押し込ませない ----
            bool stepOk = StepOk(from, next, radius, bodyCheck, allowDrop, footH);
            bool startClimb = false;
            if (g_ClimbSpeed > 0.0 && !airborne && !stepOk)
            {
                float2 want = e.velocity.xz - pushV;
                float spv = length(want);
                if (spv > 0.3 * max(e.moveSpeed, 0.5))
                {
                    float2 cdir = want / spv;
                    float2 p = from + cdir * (radius + kClimbAhead);
                    float top = SwarmIsWalkable(terrain, float3(p.x, 0.0, p.y)) ? SwarmTerrainHeightRaw(terrainHeight, p) : -1e30;
                    if (top - feet > kClimbMinRise)
                    {
                        climb = float4(g_Step, top, cdir);
                        startClimb = true;
                    }
                }
            }
            if (startClimb)
            {
                // このステップはその場に留まる。次のステップから登りの分岐が引き継ぐ
            }
            else if (stepOk)
                e.position.xz = next;
            else if (both && StepOk(from, nx, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nx;
            else if (both && StepOk(from, nz, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nz;
            else if (bodyCheck)
            {
                // 斜め前の角（2026-10-03、soak テスト）: AI は各軸のまっすぐ前しか
                // 探らないので、斜め前に壁 / 台地の角があると押し続ける一方、ここの
                // 体の円は角にかかる -> ステップも両方の滑りも失敗し、敵は
                // ずっと立ち止まっていた。
                // まっすぐの道が空くまで、触れている点から
                // 離れる向きに、速度と直角に横移動する
                float3 tn = SwarmBodyContact(terrain, terrainHeight, next, radius, !allowDrop, footH);
                if (tn.z > 0.0 && dot(tn.xy, tn.xy) > 0.01 && sp > 1e-4)
                {
                    float2 dirV = v / sp;
                    float2 away = -normalize(tn.xy);
                    float2 side = away - dirV * dot(away, dirV);
                    if (dot(side, side) > 1e-4)
                    {
                        float2 ns = from + normalize(side) * sp * g_Step;
                        if (SideStepOk(from, ns, radius, allowDrop, footH))
                            e.position.xz = ns;
                    }
                    else
                    {
                        // 押し出しが速度のちょうど逆を向いている（進行方向の両側に触れている点が
                        // 2 つある。例: 高台の角が真正面）: どちらの横を選ぶ理由も無いので、
                        // 直角の両方向を試して先に空いていた方を取る（2026-10-20）
                        float2 perp = float2(-dirV.y, dirV.x) * sp * g_Step;
                        if (SideStepOk(from, from + perp, radius, allowDrop, footH))
                            e.position.xz = from + perp;
                        else if (SideStepOk(from, from - perp, radius, allowDrop, footH))
                            e.position.xz = from - perp;
                    }
                }
            }
        }
        if (SwarmFliesThrough(enemyExtra[i]))   // 砂旋（2026-10-17）と飛んでいる石像ガーゴイル（2026-10-25）は幽霊のように飛ぶ
        {
            // 幽霊は何の上にも浮かぶ: 下の地面に合わせ、落下はしない（ガーゴイルは飛ぶ高さに）
            e.position.y = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz) + SwarmBodyHover(enemyExtra[i]);
            e.velocity.y = 0.0;
        }
        else if (climb.x == 0.0)
            Settle(e, radius, allowDrop);   // 登り / 上面への乗り移りは y を自分で決める

        // ---- 向き: 速度の方へ上限付きの速さで向く（押された向きは自分で選んだ向きではない） ----
        e.velocity.xz -= pushV;
        // 溜め中に立っているボス（CPU の突進指示が速度 0）は指示の向きへ向き、浮かんでいる
        // 魔神は止まっている間プレイヤーの方を向く（2026-10-29: 以前は最後に歩いた向きのままだった）
        float2 faceDir = e.velocity.xz;
        if (enemyExtra[i].kind == SWARM_KIND_BOSS && dot(faceDir, faceDir) <= 0.01)
        {
            if (g_BossChargeOn > 0.5)
                faceDir = g_BossChargeDir;
            else if (g_BossFly > 0.5)
                faceDir = g_PlayerPos.xz - e.position.xz;
        }
        if (dot(faceDir, faceDir) > 0.01)
        {
            float targetYaw = atan2(faceDir.x, faceDir.y);
            float delta = targetYaw - e.yaw;
            delta = atan2(sin(delta), cos(delta)); // [-PI, PI] に巻き戻す
            float maxTurn = g_TurnSpeed * g_Step;
            e.yaw += clamp(delta, -maxTurn, maxTurn);
        }
        // ---- アニメーションの時計（敵の VS の歩行サイクル、盾 / 砂丘の上下、風源のフェードインが読む） ----
        e.animTime += g_Step;
    }

    // ---- トゲ玉（2026-10-25）: このステップの転がりを向きに掛け合わせる。以前は VS が
    //      animTime x 速度 / 半径 で回していたので、速度のわずかな変化も歩いた時間倍に増幅され、
    //      曲がるたびに姿勢が飛んでいた。上 x 進行方向 の軸まわりに d m 転がると d / 半径 回る ----
    if (enemyExtra[i].kind == SWARM_KIND_SPIKEBALL)
    {
        float2 moved = e.position.xz - startXZ;
        float dist = length(moved);
        if (dist > 1e-5)
        {
            float3 axis = normalize(cross(float3(0, 1, 0), float3(moved.x, 0.0, moved.y) / dist));
            float angle = dist * g_SpikeBallRollPerMeter / max(SwarmKindScale(SWARM_KIND_SPIKEBALL), 1e-3);
            float4 q = SwarmQuatMul(SwarmQuatAxisAngle(axis, angle), SwarmSpikeBallRoll(enemyRoll[i], i));
            enemyRoll[i] = normalize(q);
        }
    }

    enemyClimb[i] = climb;
    enemies[i].position = e.position;
    enemies[i].velocity.y = e.velocity.y;   // 落下速度（xz は AI のもの）
    enemies[i].yaw = e.yaw;
    enemies[i].animTime = e.animTime;
    enemies[i].animIndex = e.animIndex;

    CountAlive(i, e.position);
}