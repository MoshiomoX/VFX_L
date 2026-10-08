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
// Enemy kind + per-kind state: 8 bytes, a side buffer with the same
// index as the enemy pool (SwarmEnemy stays 48B).
// Must match Swarm::EnemyExtra in SwarmTypes.h
// A spawn REQUEST carries the kind in animIndex; SpawnEnemyCS /
// RecycleCS move it here and reset animIndex to 0.
// ============================================================
static const uint SWARM_KIND_MOB = 0u;    // melee on contact
static const uint SWARM_KIND_BOMBER = 1u; // contact lights a fuse, blows up g_BomberFuseTime later
static const uint SWARM_KIND_ELITE = 2u;  // big mob: g_EliteScale body, g_EliteDamageMul melee, g_EliteExpMul orb
static const uint SWARM_KIND_BOSS = 3u;   // stage boss: g_Boss*, never hit-stunned, reported in SwarmBossInfo
static const uint SWARM_KIND_GHOST = 4u;  // final-swarm ghost: mob hp, fast, flies straight through walls and plateaus (2026-09-30)
static const uint SWARM_KIND_SPLITTER = 5u;  // splits into 3 splitlings on death (SwarmCorpseTrackCS -> split ring -> CPU) (2026-10-03)
static const uint SWARM_KIND_SPLITLING = 6u; // small child of a splitter, does not split again
static const uint SWARM_KIND_BRUTE = 7u;     // heavy mob: 3x hp, slow, big; replaces mobs from minute 4 (2026-10-07)
static const uint SWARM_KIND_CHARGER = 8u;   // stops, winds up (red band on the ground), dashes in a locked line (2026-10-08)
static const uint SWARM_KIND_SHIELD = 9u;    // every hit loses g_ShieldArmor (at least g_ShieldMinFrac goes through) (2026-10-08)

struct SwarmEnemyExtra
{
    uint kind;
    // bomber : seconds since the fuse was lit. 0 = not lit (lighting adds one step)
    // charger: < 0 = cooldown left (counts up to 0), 0 = chasing, > 0 = seconds since the wind-up began
    //          (SwarmChargerPhase splits it into wind-up / dash / recover)
    float fuse;
};

// ------------------------------------------------------------
// Enemy status side buffer enemySlow (float4, 2026-10-08; was float2):
//   x = slow seconds left, y = slow amount (poison pool, AreaDamageCS)
//   z = frozen seconds left (> 0: does not move, does not hit)
//   w = while frozen: the length of this freeze (the VS grows / melts the ice);
//       after it thaws: seconds left before it can freeze again
// A freeze is requested by HitCS: animIndex = SWARM_ANIM_FREEZE_REQ and
// animTime = seconds. The next step's AICS takes it (SwarmEnemy stays 48B).
// Must match Swarm::kAnimFreezeRequest
// ------------------------------------------------------------
static const uint SWARM_ANIM_FREEZE_REQ = 3u;

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
    uint motion; // bits 0-15 = motion table index, 16-30 = size scale (SwarmProjScale).
                 // In a spawn REQUEST bit 31 = mirror the curve
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
//
// DROP (meteor) uses c1 differently, in meters:
//     c1.x = height of the start point above the impact point
//     c1.y = horizontal offset of the start point, back toward the muzzle
//            (so it falls at atan(c1.x / c1.y) from the horizontal)
// The impact point is fixed at spawn (the enemy's position then) so the
// warning ring on the ground (SwarmDropRingVS) is exactly where it lands.
// ============================================================
static const uint SWARM_MOTION_STRAIGHT = 0;
static const uint SWARM_MOTION_CURVE_ONCE = 1;
static const uint SWARM_MOTION_TRACK = 2;
static const uint SWARM_MOTION_DROP = 3;
// LOB (2026-10-01, the Poison spell): thrown from the muzzle in an arc onto
// the spot where the nearest enemy stood at spawn. Built like a CURVE path
// (c1 / c2 in the shot frame, so c.z lifts the arc) but behaves like DROP:
// no enemy hits on the way, no terrain test, always lands on p3 and leaves
// its hit area there.
static const uint SWARM_MOTION_LOB = 4;

