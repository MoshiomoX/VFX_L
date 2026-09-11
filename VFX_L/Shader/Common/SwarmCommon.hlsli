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

// nearest enemy to the player. key = (asuint(dist) & 0xFFFFF000) | slot.
// positive floats compare as uints, so InterlockedMin finds the closest
// AND who it is in one atomic. 12 low bits = 4096 slots; kMaxEnemies
// must stay <= 4096 or this encoding breaks
static const uint SWARM_CNT_NEAREST_KEY = 16;
static const uint SWARM_CNT_NEAREST_POS = 20;
static const uint SWARM_CNT_NEAREST_VEL = 32;
static const uint SWARM_CNT_NEAREST_DIST = 44;

static const uint SWARM_NO_TARGET_KEY = 0xFFFFFFFFu;
static const uint SWARM_SLOT_MASK = 0xFFFu;
static const uint SWARM_DIST_MASK = 0xFFFFF000u;

#define SWARM_HP_SCALE 100.0
uint SwarmHpToFixed(float hp)
{
    return (uint) (hp * SWARM_HP_SCALE + 0.5);
}
// ============================================================
// Enemy: 48 bytes
// ============================================================
struct SwarmEnemy
{
    float3 position;
    uint hp;

    float3 velocity;
    float moveSpeed;

    float yaw;

    // reserved for VAT animation (unused for now)
    float animTime;
    uint animIndex;
    float attackCooldown;
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
    uint vfxType; // index into SwarmVFXTable's recipe table
    float2 _pad;
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
// Recipe: what a vfxType is made of. 32 bytes.
// Each range indexes into a per-type table. A projectile may carry
// several emitters (core + trail), a mesh, a light -- each consumed
// by its own CS with the same "one thread per projectile" skeleton.
// Must match Swarm::VFXRecipe in SwarmVFXTable.h
// ============================================================
struct SwarmRecipe
{
    uint particleStart;
    uint particleCount;
    uint modelStart;
    uint modelCount;

    uint lightStart;
    uint lightCount;
    uint2 _pad;
};

// ============================================================
// Per-frame constants. Default register b0; a CS that also includes
// ParticleCommon.hlsli (which owns b0/b1) must #define
// SWARM_FRAME_CB_REG before including this file.
// ============================================================
#ifndef SWARM_FRAME_CB_REG
#define SWARM_FRAME_CB_REG b0
#endif

cbuffer SwarmFrameCB : register(SWARM_FRAME_CB_REG)
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
// Enemy AI tuning. Default register b1; a CS that already uses b1
// must #define SWARM_AI_CB_REG before including this file.
// Must match Swarm::AICB in SwarmTypes.h
// ============================================================
#ifndef SWARM_AI_CB_REG
#define SWARM_AI_CB_REG b1
#endif

cbuffer SwarmAICB : register(SWARM_AI_CB_REG)
{
    float g_SeparationRadius;
    float g_SeparationPower;
    float g_AvoidPower;
    float g_LookAhead;

    float g_GroundY;
    float g_EnemyRadius;
    float g_EnemyCapsuleHalf;
    float g_VelocityLag;

    float g_MaxSpeedMul;
    float g_TurnSpeed;
    float g_PlayerPushOut;
    float g_ContactDamage;

    float g_AttackInterval;
    float g_PlayerCapsuleHalf;
    float2 _aiPad;
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