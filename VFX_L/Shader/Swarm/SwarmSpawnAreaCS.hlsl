// ============================================================
// SwarmSpawnAreaCS.hlsl
// CPU から来た範囲の生成要求を、範囲プールの空きスロットに入れる。
// 要求 1 つに 1 スレッド。他の生成シェーダーと同じ CAS の走査。
// プールが満杯なら要求は捨てる。
//
// GPU で生まれる範囲（弾の命中）はここを通らない。
// SwarmCommon.hlsli の SwarmSpawnAreaFromDef を参照。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmArea> spawnRequests : register(t0);
RWStructuredBuffer<SwarmArea> areas : register(u0);
RWBuffer<uint> areaStates : register(u1);

cbuffer SwarmSpawnCB : register(b1)
{
    uint g_RequestCount;
    uint g_ScanStart;
    uint2 _spawnPad;
};

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_RequestCount)
        return;

    SwarmArea req = spawnRequests[id.x];
    req.tickNow &= ~SWARM_AREA_TICK_MASK; // CPU は追加の情報（毒素、飛沫）を上位ビットに入れた。tick の状態はまっさらから始める

    uint start = (g_ScanStart + id.x * 97u) % SWARM_MAX_AREAS;
    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        uint slot = (start + k) % SWARM_MAX_AREAS;
        uint was;
        InterlockedCompareExchange(areaStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
        if (was == SWARM_DEAD)
        {
            areas[slot] = req;
            return;
        }
    }
}
