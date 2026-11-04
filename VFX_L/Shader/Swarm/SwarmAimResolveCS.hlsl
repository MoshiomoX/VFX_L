// ============================================================
// SwarmAimResolveCS.hlsl
// DispatchStep の 5 段目：命中と範囲ダメージ（4 / 4c）の後、
// 接触（6）の前。キーは MoveCS（2 段目）がその時生きていた敵について
// 書いたもの。その後 HitCS / AreaDamageCS が倒していた場合、CPU は
// 死体に 1 発撃つことになるが害は無い。ここで状態を見ないことで、
// 一番近い敵が死ぬたびに「標的なし」と報告するのを避ける。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
RWByteAddressBuffer counters : register(u0);
// 単純に一番近い敵（2026-10-27、MoveCS の u4）：+0 キー、+4 位置、+16 距離。キーは次のステップのためにここでリセットする
RWByteAddressBuffer closeTarget : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    // 単純に一番近いもの（ボス / エリートの優先なし）
    {
        uint ckey = closeTarget.Load(0);
        if (ckey == SWARM_NO_TARGET_KEY)
            closeTarget.Store(16, asuint(1e30));
        else
        {
            SwarmEnemy c = enemies[ckey & SWARM_SLOT_MASK];
            float3 cp = c.position - g_PlayerPos;
            closeTarget.Store3(4, asuint(c.position));
            closeTarget.Store(16, asuint(sqrt(cp.x * cp.x + cp.z * cp.z)));
        }
        closeTarget.Store(0, SWARM_NO_TARGET_KEY);
        // このステップで 10m 以内の敵の数（MoveCS の +20）-> CPU 用に +24 へ、+20 は 0 に戻す（2026-11-02、スマートカメラ）
        closeTarget.Store(24, closeTarget.Load(20));
        closeTarget.Store(20, 0u);
    }

    uint key = counters.Load(SWARM_CNT_NEAREST_KEY);
    uint slot = key & SWARM_SLOT_MASK;

    if (key == SWARM_NO_TARGET_KEY )
    {
        counters.Store(SWARM_CNT_NEAREST_DIST, asuint(1e30));
        return;
    }

    SwarmEnemy e = enemies[slot];
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);

    counters.Store3(SWARM_CNT_NEAREST_POS, asuint(e.position));
    counters.Store3(SWARM_CNT_NEAREST_VEL, asuint(e.velocity));
    counters.Store(SWARM_CNT_NEAREST_DIST, asuint(dist));
}
