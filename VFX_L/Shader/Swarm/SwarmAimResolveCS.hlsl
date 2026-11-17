// ============================================================
// SwarmAimResolveCS.hlsl
// DispatchStep の 5 段目：命中と範囲ダメージ（4 / 4c）の後、
// 接触（6）の前。キーは MoveCS（2 段目）がその時生きていた敵について
// 書いたもの。その後 HitCS / AreaDamageCS が倒していた場合、CPU は
// 死体に 1 発撃つことになるが害は無い。ここで状態を見ないことで、
// 狙いの敵が死ぬたびに「標的なし」と報告するのを避ける。
// ============================================================
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
RWByteAddressBuffer counters : register(u0);
// 単純に一番近い敵（MoveCS の u4）：SWARM_NEAREST_*（SwarmCore.hlsli）。キーは次のステップのためにここでリセットする
RWByteAddressBuffer nearestTarget : register(u1);

[numthreads(1, 1, 1)]
void main()
{
    // 単純に一番近いもの（ボス / エリートの優先なし）
    {
        uint ckey = nearestTarget.Load(SWARM_NEAREST_KEY);
        if (ckey == SWARM_NO_TARGET_KEY)
            nearestTarget.Store(SWARM_NEAREST_DIST, asuint(1e30));
        else
        {
            SwarmEnemy c = enemies[ckey & SWARM_SLOT_MASK];
            float3 cp = c.position - g_PlayerPos;
            nearestTarget.Store3(SWARM_NEAREST_POS, asuint(c.position));
            nearestTarget.Store(SWARM_NEAREST_DIST, asuint(sqrt(cp.x * cp.x + cp.z * cp.z)));
        }
        nearestTarget.Store(SWARM_NEAREST_KEY, SWARM_NO_TARGET_KEY);
        // このステップで 10m 以内の敵の数 -> CPU 用に移し、数える側は 0 に戻す（スマートカメラ用）
        nearestTarget.Store(SWARM_NEAREST_CROWD, nearestTarget.Load(SWARM_NEAREST_CROWD_COUNTING));
        nearestTarget.Store(SWARM_NEAREST_CROWD_COUNTING, 0u);
    }

    uint key = counters.Load(SWARM_CNT_AIM_KEY);
    uint slot = key & SWARM_SLOT_MASK;

    if (key == SWARM_NO_TARGET_KEY )
    {
        counters.Store(SWARM_CNT_AIM_DIST, asuint(1e30));
        return;
    }

    SwarmEnemy e = enemies[slot];
    float3 dp = e.position - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);

    counters.Store3(SWARM_CNT_AIM_POS, asuint(e.position));
    counters.Store3(SWARM_CNT_AIM_VEL, asuint(e.velocity));
    counters.Store(SWARM_CNT_AIM_DIST, asuint(dist));
}
