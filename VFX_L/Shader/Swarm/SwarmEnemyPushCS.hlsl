// ============================================================
// SwarmEnemyPushCS.hlsl
// 重なりの解消。SwarmEnemyMoveCS の直後に走る。
//
// AI の分離は速度の項にすぎない：最前列がプレイヤーの所で
// 止まると、後ろの列が押し続けて群れが
// 1 つの山に積み重なる。このパスは位置の拘束：
// 2 * g_EnemyRadius より近いペアはすべて押し離し、それぞれ
// めり込みの半分ずつ動かす（ヤコビ法、固定ステップごとに 1 回の反復。
// ステップは短いので、群れは数ステップで落ち着く）。
//
// 近傍はステップの最初に作った空間ハッシュから取るので、セルは
// 1 ステップ古いことがある。位置そのものは敵バッファから
// その場で読む（別スレッドがこのパスで既に持ち主を動かしている
// かもしれないが、柔らかいソルバーなので問題ない）。
//
// 塞がったセルに入ってしまう押しは捨てる：壁は
// 硬く、群れは柔らかい。
// ============================================================
#define SWARM_BOMBER_CB_REG b3   // 種類ごとの体の倍率（エリート / ボスの壁との隙間）
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
StructuredBuffer<uint> cellCount : register(t2);
StructuredBuffer<uint> cellItems : register(t3);
StructuredBuffer<float> terrainHeight : register(t4);
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5); // kind：幽霊は壁も崖も無視する
StructuredBuffer<float4> enemyClimb : register(t6);   // x > 0 = 壁を登っている（MoveCS、2026-10-29）：そのまま動かさない
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);

// 1 ステップで解消するめり込みの割合（1 = 全部、ガタつく）
static const float kPushRelax = 0.6;
// 深い山が瞬間移動しないための上限。敵の半径単位
static const float kPushMaxRadii = 0.75;
// 静止距離（当たり判定の直径単位）。> 1 なのはメッシュ
//（骸骨の幅 ~0.9m に対し当たり判定の直径 0.8m）が見た目でめり込まないため
static const float kPushDistMul = 1.15;

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;
    if (enemyExtra[i].kind == SWARM_KIND_TORNADO)
        return;   // 群れは砂旋を押さない（砂旋が群れを押す）（2026-10-17）
    if (enemyExtra[i].kind == SWARM_KIND_GARGOYLE && enemyExtra[i].fuse <= 0.0)
        return;   // 眠っているガーゴイルも押さない（石像、2026-10-25）
    if (enemyClimb[i].x != 0.0)   // > 0 は登っている、< 0 は上に乗り上げ中（2026-11-02）：どちらも動かさない
        return;   // 壁を登っている体も押さない：壁面から押し出すと手を離して落ちる（2026-10-29）

    float3 pos = enemies[i].position;
    float minDist = 2.0 * g_EnemyRadius * kPushDistMul;
    float minDistSq = minDist * minDist;

    float3 push = float3(0, 0, 0);

    int gx = (int) floor((pos.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((pos.z - g_GridOrigin.z) / g_CellSize);
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                continue;
            uint cell = (uint) (cz * (int) g_GridW + cx);
            uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
            for (uint k = 0u; k < n; ++k)
            {
                uint j = cellItems[cell * SWARM_BUCKET_CAP + k];
                if (j == i)
                    continue;
                if (enemyStates[j] == SWARM_DEAD)
                    continue;

                float3 away = pos - enemies[j].position;
                if (abs(away.y) > 1.5)
                    continue;   // 別の段（崖下の群れの上にある台地の縁）：触れていない
                away.y = 0.0;
                float dSq = dot(away, away);
                if (dSq >= minDistSq)
                    continue;

                float dist;
                if (dSq > 1e-6)
                    dist = sqrt(dSq);
                else
                {
                    // 完全に重なっている：スロット番号で方向を分けて離す
                    float a = (float) (i * 7u + j * 3u) * 0.618;
                    away = float3(cos(a), 0.0, sin(a));
                    dist = 1e-3;
                }
                push += away / dist * ((minDist - dist) * 0.5);
            }
        }
    }

    float pLenSq = dot(push, push);
    if (pLenSq < 1e-8)
        return;

    push *= kPushRelax;
    float maxPush = g_EnemyRadius * kPushMaxRadii;
    if (pLenSq * kPushRelax * kPushRelax > maxPush * maxPush)
        push *= maxPush * rsqrt(dot(push, push));

    float3 np = pos + push;
    uint kind = enemyExtra[i].kind;
    bool ghost = SwarmFliesThrough(enemyExtra[i]);   // 幽霊 / 飛んでいるガーゴイルは壁も崖も無視する
    if (!ghost)
    {
        // 体の円も壁 / 崖の面に押し込んではいけない
        //（2026-10-01）：押しのうち面に向かう分を捨て、それでも今より
        // 多く触れてしまうなら押し全体を捨てる
        float radius = SwarmBodyWallRadius(kind);
        float3 tn = SwarmBodyContact(terrain, terrainHeight, np.xz, radius);
        if (tn.z > 0.0)
        {
            float tl = length(tn.xy);
            if (tl > 1e-3)
            {
                float2 n = tn.xy / tl;
                float along = dot(push.xz, n);
                if (along > 0.0)
                    push.xz -= n * along;
                np = pos + push;
            }
            if (SwarmBodyContact(terrain, terrainHeight, np.xz, radius).z
                > SwarmBodyContact(terrain, terrainHeight, pos.xz, radius).z)
                return;
        }
        if (!SwarmIsWalkable(terrain, np) || !SwarmSlopeOk(terrainHeight, pos.xz, np.xz))
            return;   // 壁の中か崖の向こうに出てしまう：このステップは動かない
    }

    // 地上：地面には上にだけ合わせ、下には合わせない（2026-10-20）：双線形補間の地面は台地の縁から
    // 0.25m 以内で面の半分まで下がるので、そこでの押しで敵が沈み込んでいた
    //（代わりに MoveCS の Settle が足を台地の高さに保つ。坂道を下るときも次のステップで
    // 着地させる）。台地から落ちている最中（MoveCS）：高さはそのまま、重力で着地させる
    float groundNow = g_GroundY + SwarmTerrainHeight(terrainHeight, pos.xz);
    bool falling = (pos.y > groundNow + 0.25);   // = MoveCS kSnapDown
    np.y = falling ? pos.y : max(pos.y, g_GroundY + SwarmTerrainHeight(terrainHeight, np.xz));
    enemies[i].position = np;
}
