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
// exp collected by the player, fixed point x100. ACCUMULATES forever
// like KILLS / PLAYER_DAMAGE: the CPU takes the delta. Written by
// OrbMoveCS on pickup.
static const uint SWARM_CNT_EXP = 48;
// alive orb count, cleared every step (same as ALIVE_ENEMIES)
static const uint SWARM_CNT_ALIVE_ORBS = 52;
// area attacks. both cleared every step (same as ALIVE_ENEMIES)
static const uint SWARM_CNT_ALIVE_AREAS = 56;
static const uint SWARM_CNT_TICKING_AREAS = 60; // areas that deal damage THIS step
// total 64 bytes
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

    // animIndex 2 = hit stun. HitCS sets it with animTime = 0, MoveCS
    // counts animTime up to g_HitStun then clears it, the VS reads it
    // for the flash. Other values are reserved for VAT animation.
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
    uint motion; // motion table index. In a spawn REQUEST bit 31 = mirror the curve
    float pathT; // Bezier parameter 0..1 while on a curve
};

// ============================================================
// Projectile motion
//
// SwarmMotion   : one row per projectile profile (the editor's data).
//                 Must match Swarm::Motion in SwarmTypes.h. 48 bytes.
// SwarmProjPath : one per projectile slot, same index as the pool.
//                 The cubic Bezier this projectile is flying, built on
//                 the GPU at spawn (and again on re-target).
//                 Must match Swarm::ProjPath. 64 bytes.
//
// Control points are authored in the frame of the shot, scaled by the
// shot distance, so one curve fits near and far targets:
//     c.x = fraction along muzzle -> target
//     c.y = sideways offset / distance   (mirrored by sideSign)
//     c.z = upward   offset / distance   (world up)
// ============================================================
static const uint SWARM_MOTION_STRAIGHT = 0;
static const uint SWARM_MOTION_CURVE_ONCE = 1;
static const uint SWARM_MOTION_TRACK = 2;

static const uint SWARM_NO_TARGET = 0xFFFFFFFFu;
static const uint SWARM_MOTION_INDEX_MASK = 0xFFFFu;
static const uint SWARM_MOTION_FLIP_BIT = 0x80000000u;

struct SwarmMotion
{
    uint mode;
    float retargetRadius; // TRACK only. <= 0 = unlimited
    uint hitArea; // area def spawned where the projectile hits. 0 = none
    uint hitAreaFlags; // bit 0 = also spawn when it expires / hits a wall

    float3 c1;
    float _pad1;

    float3 c2;
    float _pad2;
};

struct SwarmProjPath
{
    float3 p0;
    uint target; // enemy slot, SWARM_NO_TARGET = flying straight

    float3 p1;
    float duration; // seconds from p0 to p3

    float3 p2;
    float sideSign; // +1 / -1, fixed per shot

    float3 p3;
    float speed; // nominal speed, restored when the curve lets go
};

float3 SwarmBezier(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return c.p0 * (u * u * u)
         + c.p1 * (3.0 * u * u * t)
         + c.p2 * (3.0 * u * t * t)
         + c.p3 * (t * t * t);
}

float3 SwarmBezierTangent(SwarmProjPath c, float t)
{
    float u = 1.0 - t;
    return (c.p1 - c.p0) * (3.0 * u * u)
         + (c.p2 - c.p1) * (6.0 * u * t)
         + (c.p3 - c.p2) * (3.0 * t * t);
}

