// ============================================================
// SwarmProjTrailCS.hlsl
// GPU の弾の帯状の軌跡（2026-10-15）。弾のスロット 1 つにつき
// 1 スレッド、固定ステップごとに ProjMoveCS の直後に走る。
//
// どのスロットも TRAIL_POINTS 個の位置のリングを持つ（並びと
// スタイル表はパーティクルの軌跡、ParticleTrail.hlsli と同じ）。
// レシピに Trail エントリがあるスロット（recipeTrail[vfxType] != 0）は、
// 経過時間が lifetime / (TRAIL_POINTS - 1) の格子をまたぐたびに
// 自分の位置をリングに押し込む。死んだスロットはリセットするので、
// 次にそこに入った弾は新しい帯から始まる。SwarmSpawnProjCS も
// スロットを使い回す時に状態を消す（2026-10-25）：フレームの最後の
// ステップで HitCS に消された弾は、このパスが死を見る前に使い回され、
// 新しい帯が古い弾の点から始まっていた。
//
// trailState : bit 0-7 リングの書き込み位置、bit 8-15 有効なサンプル数、
//              bit 16 = 吸い込み中（弾が死んだ。帯には着弾点を
//              入れ続けるので尾が追いつく =
//              「敵の中に吸い込まれる」）、bit 17-24 = 死んでから
//              押し込んだサンプル数（TRAIL_POINTS 個で排出完了）
// trailAge   : スロットが生きてからの秒（格子の時計）
// ============================================================
#include "../Common/SwarmCommon.hlsli"
#include "../Particle/Common/ParticleTrail.hlsli"
#include "../Common/SwarmProjTrail.hlsli"

StructuredBuffer<uint> recipeTrail : register(t0);
StructuredBuffer<TrailStyle> trailStyles : register(t1);
StructuredBuffer<SwarmProjectile> projectiles : register(t2);
Buffer<uint> projStates : register(t3);

RWStructuredBuffer<float3> trailPoints : register(u0);
RWStructuredBuffer<uint> trailState : register(u1);
RWStructuredBuffer<float> trailAge : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxProjectiles)
        return;

    uint state = trailState[i];
    bool alive = projStates[i] != SWARM_DEAD;
    if (!alive && state == 0u)
        return;

    // スロットは弾が死んだ後も最後のデータを持っている（使い回されるまで）
    SwarmProjectile p = projectiles[i];
    uint style = recipeTrail[p.vfxType];
    if (style == 0u)
    {
        if (state != 0u)
        {
            trailState[i] = 0u;
            trailAge[i] = 0.0;
        }
        return;
    }

    bool draining = (state & PROJ_TRAIL_DRAINING) != 0u;
    if (alive && draining)
    {
        // 古い帯が排出し切る前にスロットが使い回された：最初からやり直す
        state = 0u;
        draining = false;
        trailAge[i] = 0.0;
    }
    if (!alive)
        draining = true; // 今死んだ、またはまだ吸い込み中

    TrailStyle s = trailStyles[style - 1u];
    float interval = TrailInterval(s.lifetime);

    float agePrev = trailAge[i];
    float age = agePrev + g_Step;
    trailAge[i] = age;

    uint head = TrailHead(state);
    uint count = TrailCount(state);
    uint drained = state >> PROJ_TRAIL_DRAIN_SHIFT;

    uint cellNow = (uint) (age / interval);
    uint cellPrev = (uint) (agePrev / interval);

    // count == 0：生きて最初のステップ -> 出発点を記録する。
    // 吸い込み中：先頭は着弾点のまま。押し込むたびに一番古いサンプルが
    // それに置き換わるので、帯は尾の側から短くなる
    if (count == 0u || cellNow != cellPrev)
    {
        head = (count == 0u) ? 0u : (head + 1u) % TRAIL_POINTS;
        trailPoints[i * TRAIL_POINTS + head] = p.position;
        count = min(count + 1u, TRAIL_POINTS);
        if (draining)
            drained += 1u;
    }

    if (draining && drained >= TRAIL_POINTS)
    {
        trailState[i] = 0u; // 全サンプルが着弾点になった：消える
        trailAge[i] = 0.0;
        return;
    }
    trailState[i] = TrailPack(head, count)
        | (draining ? PROJ_TRAIL_DRAINING : 0u)
        | (drained << PROJ_TRAIL_DRAIN_SHIFT);
}
