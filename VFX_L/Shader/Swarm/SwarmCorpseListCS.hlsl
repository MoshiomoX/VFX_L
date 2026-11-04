// ============================================================
// SwarmCorpseListCS.hlsl
// 1 フレームに 1 回、死体リングの要素 1 つにつき 1 スレッド（2026-10-02、死亡時の
// 砕け散り）：まだ砕けている最中の要素（0 <= age < g_CorpseLife）を、見た目ごとの
// 描画一覧に入れる。SwarmEnemyCompactCS が生きている敵を振り分けるのと
// 同じ：種類ごとの一覧に分ける（ボス・砂嵐の術師・突撃爆弾兵・遺跡の種類は
// SwarmCorpseList2CS が受け持つ）。幽霊は半透明で描く。一覧の長さが各サブメッシュの
// DrawIndexedInstancedIndirect の InstanceCount になる。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmCorpseListCB : register(b4)
{
    float g_CorpseTime;
    float g_CorpseLife;
    float2 _corpseListPad;
};

StructuredBuffer<SwarmCorpse> corpses : register(t0);
AppendStructuredBuffer<uint> mobCorpses : register(u0);
AppendStructuredBuffer<uint> bomberCorpses : register(u1);
AppendStructuredBuffer<uint> ghostCorpses : register(u2);
AppendStructuredBuffer<uint> splitterCorpses : register(u3);
AppendStructuredBuffer<uint> bruteCorpses : register(u4);
AppendStructuredBuffer<uint> chargerCorpses : register(u5); // 2026-10-08
AppendStructuredBuffer<uint> shieldCorpses : register(u6);
AppendStructuredBuffer<uint> burrowerCorpses : register(u7); // 2026-10-17

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

    if (SwarmInCorpseList2(c.kind))
        return;   // ボス、砂嵐の術師、突撃爆弾兵、遺跡の種類：SwarmCorpseList2CS（自分のスキン、2026-10-25）
    if (c.kind == SWARM_KIND_BOMBER)
        bomberCorpses.Append(i);
    else if (c.kind == SWARM_KIND_GHOST || c.kind == SWARM_KIND_DARKGHOST)
        ghostCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SPLITTER || c.kind == SWARM_KIND_SPLITLING)
        splitterCorpses.Append(i);
    else if (c.kind == SWARM_KIND_BRUTE)
        bruteCorpses.Append(i);
    else if (c.kind == SWARM_KIND_CHARGER)
        chargerCorpses.Append(i);
    else if (c.kind == SWARM_KIND_SHIELD)
        shieldCorpses.Append(i);
    else if (c.kind == SWARM_KIND_BURROWER)
        burrowerCorpses.Append(i);
    else
        mobCorpses.Append(i);
}
