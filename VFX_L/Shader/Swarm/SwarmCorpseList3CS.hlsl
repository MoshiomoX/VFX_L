// ============================================================
// SwarmCorpseList3CS.hlsl
// ステージ固定のエリートの砕け散りリスト（2026-10-25）:
// SwarmCorpseListCS と SwarmCorpseList2CS はもう UAV を 8 本ずつバインドしている。
// 同じ規則: まだ砕け散っている途中のエントリ（0 <= age < g_CorpseLife）を
// 見た目ごとのリストに入れるので、各エリートは自分のスキンで砕ける。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;
    float g_CorpseLife;
    float2 _corpseListPad;
};

StructuredBuffer<SwarmCorpse> corpses : register(t0);
AppendStructuredBuffer<uint> eliteACorpses : register(u0);
AppendStructuredBuffer<uint> eliteBCorpses : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= SWARM_MAX_CORPSES)
        return;
    SwarmCorpse c = corpses[i];
    float age = g_CorpseTime - c.birth;
    if (c.birth <= 0.0 || age < 0.0 || age >= g_CorpseLife)
        return;

    if (c.kind == SWARM_KIND_ELITE_A)
        eliteACorpses.Append(i);
    else if (c.kind == SWARM_KIND_ELITE_B)
        eliteBCorpses.Append(i);
}