bool SwarmMotionLands(uint mode)
{
    return mode == SWARM_MOTION_DROP || mode == SWARM_MOTION_LOB;
}

static const uint SWARM_NO_TARGET = 0xFFFFFFFFu;
static const uint SWARM_MOTION_INDEX_MASK = 0xFFFFu;
static const uint SWARM_MOTION_FLIP_BIT = 0x80000000u;

// ------------------------------------------------------------
// Trigger chain (basic spell -> advanced spell, e.g. fireball / stone
// shot -> meteor). A spawn request may carry a parallel uint2
// (SwarmSpawnProjCS t4):
//   x = trigger tag: bit k = "when this projectile ends, advanced spell
//       k of the wand may fire there". Kept per slot in projTags.
//   y = flags: SWARM_SPAWN_AT_POS = DROP lands on the request position
//       instead of locking the enemy nearest to the player.
// SwarmProjEndCS reports tagged projectiles that died this step into a
// ring (header 16B: [0] = total events ever written, then
// SWARM_MAX_TRIGGER_EVENTS entries of 16B: ground pos xyz + tag).
// The CPU reads the ring back and takes the entries it has not seen.
// Must match Swarm::kMaxTriggerEvents / kSpawnAtPos in SwarmTypes.h
// ------------------------------------------------------------
static const uint SWARM_MAX_TRIGGER_EVENTS = 128u;
static const uint SWARM_SPAWN_AT_POS = 1u;

// ------------------------------------------------------------
// Boost on the areas a projectile leaves (2026-10-02). y bits 8-19 =
// damage multiplier, bits 20-31 = duration multiplier, 256 = 1.0 (0 reads
// as 1.0). SpawnProjCS keeps y >> SWARM_SPAWN_BOOST_SHIFT per slot in
// projBoost; HitCS / ProjMoveCS hand it to SwarmSpawnAreaFromDef.
// Damage = the "spell power" stat card, duration = shots cast during a
// mana surge (only lasting areas get longer).
// Must match Swarm::kSpawnBoostShift / PackSpawnBoost in SwarmTypes.h
// ------------------------------------------------------------
static const uint SWARM_SPAWN_BOOST_SHIFT = 8u;

// ------------------------------------------------------------
// A dead enemy shattering into its rigid parts (2026-10-02).
// SwarmCorpseTrackCS writes one when a slot that was alive last frame is
// DEAD now; SwarmCorpseVS throws the parts as a closed-form function of
// (seed, age). Must match Swarm::Corpse / kMaxCorpses in SwarmTypes.h
// ------------------------------------------------------------
static const uint SWARM_MAX_CORPSES = 512u;

struct SwarmCorpse
{
    float3 position; // where it died (enemy position = ground + groundY)
    float yaw;
    float2 dir;      // throw direction, unit xz (away from the player)
    float birth;     // time of death on SwarmSystem's clock, 0 = empty
    uint kind;
    uint seed;
    uint cause;      // reserved: cause of death (fire / poison / lightning variants), 0 for now
    float2 _pad;
};

