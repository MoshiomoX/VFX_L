// ============================================================
// SwarmRecycleCS.hlsl
// Pool is full but the director still wants spawns: instead of
// killing the farthest enemies and re-spawning, far-enough alive
// enemies claim a pending request and overwrite themselves with
// it (position, hp, speed -- everything). No sort, no free slot.
//
// "Far enough" is a distance threshold, not "the N farthest".
// First come first served among those past the threshold; if
// nobody is far enough the requests are simply dropped, which is
// the intended behaviour when the whole crowd is on the player.
// ============================================================
#define SWARM_AI_CB_REG b2
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> spawnRequests : register(t0);
Buffer<uint> enemyStates : register(t1);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer claim : register(u1);

cbuffer SwarmRecycleCB : register(b1)
{
    uint g_RecycleCount;
    uint g_RecycleOffset;
    float g_RecycleMinDistSq;
    uint _recyclePad;
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float3 d = enemies[i].position - g_PlayerPos;
    float dSq = d.x * d.x + d.z * d.z;
    if (dSq < g_RecycleMinDistSq)
        return;

    uint prev;
    claim.InterlockedAdd(0, 1u, prev);
    if (prev >= g_RecycleCount)
        return;

    enemies[i] = spawnRequests[g_RecycleOffset + prev];
}