// ============================================================
// SwarmContactCS.hlsl
// Enemy touches player -> damage. One thread per enemy slot.
//
// Both bodies are vertical capsules, so the test is: XZ circle
// distance combined with the vertical gap between the two straight
// segments. Jumping over the crowd clears the vertical gap and
// lands no hit, which is the intended read.
//
// Each enemy has its own cooldown so a crowd of 30 does not deal
// 30x per step. Damage goes into the counter as fixed point x100;
// the CPU takes the delta and feeds it to the player state machine,
// which owns invincibility frames.
//
// Only writer of Enemy.attackCooldown.
// ============================================================
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    float cd = enemies[i].attackCooldown - g_Step;

    if (cd <= 0.0 && g_PlayerAlive != 0u)
    {
        float3 pos = enemies[i].position;

        float dx = pos.x - g_PlayerPos.x;
        float dz = pos.z - g_PlayerPos.z;
        float dxzSq = dx * dx + dz * dz;

        // vertical gap between the two straight segments (0 if they overlap)
        float dy = abs(pos.y - g_PlayerPos.y);
        float gap = max(0.0, dy - (g_EnemyCapsuleHalf + g_PlayerCapsuleHalf));

        float reach = g_EnemyRadius + g_PlayerRadius;
        if (dxzSq + gap * gap <= reach * reach)
        {
            uint prev;
            counters.InterlockedAdd(SWARM_CNT_PLAYER_DAMAGE,
                                    SwarmHpToFixed(g_ContactDamage), prev);
            cd = g_AttackInterval;
        }
    }
        
    enemies[i].attackCooldown = max(cd, 0.0);
}