// ============================================================
// SwarmCommon.hlsli
// C++ side (Swarm/SwarmTypes.h) must match these layouts exactly.
// StructuredBuffer has no reflection, a mismatch fails silently.
//
// Layout rule: keep every struct a multiple of 16 bytes and avoid
// straddling float3 across 16-byte boundaries.
//
// Note: the alive/dead flag is NOT in these structs. It lives in a
// separate RWBuffer<uint> because SM5.0 atomics only work on
// RWByteAddressBuffer and RWBuffer<uint> / RWStructuredBuffer<uint>,
// and slot claiming needs InterlockedCompareExchange.
// ============================================================

#ifndef SWARM_COMMON_HLSLI
#define SWARM_COMMON_HLSLI

// ---- fixed step, shared by every CS in this pipeline ----
static const float SWARM_FIXED_STEP = 0.02; // Unity default (50Hz)

// ---- slot state (stored in the separate state buffers) ----
static const uint SWARM_DEAD = 0;
static const uint SWARM_ALIVE = 1;

// ---- counter byte offsets (RWByteAddressBuffer) ----
static const uint SWARM_CNT_ALIVE_ENEMIES = 0;
static const uint SWARM_CNT_KILLS = 4;
static const uint SWARM_CNT_PLAYER_DAMAGE = 8;
static const uint SWARM_CNT_ALIVE_PROJ = 12;

// ============================================================
// Enemy: 48 bytes
// ============================================================
struct SwarmEnemy
{
    float3 position;
    float hp;

    float3 velocity;
    float moveSpeed;

    float yaw;

    // reserved for VAT animation (unused for now)
    float animTime;
    uint animIndex;
    float _pad;
};

// ============================================================
// Projectile: 48 bytes
// ============================================================
struct SwarmProjectile
{
    float3 position;
    float damage;

    float3 velocity;
    float lifetime;

    float radius;
    float3 _pad;
};

// ============================================================
// Exp orb: 32 bytes
// ============================================================
struct SwarmOrb
{
    float3 position;
    float amount;

    float3 velocity;
    float _pad;
};

// ============================================================
// Per-frame constants (b0). 64 bytes.
// ============================================================
cbuffer SwarmFrameCB : register(b0)
{
    float3 g_PlayerPos;
    float g_PlayerRadius;

    float g_Step;
    uint g_MaxEnemies;
    uint g_MaxProjectiles;
    uint g_MaxOrbs;

    float3 g_GridOrigin; // world position of cell (0,0)
    float g_CellSize;

    uint g_GridW;
    uint g_GridD;
    uint g_Seed;
    uint g_PlayerAlive; // 0 = dead, enemies stop seeking
};

// ============================================================
// terrain lookup: 1 = walkable
// ============================================================
bool SwarmIsWalkable(StructuredBuffer<uint> walkable, float3 p)
{
    int gx = (int) floor((p.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((p.z - g_GridOrigin.z) / g_CellSize);

    if (gx < 0 || gx >= (int) g_GridW)
        return false;
    if (gz < 0 || gz >= (int) g_GridD)
        return false;

    return walkable[gz * g_GridW + gx] != 0;
}

#endif