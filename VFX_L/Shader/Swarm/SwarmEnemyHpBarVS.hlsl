// ============================================================
// SwarmEnemyHpBarVS.hlsl
// HP bar floating over every live mob. One camera-facing quad per
// enemy, 6 vertices from SV_VertexID, no vertex buffer. Instances
// come from the same alive list as the mob mesh (SwarmEnemyCompactCS),
// drawn with DrawInstancedIndirect.
//
// Size is in world units (shrinks with distance like the mesh).
// Fill ratio = hp / spawn hp. The spawn hp lives in a side buffer
// (enemyMaxHp, written by SwarmSpawnEnemyCS / SwarmRecycleCS) so
// SwarmEnemy stays 48 bytes.
//
// C++ mirror of the cbuffer: SwarmHpBarCB in SwarmSystem.cpp
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmHpBarCB : register(b0)
{
    row_major float4x4 g_BarView;
    row_major float4x4 g_BarProj;
    float g_BarWidth;   // meters
    float g_BarHeight;  // meters
    float g_BarOffset;  // meters above SwarmEnemy.position (capsule centre)
    float g_BarBorder;  // meters
    float4 g_BarFill;
    float4 g_BarBack;
    float4 g_BarEdge;
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<uint> aliveList : register(t2);
StructuredBuffer<uint> enemyMaxHp : register(t3);

struct BarOut
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;   // (0,0) top-left .. (1,1) bottom-right
    float ratio : TEXCOORD1; // 0..1 remaining hp
};

static const float2 kCorner[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

BarOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    BarOut o = (BarOut) 0;

    uint slot = aliveList[iid];
    if (enemyStates[slot] == SWARM_DEAD)
    {
        o.pos = float4(0, 0, -1, 1); // clipped
        return o;
    }

    SwarmEnemy e = enemies[slot];

    // hp drops through InterlockedAdd of a negative value, so the killing
    // hit can wrap it past 0 (huge uint) before the state flips: treat
    // anything above the spawn value as empty
    uint maxHp = max(enemyMaxHp[slot], 1u);
    o.ratio = (e.hp > maxHp) ? 0.0 : (float) e.hp / (float) maxHp;

    // camera axes in world space = the first two columns of the view
    // matrix (row-vector convention). Keeps the bar facing the screen
    // with its fill growing left to right whatever the handedness
    float3 right = float3(g_BarView._11, g_BarView._21, g_BarView._31);
    float3 up = float3(g_BarView._12, g_BarView._22, g_BarView._32);

    float2 c = kCorner[vid];
    float3 center = e.position + float3(0.0, g_BarOffset, 0.0);
    float3 world = center
        + right * ((c.x - 0.5) * g_BarWidth)
        + up * ((0.5 - c.y) * g_BarHeight);

    o.pos = mul(mul(float4(world, 1.0), g_BarView), g_BarProj);
    o.uv = c;
    return o;
}
