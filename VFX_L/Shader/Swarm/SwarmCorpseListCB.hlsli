// ============================================================
// SwarmCorpseListCB.hlsli
// 砕け散りの描画リストを作る CS（SwarmCorpseListCS / List2CS / List3CS）の cbuffer（b4）。
// C++ 側：SwarmSystem::CorpseListCB
// ============================================================
#ifndef SWARM_CORPSE_LIST_CB_HLSLI
#define SWARM_CORPSE_LIST_CB_HLSLI

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;   // SwarmSystem の時計（SwarmCorpse.birth と同じ）
    float g_CorpseLife;   // 砕けてから消えるまでの秒
    float2 _corpseListPad;
};

#endif
