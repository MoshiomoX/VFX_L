// ============================================================
// SwarmOrbMoveCS.hlsl
// 経験玉のスロット 1 つにつき 1 スレッド。プレイヤーへ吸い寄せ、拾い、数える。
//
// 吸引の状態は SwarmOrb._pad に今の吸引の速さとして入っている：
//   0     -> 待機、プレイヤーが吸引半径に入るのを待つ
//   > 0   -> 吸引中。一度吸われたら、プレイヤーが半径から出ても
//            放さない（CPU 版と同じ規則、
//            境界で行ったり来たりしないように）
//
// 拾うと amount（固定小数点 x100）を SWARM_CNT_EXP に足す。これは
// 永久に累積し、CPU が差分を取る。
//
// 経験玉の position / velocity / _pad を書くのはここだけ。HitCS の後に走るので、
// このステップで落ちた経験玉も既に見えている。
//
// velocity は見た目専用（SwarmOrbVS が宝石をその向きに傾け / 伸ばし、
// SwarmOrbEmitCS が尾を掃き出す）：このステップの水平方向の吸引、
// 待機中は 0。
//
// 高さ：地形（台地、坂）の g_OrbY 上。補間するので、台地の縁を越えて
// 吸われる経験玉は急に跳ねず滑らかに上がる。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<float> terrainHeight : register(t0);

RWStructuredBuffer<SwarmOrb> orbs : register(u0);
RWBuffer<uint> orbStates : register(u1);
RWByteAddressBuffer counters : register(u2);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxOrbs)
        return;
    if (orbStates[i] == SWARM_DEAD)
        return;

    SwarmOrb o = orbs[i];

    // XZ だけ、パイプラインの他の部分と同じ
    float dx = g_PlayerPos.x - o.position.x;
    float dz = g_PlayerPos.z - o.position.z;
    float distSq = dx * dx + dz * dz;

    float speed = o._pad;
    bool attracted = speed > 0.0;
    float3 vel = float3(0, 0, 0);

    if (g_PlayerAlive != 0u)
    {
        if (!attracted && distSq <= g_OrbAttractRadius * g_OrbAttractRadius)
        {
            attracted = true;
            speed = g_OrbAccel * g_Step; // 次のステップで speed > 0 になるよう初速を与える
        }

        if (attracted)
        {
            // ---- 拾う ----
            if (distSq <= g_OrbPickupRadius * g_OrbPickupRadius)
            {
                uint prev;
                counters.InterlockedAdd(SWARM_CNT_EXP,
                                        SwarmHpToFixed(o.amount), prev);
                orbStates[i] = SWARM_DEAD;
                return;
            }

            // ---- 吸引：吸われている間に加速する（上限 g_OrbMaxSpeed） ----
            speed = min(speed + g_OrbAccel * g_Step, g_OrbMaxSpeed);

            float invLen = rsqrt(max(distSq, 1e-8));
            vel = float3(dx * invLen * speed, 0.0, dz * invLen * speed);
            o.position.x += vel.x * g_Step;
            o.position.z += vel.z * g_Step;
        }
    }

    float targetY = SwarmTerrainHeight(terrainHeight, o.position.xz) + g_OrbY;
    o.position.y = lerp(o.position.y, targetY, 1.0 - exp(-12.0 * g_Step));
    o._pad = speed;

    orbs[i].position = o.position;
    orbs[i].velocity = vel;
    orbs[i]._pad = speed;

    uint prev2;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ORBS, 1u, prev2);
}