float SwarmBoostDamage(uint boost)
{
    uint q = boost & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

float SwarmBoostDuration(uint boost)
{
    uint q = (boost >> 12) & 0xFFFu;
    return (q == 0u) ? 1.0 : (float) q / 256.0;
}

// ------------------------------------------------------------
// Size scale of one projectile / area (the Magnifier item).
// A projectile's scale = its hit radius / its profile's radius
// (SwarmMotion.baseRadius), worked out once in SwarmSpawnProjCS and
// kept in bits 16-30 of SwarmProjectile.motion (every reader masks the
// index with SWARM_MOTION_INDEX_MASK). An area keeps its scale in bits
// 16-31 of SwarmArea.flags. Fixed point, 1024 = 1.0; 0 = 1.0 (spawn
// requests from the CPU leave the bits empty).
// Multiplied into: the hit area's radius, particle size / offset /
// shape / speed (SwarmEmitCS), point light radius, sprite size, the
// meteor's warning ring.
// ------------------------------------------------------------
static const uint SWARM_SCALE_SHIFT = 16u;
static const float SWARM_SCALE_ONE = 1024.0;

uint SwarmPackScale(float scale, uint maxQ)
{
    return (uint) clamp(round(scale * SWARM_SCALE_ONE), 1.0, (float) maxQ);
}
float SwarmUnpackScale(uint q)
{
    return (q == 0u) ? 1.0 : (float) q / SWARM_SCALE_ONE;
}

// size scale of a live projectile (1 = as authored)
float SwarmProjScale(SwarmProjectile p)
{
    return SwarmUnpackScale((p.motion >> SWARM_SCALE_SHIFT) & 0x7FFFu);
}

struct SwarmMotion
{
    uint mode;
    float retargetRadius; // TRACK only. <= 0 = unlimited
    uint hitArea; // area def spawned where the projectile hits. 0 = none
    uint hitAreaFlags; // bit 0 = also spawn when it expires / hits a wall

    float3 c1;
    float baseRadius; // the profile's hit radius: a shot bigger than this is drawn bigger

    float3 c2;
    float freezeTime; // > 0: the enemy this hits freezes for this long (Ice Lance, 2026-10-08; was _pad2)
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

    // the side offset (c.y) is rolled around the flight axis by path.sideSign (radians):
    // 0 = right, pi = left, other = random direction per shot (2026-10-06). c.z stays world-up
    float3 lateral = side * cos(path.sideSign) + cross(fwd, side) * sin(path.sideSign);

    path.p0 = from;
    path.p3 = to;
    path.p1 = from + fwd * (m.c1.x * dist)
                   + lateral * (m.c1.y * dist)
                   + up * (m.c1.z * dist);
    path.p2 = from + fwd * (m.c2.x * dist)
                   + lateral * (m.c2.y * dist)
                   + up * (m.c2.z * dist);
    if (keepHeading)
        path.p1 = from + heading * (dist / 3.0);

    // arc length ~ average of the chord and the control polygon
    float len = 0.5 * (dist + length(path.p1 - path.p0)
                            + length(path.p2 - path.p1)
                            + length(path.p3 - path.p2));
    path.duration = max(len / max(path.speed, 0.01), SWARM_FIXED_STEP);
}

// DROP: a straight line from start to impact. The handles sit on the line
// so that the distance covered is s(t) = 0.4 t + 0.6 t^2: it speeds up as
// it falls (1.6x the nominal speed at impact, the nominal speed on average).
// path.speed must be set by the caller.
void SwarmBuildDropPath(inout SwarmProjPath path, float3 start, float3 impact)
{
    float3 d = impact - start;
    path.p0 = start;
    path.p1 = start + d * (0.4 / 3.0);
    path.p2 = start + d * ((2.0 * 0.4 + 0.6) / 3.0);
    path.p3 = impact;
    path.duration = max(length(d) / max(path.speed, 0.01), SWARM_FIXED_STEP);
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
// Capsule (beam): the area is the segment centre -> areaEnds[slot] with
// radius around it. Start / end / radius come from SwarmBeamCB every step
// (AreaTickCS), channel = bits 8-11 of flags. Only the CPU spawns these.
static const uint SWARM_AREA_CAPSULE = 4u;
// shakes the camera when it appears (explosions; = Swarm::kAreaShake, 2026-10-03).
// Counted by SwarmLiquidTrackCS, which already spots every newly born area
static const uint SWARM_AREA_SHAKE = 8u;
static const uint SWARM_AREA_BEAM_SHIFT = 8u; // (flags >> shift) & 0xF = beam channel
static const uint SWARM_MAX_BEAMS = 4u; // = Swarm::kMaxBeams

// Slow (2026-10-01, the Poison pool): bits 12-15 of flags = q, a tick slows
// the enemies inside by q / 15 (0 = none). AreaDamageCS keeps the strongest
// slow per enemy in enemySlow[slot] = (seconds left, amount) and refreshes it
// on every tick, so it holds while the enemy stands in the pool and wears off
// SWARM_SLOW_LINGER after the next tick it misses. AICS scales moveSpeed by
// (1 - amount) and counts the seconds down. Elites / the boss take half.
static const uint SWARM_AREA_SLOW_SHIFT = 12u;
static const float SWARM_SLOW_LINGER = 0.15;
static const float SWARM_SLOW_BIG_MUL = 0.5;

float SwarmAreaSlow(uint flags)
{
    return (float) ((flags >> SWARM_AREA_SLOW_SHIFT) & 0xFu) / 15.0;
}

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
    uint flags; // bits 0-15 = SWARM_AREA_*, 16-31 = size scale (SwarmAreaScale)
    uint vfxType; // recipe index for GPU-side particles. 0 = none (the CPU plays the VFX)
    uint tickNow; // 1 while this step deals damage. written by AreaTickCS
};

// size scale of an area (1 = as authored). radius / halfHeight already include it
float SwarmAreaScale(SwarmArea a)
{
    return SwarmUnpackScale(a.flags >> SWARM_SCALE_SHIFT);
}

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
// Optional: the direction the spawner was travelling, per area slot
// (xy = unit xz, w = 1 "fresh"). SwarmLiquidTrackCS reads it once when it
// first sees the area and clears w. A shader with a UAV to spare #defines
// SWARM_AREA_DIR_U (ProjMoveCS: a LOB poison flask lands there). HitCS has
// no UAV left, so its areas carry no direction (a liquid picks an angle)
#ifdef SWARM_AREA_DIR_U
RWStructuredBuffer<float4> areaDirs : register(SWARM_AREA_DIR_U);
#endif

// Same CAS scan as the other pools. Pool full -> no area (degrade, never corrupt).
// scale: the size scale of whatever spawned it (a projectile's SwarmProjScale, 1 = as authored)
// boost: the projectile's projBoost (SwarmBoostDamage / SwarmBoostDuration), 0 = none.
//        The duration part only stretches lasting areas (a one-shot ticks once anyway)
// dir  : the spawner's velocity in xz (any length). Kept only with SWARM_AREA_DIR_U
void SwarmSpawnAreaFromDef(uint defId, float3 pos, uint salt, float scale, uint boost, float2 dir)
{
    if (defId == 0u)
        return;

    SwarmAreaDef d = areaDefs[defId];
    bool lasting = d.tickInterval < d.duration;

    SwarmArea a = (SwarmArea) 0;
    a.center = pos;
    a.radius = d.radius * scale;
    a.damage = d.damage * SwarmBoostDamage(boost);
    a.timeLeft = d.duration * (lasting ? SwarmBoostDuration(boost) : 1.0);
    a.tickInterval = d.tickInterval;
    a.tickTimer = 0.0;
    a.halfHeight = d.halfHeight * scale;
    a.flags = (d.flags & 0xFFFFu & ~SWARM_AREA_FOLLOW_PLAYER) // born from a hit: stays where it is
        | (SwarmPackScale(scale, 0xFFFFu) << SWARM_SCALE_SHIFT);
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
#ifdef SWARM_AREA_DIR_U
            float dirLenSq = dot(dir, dir);
            areaDirs[slot] = float4((dirLenSq > 1e-8) ? dir * rsqrt(dirLenSq) : float2(0.0, 0.0), 0.0, 1.0);
#endif
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
    uint spriteStart; // SwarmSpriteDef table (GPU areas only, was _pad)
    uint spriteCount;
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
// Bomber tuning. Opt-in: a shader that needs it #defines
// SWARM_BOMBER_CB_REG (ContactCS b3, the enemy VS b5).
// Must match Swarm::BomberCB in SwarmTypes.h
// ============================================================
#ifdef SWARM_BOMBER_CB_REG
cbuffer SwarmBomberCB : register(SWARM_BOMBER_CB_REG)
{
    float g_BomberFuseTime; // lit -> explode, seconds
    float g_BomberTriggerMargin; // lights inside (radius sum + this)
    float g_BomberBlastRadius; // the player capsule inside this at the blast takes damage
    float g_BomberBlastDamage;

    uint g_BomberBlastArea; // area def spawned for the look (damage 0). 0 = none
    float g_BomberSwell; // VS: grows to (1 + this) right before the blast
    float g_BomberFlashGain; // VS: blink brightness
    float _bomberPad;

    // ---- elite (SWARM_KIND_ELITE). Shares this cbuffer: every pass that
    // needs the kind rules already binds it ----
    float g_EliteScale; // body size (model, hit / contact radius, HP bar height)
    float g_EliteDamageMul; // melee damage = g_ContactDamage * this
    float g_EliteExpMul; // its orb is worth g_OrbAmount * this
    float _elitePad;

    // ---- stage boss (SWARM_KIND_BOSS) ----
    float g_BossScale;
    float g_BossDamageMul;
    float g_BossExpMul;
    float _bossPad;

    // ---- final-swarm ghost (SWARM_KIND_GHOST) ----
    float g_GhostHover; // VS: floats this much above the ground
    float g_GhostAlpha; // VS: vertex alpha (drawn with alpha blend)
    float g_GhostGlow; // VS: HDR gain on the cyan tint
    float g_GhostDamageMul; // melee damage = g_ContactDamage * this

    // ---- splitter (SWARM_KIND_SPLITTER) and its splitlings (2026-10-03) ----
    float g_SplitterScale;
    float g_SplitterDamageMul;
    float g_SplitterExpMul;
    float _splitterPad;
    float g_SplitlingScale;
    float g_SplitlingDamageMul;
    float g_SplitlingExpMul;
    float _splitlingPad;

    // ---- boss charge (2026-10-07, BossAttacks writes it every frame) ----
    // while g_BossChargeOn > 0.5 the boss ignores the flow field / chase and moves
    // straight along g_BossChargeDir * g_BossChargeSpeed (speed 0 = winding up in place).
    // Walls still stop it; it does not stop at the player (runs through)
    float2 g_BossChargeDir;
    float g_BossChargeSpeed;
    float g_BossChargeOn;

    // ---- brute (SWARM_KIND_BRUTE, 2026-10-07) ----
    float g_BruteScale;
    float g_BruteDamageMul;
    float g_BruteExpMul;
    float _brutePad;

    // ---- charger (SWARM_KIND_CHARGER, 2026-10-08) ----
    float g_ChargerScale;
    float g_ChargerDamageMul;     // plain melee
    float g_ChargerExpMul;
    float g_ChargerDashDamageMul; // hitting the player during the dash (knocked back like a blast)
    float g_ChargerWindup;        // seconds standing still with the band on the ground
    float g_ChargerDashSpeed;
    float g_ChargerDashDist;
    float g_ChargerRecover;       // seconds standing still after the dash
    float g_ChargerCooldown;      // seconds of plain chasing before the next wind-up
    float g_ChargerMinDist;       // starts a wind-up with the player this far .. g_ChargerMaxDist away
    float g_ChargerMaxDist;
    float g_ChargerGlow;          // VS: red blink during the wind-up

    // ---- shield bearer (SWARM_KIND_SHIELD, 2026-10-08) ----
    float g_ShieldScale;
    float g_ShieldDamageMul;
    float g_ShieldExpMul;
    float g_ShieldArmor;          // taken off every hit (one projectile, one area tick)

    float g_ShieldMinFrac;        // ... but at least this fraction of it goes through
    // ---- freeze (Ice Lance, 2026-10-08) ----
    float g_FreezeBigMul;         // elites / the boss freeze this much shorter
    float g_FreezeImmunity;       // seconds after a thaw with no new freeze
    float g_FreezeTint;           // VS: how icy a frozen body looks (0 = no tint)
};

// body size multiplier of a kind (radius and model)
float SwarmKindScale(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteScale
         : (kind == SWARM_KIND_BOSS) ? g_BossScale
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterScale
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingScale
         : (kind == SWARM_KIND_BRUTE) ? g_BruteScale
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerScale
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldScale : 1.0;
}

// melee damage multiplier of a kind (times g_ContactDamage)
float SwarmKindDamageMul(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteDamageMul
         : (kind == SWARM_KIND_BOSS) ? g_BossDamageMul
         : (kind == SWARM_KIND_GHOST) ? g_GhostDamageMul
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterDamageMul
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingDamageMul
         : (kind == SWARM_KIND_BRUTE) ? g_BruteDamageMul
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerDamageMul
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldDamageMul : 1.0;
}

// exp orb value multiplier of a kind (times g_OrbAmount)
float SwarmKindExpMul(uint kind)
{
    return (kind == SWARM_KIND_ELITE) ? g_EliteExpMul
         : (kind == SWARM_KIND_BOSS) ? g_BossExpMul
         : (kind == SWARM_KIND_SPLITTER) ? g_SplitterExpMul
         : (kind == SWARM_KIND_SPLITLING) ? g_SplitlingExpMul
         : (kind == SWARM_KIND_BRUTE) ? g_BruteExpMul
         : (kind == SWARM_KIND_CHARGER) ? g_ChargerExpMul
         : (kind == SWARM_KIND_SHIELD) ? g_ShieldExpMul : 1.0;
}

// damage one hit (a projectile, one area tick) really does to this kind.
// The shield bearer takes g_ShieldArmor off every hit, so many small ticks
// (poison pool, beam) barely scratch it and one heavy hit works (2026-10-08)
float SwarmArmorDamage(uint kind, float dmg)
{
    return (kind == SWARM_KIND_SHIELD && dmg > 0.0)
        ? max(dmg - g_ShieldArmor, dmg * g_ShieldMinFrac) : dmg;
}

// ---- charger phases, from SwarmEnemyExtra.fuse ----
static const uint SWARM_CHARGE_CHASE = 0u;   // fuse <= 0 (cooldown counts up to 0)
static const uint SWARM_CHARGE_WINDUP = 1u;
static const uint SWARM_CHARGE_DASH = 2u;
static const uint SWARM_CHARGE_RECOVER = 3u;

float SwarmChargerDashTime()
{
    return g_ChargerDashDist / max(g_ChargerDashSpeed, 0.1);
}

uint SwarmChargerPhase(float fuse)
{
    float w = g_ChargerWindup;
    float d = w + SwarmChargerDashTime();
    return (fuse <= 0.0) ? SWARM_CHARGE_CHASE
         : (fuse <= w) ? SWARM_CHARGE_WINDUP
         : (fuse <= d) ? SWARM_CHARGE_DASH : SWARM_CHARGE_RECOVER;
}

// Body capsule of an enemy of this kind. The model is scaled about its
// feet, so a bigger body also has its centre higher up.
// pos = SwarmEnemy.position (the capsule centre of a normal mob)
void SwarmKindCapsule(uint kind, float3 pos, out float3 center, out float radius, out float halfLen)
{
    float k = SwarmKindScale(kind);
    radius = g_EnemyRadius * k;
    halfLen = g_EnemyCapsuleHalf * k;
    center = pos;
    center.y += (k - 1.0) * (g_EnemyRadius + g_EnemyCapsuleHalf);
}
#endif

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
// Enemy spatial hash. Same cells as the terrain grid (g_CellSize).
// SwarmEnemyBinCS fills it every step: cellCount[cell] = live enemies
// in the cell, cellItems[cell * CAP + k] = their slot indices. Cells
// past CAP entries drop the rest (readers clamp with min()). With a
// 2m cell and 0.4m enemies a packed cell holds ~7, so 32 leaves room
// for the crush the push pass is there to undo.
// AI (separation) and PushCS (overlap resolve) only look at 3x3 cells.
// ============================================================
static const uint SWARM_BUCKET_CAP = 32u;

// cell index of a world position, or -1 outside the grid
int SwarmCellOf(float3 p)
{
    int gx = (int) floor((p.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((p.z - g_GridOrigin.z) / g_CellSize);
    bool inside = (gx >= 0 && gx < (int) g_GridW && gz >= 0 && gz < (int) g_GridD);
    return inside ? (gz * (int) g_GridW + gx) : -1;
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

// ============================================================
// Cliffs. Plateau cells are walkable (enemies roam on top) and carry
// their height in the height field, so the grid alone would let an
// enemy slide straight up a plateau side. A plateau side is a jump of
// several meters inside one 0.5m height cell, a ramp is <= 30 degrees:
// any move steeper than SWARM_MAX_WALK_SLOPE (rise / run) is refused.
// Must match FlowField::maxStep (per 2m cell) and the generator's ramps
// ============================================================
static const float SWARM_MAX_WALK_SLOPE = 0.84; // tan(40 deg)

bool SwarmSlopeOk(StructuredBuffer<float> heights, float2 from, float2 to)
{
    float run = length(to - from);
    float rise = abs(SwarmTerrainHeight(heights, to) - SwarmTerrainHeight(heights, from));
    return rise <= SWARM_MAX_WALK_SLOPE * run + 0.05; // + slack for the bilinear kinks
}

// ============================================================
// Dropping off a plateau (2026-10-01). An enemy on higher ground than
// the player may step DOWN a cliff (it falls, MoveCS applies gravity);
// climbing one is still refused. Only while the player stands at least
// SWARM_DROP_MIN_BELOW lower: with the player up on the same plateau
// the cliffs stay walls both ways. FlowField (CPU) uses the same rule,
// so the field never routes an enemy off an edge the GPU will not let
// it take. The crowd push (PushCS) never drops anyone.
// ============================================================
static const float SWARM_DROP_MIN_BELOW = 1.0;

bool SwarmDropAllowed(StructuredBuffer<float> heights, float2 enemyXZ)
{
    return g_PlayerAlive != 0u
        && SwarmTerrainHeight(heights, g_PlayerPos.xz) <= SwarmTerrainHeight(heights, enemyXZ) - SWARM_DROP_MIN_BELOW;
}

// SwarmSlopeOk, but a drop of any height passes when allowDrop
bool SwarmStepHeightOk(StructuredBuffer<float> heights, float2 from, float2 to, bool allowDrop)
{
    float rise = SwarmTerrainHeight(heights, to) - SwarmTerrainHeight(heights, from);
    float lim = SWARM_MAX_WALK_SLOPE * length(to - from) + 0.05;
    return rise <= lim && (allowDrop || -rise <= lim);
}

// ============================================================
// Body circle vs walls and cliffs (2026-10-01).
// MoveCS / PushCS used to check the centre only, so the crowd pushed
// enemies up to the wall face with half the body inside a plateau side
// (self test "clip": 0.37m of a 0.4m radius, elites 0.73m of 0.88m).
// The body is sampled at 8 points on a circle; a point counts as
// touching when its cell is blocked, or the ground there is a cliff
// (higher / lower than the centre by more than a walkable slope plus
// SWARM_BODY_CLIFF_RISE). The heights are the raw 0.5m height cells,
// not the bilinear sample: plateau sides sit on cell edges, so the step
// is exactly at the face (the bilinear one starts rising 0.25m before
// it). The margin covers the stair steps of a ramp (~0.27m per cell).
// Returns xy = sum of the directions of the touching points (the push
// out goes the other way), z = how many touch.
// ============================================================
static const float SWARM_BODY_CLIFF_RISE = 0.35;
// slope allowed across the body: the steepest ramp is ~30 deg, so this
// can be tighter than SWARM_MAX_WALK_SLOPE (40 deg) and still catch a
// ~1m ledge under an elite's 0.9m radius
static const float SWARM_BODY_MAX_SLOPE = 0.62; // tan(32 deg)

// height of the 0.5m height cell that contains xz (no blending)
float SwarmTerrainHeightRaw(StructuredBuffer<float> heights, float2 xz)
{
    float s = g_CellSize / (float) SWARM_HEIGHT_SUB;
    return SwarmHeightCell(heights, (int) floor((xz.x - g_GridOrigin.x) / s),
                                    (int) floor((xz.y - g_GridOrigin.z) / s));
}
// rest radius against walls in units of the collision radius: the mesh
// is wider than the capsule (~0.9m vs 0.8m), same reason as PushCS
static const float SWARM_BODY_WALL_MUL = 1.15;
// cap so the big kinds still fit a 1-cell (2m) gap between walls:
// the flow field routes through those and the boss would get stuck
static const float SWARM_BODY_WALL_MAX = 0.9;

// countDrops = false: lower ground does not count (an enemy dropping off
// an edge may hang its body over it), only walls and rising faces do.
// footH: the height the rises are measured from (the enemy's feet, terrain
// space). Default = the ground under the centre. MoveCS passes the real
// feet: an enemy stepping off an edge has its centre over the low ground
// already while its feet are still level with the plateau behind it,
// which must not count as a wall (it used to stop them right at the edge)
float3 SwarmBodyContact(StructuredBuffer<uint> walkable, StructuredBuffer<float> heights, float2 c, float radius,
                        bool countDrops = true, float footH = -1e30)
{
    float hc = (footH > -1e29) ? footH : SwarmTerrainHeightRaw(heights, c);
    float maxRise = SWARM_BODY_MAX_SLOPE * radius + SWARM_BODY_CLIFF_RISE;
    float3 acc = float3(0, 0, 0);
    [unroll]
    for (int k = 0; k < 8; ++k)
    {
        float a = (float) k * 0.7853982;
        float2 d = float2(cos(a), sin(a));
        float2 q = c + d * radius;
        float rise = SwarmTerrainHeightRaw(heights, q) - hc;
        if (!SwarmIsWalkable(walkable, float3(q.x, 0.0, q.y))
            || rise > maxRise || (countDrops && -rise > maxRise))
            acc += float3(d, 1.0);
    }
    return acc;
}

// SwarmSlopeOk on the raw height cells. Used for the step that eases a
// body out of a cliff face: within 0.25m of the face the bilinear height
// already climbs the cliff, so stepping away from it looks like a steep
// drop to SwarmSlopeOk and an enemy spawned there could never leave
bool SwarmRawSlopeOk(StructuredBuffer<float> heights, float2 from, float2 to)
{
    float rise = abs(SwarmTerrainHeightRaw(heights, to) - SwarmTerrainHeightRaw(heights, from));
    return rise <= SWARM_MAX_WALK_SLOPE * length(to - from) + SWARM_BODY_CLIFF_RISE;
}

#ifdef SWARM_BOMBER_CB_REG
// the body radius used against walls (bigger kinds = bigger, capped)
float SwarmBodyWallRadius(uint kind)
{
    return min(g_EnemyRadius * SwarmKindScale(kind) * SWARM_BODY_WALL_MUL, SWARM_BODY_WALL_MAX);
}
#endif

#endif
