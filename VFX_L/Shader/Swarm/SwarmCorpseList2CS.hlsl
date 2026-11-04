// ============================================================
// SwarmCorpseList2CS.hlsl
// SwarmCorpseListCS の後半（2026-10-25）: あちらのパスは既に UAV を 8 本
// 使い切っていて、そのせいでボス・砂嵐の術師・突撃爆弾兵は借り物の皮
// （ゾンビ / 赤いロボットのもの）で砕けていた。
// ここでそれらと遺跡の敵に専用の一覧を持たせ、どの種類も自分の皮で
// 砕けるようにする。規則は同じで、まだ砕けている途中のもの
// （0 <= age < g_CorpseLife）を見た目ごとの一覧に入れる。
// （呪いの鎧は 1 回目の死では崩れ落ちるだけで、死体を書かない。）
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;
    float g_CorpseLife;
    float2 _corpseListPad;
};

StructuredBuffer<SwarmCorpse> corpses : register(t0);
AppendStructuredBuffer<uint> bossCorpses : register(u0);
AppendStructuredBuffer<uint> sandMageCorpses : register(u1);
AppendStructuredBuffer<uint> dashBomberCorpses : register(u2);
AppendStructuredBuffer<uint> armorCorpses : register(u3);
AppendStructuredBuffer<uint> gargoyleCorpses : register(u4);
AppendStructuredBuffer<uint> necroCorpses : register(u5);
AppendStructuredBuffer<uint> skeletonCorpses : register(u6);
AppendStructuredBuffer<uint> wardenCorpses : register(u7);

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

    if (c.kind == SWARM_KIND_BOSS)
        bossCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SANDMAGE)
        sandMageCorpses.Append(i);
    else if (c.kind == SWARM_KIND_DASHBOMBER)
        dashBomberCorpses.Append(i);
    else if (c.kind == SWARM_KIND_ARMOR || c.kind == SWARM_KIND_ARMOR2)
        armorCorpses.Append(i);
    else if (c.kind == SWARM_KIND_GARGOYLE)
        gargoyleCorpses.Append(i);
    else if (c.kind == SWARM_KIND_NECRO)
        necroCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SKELETON)
        skeletonCorpses.Append(i);
    else if (c.kind == SWARM_KIND_WARDEN)
        wardenCorpses.Append(i);
}