// Fills p0..p3 and duration. sideSign / speed / target are the caller's.
// keepHeading: leave along `heading` (re-target in flight) instead of
// using c1, so the path has no kink where the new curve starts.
void SwarmBuildPath(inout SwarmProjPath path, SwarmMotion m,
                    float3 from, float3 to, float3 heading, bool keepHeading)
{
    float3 chord = to - from;
    float dist = length(chord);
    float3 fwd = (dist > 1e-4) ? chord / dist : heading;

    float3 up = float3(0, 1, 0);
    float3 side = cross(up, fwd);
    float sideLenSq = dot(side, side);
    side = (sideLenSq > 1e-6) ? side * rsqrt(sideLenSq) : float3(1, 0, 0);

    path.p0 = from;
    path.p3 = to;
    path.p1 = from + fwd * (m.c1.x * dist)
                   + side * (m.c1.y * dist * path.sideSign)
                   + up * (m.c1.z * dist);
    path.p2 = from + fwd * (m.c2.x * dist)
                   + side * (m.c2.y * dist * path.sideSign)
                   + up * (m.c2.z * dist);
    if (keepHeading)
        path.p1 = from + heading * (dist / 3.0);

    // arc length ~ average of the chord and the control polygon
    float len = 0.5 * (dist + length(path.p1 - path.p0)
                            + length(path.p2 - path.p1)
                            + length(path.p3 - path.p2));
    path.duration = max(len / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// ============================================================
// Area attacks (explosions, magic circles)
//
// SwarmArea    : one live area. 48 bytes. Must match Swarm::Area.
//                One-shot and lasting areas are the same thing:
//                an explosion is an area that ticks once and lingers
//                only long enough for its particles.
// SwarmAreaDef : a template. 32 bytes. Must match Swarm::AreaDef.
//                Row 0 = none. Used when the GPU itself spawns an area
//                (projectile hit), because only the GPU knows where.
//
// Shape: a disc. XZ distance <= radius and |dy| <= halfHeight
// (+ the enemy capsule), same vertical rule as the projectile hit.
// ============================================================
static const uint SWARM_MAX_AREAS = 256; // = Swarm::kMaxAreas

static const uint SWARM_AREA_FOLLOW_PLAYER = 1u; // centre rides on the player
static const uint SWARM_AREA_STUN = 2u; // a tick freezes + flashes the enemy (hit stun)

static const uint SWARM_HITAREA_ON_EXPIRE = 1u; // SwarmMotion.hitAreaFlags

struct SwarmArea
{
    float3 center;
    float radius;

    float damage; // per tick
    float timeLeft;
    float tickInterval;
    float tickTimer; // seconds until the next tick. 0 at spawn = ticks on its first step

    float halfHeight;
    uint flags;
    uint vfxType; // recipe index for GPU-side particles. 0 = none (the CPU plays the VFX)
    uint tickNow; // 1 while this step deals damage. written by AreaTickCS
};

struct SwarmAreaDef
{
    float radius;
    float halfHeight;
    float damage;
    float duration;

    float tickInterval;
    uint flags;
    uint vfxType;
    uint _pad;
};

// ---- GPU-side spawn ----
// A shader that may spawn areas #defines the three registers before
// including this file, e.g.
//     #define SWARM_AREA_POOL_U  u6
//     #define SWARM_AREA_STATE_U u7
//     #define SWARM_AREA_DEF_T   t2
#ifdef SWARM_AREA_POOL_U
RWStructuredBuffer<SwarmArea> areas : register(SWARM_AREA_POOL_U);
RWBuffer<uint> areaStates : register(SWARM_AREA_STATE_U);
StructuredBuffer<SwarmAreaDef> areaDefs : register(SWARM_AREA_DEF_T);

// Same CAS scan as the other pools. Pool full -> no area (degrade, never corrupt)
void SwarmSpawnAreaFromDef(uint defId, float3 pos, uint salt)
{
    if (defId == 0u)
        return;

    SwarmAreaDef d = areaDefs[defId];

    SwarmArea a = (SwarmArea) 0;
    a.center = pos;
    a.radius = d.radius;
    a.damage = d.damage;
    a.timeLeft = d.duration;
    a.tickInterval = d.tickInterval;
    a.tickTimer = 0.0;
    a.halfHeight = d.halfHeight;
    a.flags = d.flags & ~SWARM_AREA_FOLLOW_PLAYER; // born from a hit: stays where it is
    a.vfxType = d.vfxType;
    a.tickNow = 0u;

    uint start = (salt * 97u) % SWARM_MAX_AREAS;
    for (uint k = 0; k < SWARM_MAX_AREAS; ++k)
    {
        uint slot = (start + k) % SWARM_MAX_AREAS;
        uint was;
        InterlockedCompareExchange(areaStates[slot], SWARM_DEAD, SWARM_ALIVE, was);
        if (was == SWARM_DEAD)
        {
            areas[slot] = a;
            return;
        }
    }
}
#endif

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
    float g_HitStun; // seconds an enemy freezes after a hit (HitCS sets animIndex = 2)
    float g_HitFlash; // vertex color gain at the start of the stun, decays to 1
};

// ============================================================
// Exp orb tuning. Default register b2. HitCS (spawns orbs) and
// OrbMoveCS (moves / picks them up) both read it.
// Must match Swarm::OrbCB in SwarmTypes.h
// ============================================================
#ifndef SWARM_ORB_CB_REG
#define SWARM_ORB_CB_REG b2
#endif

cbuffer SwarmOrbCB : register(SWARM_ORB_CB_REG)
{
    float g_OrbAttractRadius; // start pulling toward the player inside this
    float g_OrbPickupRadius; // consumed inside this
    float g_OrbAccel; // pull acceleration
    float g_OrbMaxSpeed;

    float g_OrbAmount; // exp per orb (global for now; per-enemy later)
    float g_OrbY; // orbs float at this height
    float2 _orbPad;
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

// ============================================================
// Terrain height field (GridWorld::Heights). SWARM_HEIGHT_SUB cells
// per grid cell, 0 on flat ground, the walkable surface height on
// ramps. Enemies pin y to it (MoveCS), projectiles explode when they
// fly below it (ProjMoveCS). Must match GridWorld::kHeightSub
// ============================================================
static const uint SWARM_HEIGHT_SUB = 4u;

float SwarmHeightCell(StructuredBuffer<float> heights, int hx, int hz)
{
    // single exit (fxc X4000 otherwise)
    int hw = (int) (g_GridW * SWARM_HEIGHT_SUB);
    int hd = (int) (g_GridD * SWARM_HEIGHT_SUB);
    bool inside = (hx >= 0 && hx < hw && hz >= 0 && hz < hd);
    return inside ? heights[clamp(hz, 0, hd - 1) * hw + clamp(hx, 0, hw - 1)] : 0.0;
}

// bilinear sample at a world xz (same formula as GridWorld::SampleHeight)
float SwarmTerrainHeight(StructuredBuffer<float> heights, float2 xz)
{
    float s = g_CellSize / (float) SWARM_HEIGHT_SUB;
    float fx = (xz.x - g_GridOrigin.x) / s - 0.5;
    float fz = (xz.y - g_GridOrigin.z) / s - 0.5;
    int ix = (int) floor(fx);
    int iz = (int) floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    float h00 = SwarmHeightCell(heights, ix, iz), h10 = SwarmHeightCell(heights, ix + 1, iz);
    float h01 = SwarmHeightCell(heights, ix, iz + 1), h11 = SwarmHeightCell(heights, ix + 1, iz + 1);
    return lerp(lerp(h00, h10, tx), lerp(h01, h11, tx), tz);
}

#endif