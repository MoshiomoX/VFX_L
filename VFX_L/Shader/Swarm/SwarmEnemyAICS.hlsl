// ============================================================
// SwarmEnemyAICS.hlsl
// One thread per enemy slot. Decide this step's velocity:
//   seek (flow field toward the player, straight line when close)
//   + separation (3x3 spatial hash cells) + avoid (soft push from
//   blocked cells) + hard block (kill the axis that would enter a
//   blocked cell)
//
// Writes velocity ONLY. Position is integrated by SwarmEnemyMoveCS
// in a separate dispatch, so every thread here reads a consistent
// snapshot of positions -- same reason the CPU version copies all
// positions into a vector first.
//
// Logic mirrors ChaseAISystem.cpp. Keep them in sync.
// + player block (solid circle, slide around it)
// Elites (bigger body) stop farther out: SwarmKindScale.
//
// Freeze (Ice Lance, 2026-10-08): HitCS leaves a request in animIndex
// (SWARM_ANIM_FREEZE_REQ, animTime = seconds); this pass turns it into
// enemySlow.z and keeps the frozen enemy still.
// Charger (2026-10-08): the only writer of a charger's enemyExtra.fuse
// (wind-up -> dash -> recover -> cooldown, see SwarmChargerPhase). The dash
// direction is the yaw set when the wind-up starts (MoveCS only turns the
// yaw toward a non-zero velocity, so it holds while the charger stands).
// ============================================================
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<uint> terrain : register(t1);
StructuredBuffer<uint> cellCount : register(t2);   // spatial hash (SwarmEnemyBinCS)
StructuredBuffer<uint> cellItems : register(t3);
StructuredBuffer<float2> flowField : register(t4); // per-cell direction to the player (CPU FlowField)
StructuredBuffer<float> terrainHeight : register(t6);         // cliffs block like walls (SwarmSlopeOk)
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
// slow (x seconds left, y amount; AreaDamageCS) + freeze (z seconds left, w length / immunity), counted down here
RWStructuredBuffer<float4> enemySlow : register(u1);
// kind + fuse. ContactCS lights a bomber's fuse; this pass runs a charger's
RWStructuredBuffer<SwarmEnemyExtra> enemyExtra : register(u2);
// the split ring (SwarmCorpseTrackCS writes a splitter's death here): a sand mage that finishes its
// wind-up writes its position + SWARM_KIND_SANDMAGE, the CPU spawns the tornado toward the player (2026-10-17)
RWByteAddressBuffer castEvents : register(u3);
static const uint SWARM_MAX_SPLIT_EVENTS = 128u;   // Swarm::kMaxSplitEvents

// one entry on the split ring: position + kind (low 16 bits) | what happened (high 16 bits, 2026-10-25)
void CastEvent(float3 pos, uint word)
{
    uint s;
    castEvents.InterlockedAdd(0, 1u, s);
    uint at = 16u + (s % SWARM_MAX_SPLIT_EVENTS) * 16u;
    castEvents.Store3(at, asuint(pos));
    castEvents.Store(at + 12u, word);
}

// a caster (sand mage / necromancer / chain warden, 2026-10-25 shared): fuse 0 = ready -> > 0 = wind-up (counts up,
// stands still, faces the player locked at the start) -> at `windup` the cast event (the CPU does the rest) -> < 0 =
// cooldown (counts up to 0). Starts only with the player on our level and within `range`; moveSpeed 0 = training
// target: never casts. withYaw: the event carries the locked yaw in its high 16 bits (the warden's chain line)
float CasterStep(uint i, float3 pos, float f, float windup, float cooldown, float range, uint kind, bool withYaw)
{
    if (f < 0.0)
        f = min(f + g_Step, 0.0);
    else if (f > 0.0)
    {
        f += g_Step;
        if (f >= windup)
        {
            uint word = kind;
            if (withYaw)
                word |= ((uint) (frac(enemies[i].yaw / 6.2831853 + 1.0) * 65535.0 + 0.5) & 0xFFFFu) << 16;
            CastEvent(pos, word);
            f = -cooldown;
        }
    }
    if (f == 0.0 && g_PlayerAlive != 0u && enemies[i].animIndex != 2u && enemies[i].moveSpeed > 0.01
        && abs(g_PlayerPos.y - pos.y) < 1.5)
    {
        float2 toP = g_PlayerPos.xz - pos.xz;
        if (dot(toP, toP) <= range * range)
        {
            f = g_Step;
            enemies[i].yaw = atan2(toP.x, toP.y);   // faces the player while winding up (the warden's chain goes this way)
        }
    }
    return f;
}

// a charger only winds up if the ground along the dash is open up to the player
// (walkable, no cliff): it would just ram the wall otherwise
bool ChargeLineClear(float3 pos, float2 dir, float dist)
{
    float2 prev = pos.xz;
    bool ok = true;
    int n = (int) ceil(dist);
    for (int k = 1; k <= n && ok; ++k)
    {
        float2 q = pos.xz + dir * min((float) k, dist);
        ok = SwarmIsWalkable(terrain, float3(q.x, 0.0, q.y))
          && SwarmStepHeightOk(terrainHeight, prev, q, false);
        prev = q;
    }
    return ok;
}

// The hard block's probe (2026-10-20): the probe point is open when its cell is walkable, the
// slope from here is walkable (a cliff = wall, a drop is open only with allowDrop) AND the body
// circle there touches nothing (SwarmBodyContact, the same test MoveCS applies to every step).
// The centre-line slope test alone, over a look-ahead of 1.5-2 m, passed the 0.6 m ledge along
// a terrace ramp's side and the corner of a plateau cell (its cell is walkable), so the AI kept
// pushing into what MoveCS then refused every step (soak on the step terraces).
// Rises are measured from the probe's own ground (walking up a ramp is no wall); with a drop
// open, from the feet if higher (stepping off an edge: the plateau behind is no wall, and the
// low ground under the circle does not count)
bool ProbeBlocked(float3 pos, float2 probe, float r, bool allowDrop, float feet)
{
    if (!SwarmIsWalkable(terrain, float3(probe.x, 0.0, probe.y)))
        return true;
    // bilinear OR raw (same as MoveCS's StepOk): from inside the bilinear dip at a plateau edge
    // the bilinear test alone calls every step back onto the plateau a climb too steep
    if (!SwarmStepHeightOk(terrainHeight, pos.xz, probe, allowDrop) && !SwarmRawStepOk(terrainHeight, pos.xz, probe, allowDrop))
        return true;
    float footH = SwarmTerrainHeightRaw(terrainHeight, probe);
    if (allowDrop)
        footH = max(footH, feet);
    return SwarmBodyContact(terrain, terrainHeight, probe, r, !allowDrop, footH).z > 0.0;
}

// straight-line chase inside this many cells of the player: the flow
// field's per-cell steps would make the ring around the player jitter
static const float kDirectChaseCells = 1.5;

// flow direction at a world xz: bilinear over the 4 nearest cell centres
// so enemies do not snap to a new heading at every cell border.
// (0,0) cells (unreachable / target) pull the blend toward zero, which
// the caller treats as "no path, chase directly".
// Cells on another level (centre more than kFlowLevelStep above / below the
// enemy's ground) are left out (2026-10-20): beside a step terrace the blend
// mixed in the plateau cell's heading - a route the enemy cannot take from
// down here - which steered it into the face / corner, where it twitched
static const float kFlowLevelStep = 1.5;   // = FlowField::maxStep
float2 FlowAt(float2 xz, float groundH)
{
    float fx = (xz.x - g_GridOrigin.x) / g_CellSize - 0.5;
    float fz = (xz.y - g_GridOrigin.z) / g_CellSize - 0.5;
    int ix = (int) floor(fx), iz = (int) floor(fz);
    float tx = fx - ix, tz = fz - iz;
    int w = (int) g_GridW, d = (int) g_GridD;

    float2 sum = float2(0, 0);
    float wsum = 0.0;
    [unroll]
    for (int k = 0; k < 4; ++k)
    {
        int cx = ix + (k & 1), cz = iz + (k >> 1);
        float wgt = ((k & 1) ? tx : 1.0 - tx) * ((k >> 1) ? tz : 1.0 - tz);
        bool inside = (cx >= 0 && cx < w && cz >= 0 && cz < d);
        float2 centre = float2(g_GridOrigin.x + (cx + 0.5) * g_CellSize, g_GridOrigin.z + (cz + 0.5) * g_CellSize);
        bool sameLevel = abs(SwarmTerrainHeight(terrainHeight, centre) - groundH) <= kFlowLevelStep;
        if (inside && sameLevel)
        {
            sum += flowField[cz * w + cx] * wgt;
            wsum += wgt;
        }
    }
    return (wsum > 1e-4) ? sum / wsum : float2(0, 0);
}


[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
        return;

    SwarmEnemyExtra extra = enemyExtra[i];
    bool big = (SwarmIsBigElite(extra.kind) || extra.kind == SWARM_KIND_BOSS);

    // ---- stage boss: how far under the sand it is (sand guardian's burrow, 2026-10-24). The CPU (BossAttacks)
    //      drives g_BossBurrow; the boss's fuse = -burrow so SwarmBuried / the VS see it like a burrower's ----
    if (extra.kind == SWARM_KIND_BOSS)
    {
        extra.fuse = -saturate(g_BossBurrow);
        enemyExtra[i].fuse = extra.fuse;
    }
    // ---- the stage's fixed elites (2026-10-25): the CPU (EliteAttacks) runs the moves through g_EliteCmd. How far it has
    //      vanished goes into fuse (-hide: SwarmUntargetable from half, the VS shrinks it into the ground) ----
    int eliteRow = -1;
    if (SwarmIsFixedElite(extra.kind))
    {
        eliteRow = SwarmEliteEntry(i);
        extra.fuse = (eliteRow >= 0) ? -saturate(g_EliteMove[eliteRow].w) : 0.0;
        enemyExtra[i].fuse = extra.fuse;
    }

    // ---- tornado (2026-10-17): straight along its yaw at moveSpeed until the life runs out. No slow, no freeze,
    //      no separation, no walls (MoveCS treats it like a ghost). fuse: 0 = just spawned, > 0 = seconds left,
    //      < 0 = expired (ContactCS turns the slot DEAD this step) ----
    if (extra.kind == SWARM_KIND_TORNADO)
    {
        float f = extra.fuse;
        if (f == 0.0) f = g_TornadoLife;
        else if (f > 0.0) { f -= g_Step; if (f <= 0.0) f = -1.0; }
        enemyExtra[i].fuse = f;
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        enemies[i].velocity = (f > 0.0) ? float3(ys, 0.0, yc) * enemies[i].moveSpeed : float3(0, 0, 0);
        return;
    }

    // ---- status: slow (poison pool) and freeze (ice). Counted down before any early return ----
    float4 st = enemySlow[i];
    float slowMul = 1.0;
    if (st.x > 0.0)
    {
        slowMul = 1.0 - saturate(st.y);
        st.x -= g_Step;
        if (st.x <= 0.0)
            st.xy = float2(0, 0);
    }
    // a freeze HitCS asked for last step. Ignored while frozen or still immune
    if (enemies[i].animIndex == SWARM_ANIM_FREEZE_REQ)
    {
        float want = enemies[i].animTime * (big ? g_FreezeBigMul : 1.0);
        if (st.z <= 0.0 && st.w <= 0.0 && want > 0.0)
            st.zw = float2(want, want);
        enemies[i].animIndex = 0u;
        enemies[i].animTime = 0.0;
    }
    bool frozen = st.z > 0.0;
    if (frozen)
    {
        st.z -= g_Step;
        if (st.z <= 0.0)
            st.zw = float2(0.0, g_FreezeImmunity); // thawed: a while before the next freeze
    }
    else if (st.w > 0.0)
        st.w = max(st.w - g_Step, 0.0);
    // the final-swarm ghosts are never slowed or frozen (2026-10-18: ice / poison must not hold the swarm off);
    // neither is a lit bomber / dash bomber (it is a bomb now)
    if (SwarmIsGhost(extra.kind) || SwarmInvulnerable(extra))
    {
        slowMul = 1.0;
        frozen = false;
        st = float4(0, 0, 0, 0);
    }
    enemySlow[i] = st;

    if (frozen)
    {
        // a frozen charger forgets its wind-up / dash
        if (extra.kind == SWARM_KIND_CHARGER && extra.fuse > 0.0)
            enemyExtra[i].fuse = -0.5 * g_ChargerCooldown;
        enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
        return;
    }

    float3 pos = enemies[i].position;

    // ---- charger: wind-up -> dash -> recover -> cooldown (fuse) ----
    uint cphase = SWARM_CHARGE_CHASE;
    if (extra.kind == SWARM_KIND_CHARGER)
    {
        float f = extra.fuse;
        if (f < 0.0)
            f = min(f + g_Step, 0.0);
        else if (f > 0.0)
        {
            f += g_Step;
            if (f > g_ChargerWindup + SwarmChargerDashTime() + g_ChargerRecover)
                f = -g_ChargerCooldown;
        }
        // start a wind-up: the player on our level, in range, nothing in the way. The dash
        // direction is locked now (yaw), so the band on the ground shows exactly where it goes
        // (moveSpeed 0 = a training target that must stay put: it never charges)
        if (f == 0.0 && g_PlayerAlive != 0u && enemies[i].animIndex != 2u
            && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.0)
        {
            float2 toP = g_PlayerPos.xz - pos.xz;
            float dist = length(toP);
            if (dist >= g_ChargerMinDist && dist <= g_ChargerMaxDist
                && ChargeLineClear(pos, toP / dist, min(dist + 1.0, g_ChargerDashDist)))
            {
                f = g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);
            }
        }
        cphase = SwarmChargerPhase(f);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
    }

    // ---- burrower: under the sand (fuse <= 0, counts down) -> surfaces next to the player (fuse = g_BurrowSurfaceTime,
    //      counts down) -> digs in again. moveSpeed 0 = training target: stays under (2026-10-17) ----
    bool buried = false;
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        float f = extra.fuse;
        if (f > 0.0)
        {
            f -= g_Step;
            if (f <= 0.0) f = -g_Step;
        }
        else
        {
            f -= g_Step;
            if (g_PlayerAlive != 0u && -f >= g_BurrowMinBuried && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.5)
            {
                float2 toP = g_PlayerPos.xz - pos.xz;
                if (dot(toP, toP) <= g_BurrowSurfaceDist * g_BurrowSurfaceDist)
                    f = g_BurrowSurfaceTime;
            }
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        buried = f <= 0.0;
    }

    // ---- sand mage: ready (fuse 0) -> wind-up (fuse counts up to g_SandMageWindup, stands still) -> cast
    //      (castEvents, the CPU spawns the tornado) -> cooldown (fuse < 0 counts up to 0). Only with the player on our
    //      level and within g_SandMageCastRange; moveSpeed 0 = training target: never casts (2026-10-17) ----
    bool casting = false;
    if (extra.kind == SWARM_KIND_SANDMAGE)
    {
        float f = CasterStep(i, pos, extra.fuse, g_SandMageWindup, g_SandMageCooldown, g_SandMageCastRange, SWARM_KIND_SANDMAGE, false);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0;
    }
    // ---- necromancer (2026-10-25): the same caster as the sand mage; the cast raises skeletons (CPU, MobSpawner) ----
    if (extra.kind == SWARM_KIND_NECRO)
    {
        float f = CasterStep(i, pos, extra.fuse, g_NecroWindup, g_NecroCooldown, g_NecroCastRange, SWARM_KIND_NECRO, false);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0;
    }
    // ---- chain warden (2026-10-25): winds up with its yaw locked (the band on the ground, SwarmChargeLineVS), throws at the
    //      end (the event carries the yaw; the scene flies the hook), then stands g_WardenHold while the chain is out ----
    if (extra.kind == SWARM_KIND_WARDEN)
    {
        float f = CasterStep(i, pos, extra.fuse, g_WardenWindup, g_WardenCooldown, g_WardenRange, SWARM_KIND_WARDEN, true);
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        casting = f > 0.0 || f < -(g_WardenCooldown - g_WardenHold);
    }

    // ---- stone gargoyle (2026-10-25): asleep = a statue (fuse <= 0 counts down) -> wakes with the player within
    //      g_GargoyleWakeDist or after g_GargoyleAutoWake -> waking (shakes, rises) -> flies at the player like a ghost
    //      -> with the player within g_GargoyleDiveRange: locks its yaw, hangs (the band on the ground), dives along it,
    //      lands and stands g_GargoyleLandTime (the time to hit it) -> rises and flies again. Wake / dive / land go to the
    //      split ring (the CPU's stone crumbs, dust and sounds). moveSpeed 0 = training target: never wakes ----
    bool gargStand = false, gargDive = false;
    if (extra.kind == SWARM_KIND_GARGOYLE)
    {
        float f = extra.fuse;
        float2 toP = g_PlayerPos.xz - pos.xz;
        float distSq = dot(toP, toP);
        float landAt = g_GargoyleDiveWindup + g_GargoyleDiveTime;
        if (f <= 0.0)
        {
            f -= g_Step;
            bool near = (g_PlayerAlive != 0u) && distSq <= g_GargoyleWakeDist * g_GargoyleWakeDist;
            if (enemies[i].moveSpeed > 0.01 && (near || -f >= g_GargoyleAutoWake))
            {
                f = g_Step;
                CastEvent(pos, SWARM_KIND_GARGOYLE | (0u << 16));
            }
        }
        else if (f < SWARM_GARGOYLE_DIVE)
        {
            f = min(f + g_Step, SWARM_GARGOYLE_DIVE - 1.0);
            if (f >= g_GargoyleWakeTime + g_GargoyleFlyMin && g_PlayerAlive != 0u
                && distSq <= g_GargoyleDiveRange * g_GargoyleDiveRange && distSq >= g_GargoyleDiveMin * g_GargoyleDiveMin)
            {
                f = SWARM_GARGOYLE_DIVE + g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);   // the dive line, locked now
                CastEvent(pos, SWARM_KIND_GARGOYLE | (1u << 16));
            }
        }
        else
        {
            float d0 = f - SWARM_GARGOYLE_DIVE;
            f += g_Step;
            float d1 = f - SWARM_GARGOYLE_DIVE;
            if (d0 < landAt && d1 >= landAt)
                CastEvent(pos, SWARM_KIND_GARGOYLE | (2u << 16));
            // flies again: from just past the shake of the waking, so it rises smoothly (SwarmGargoyleHover)
            if (d1 >= landAt + g_GargoyleLandTime)
                f = 0.36 * g_GargoyleWakeTime;
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        gargDive = SwarmGargoyleDiving(extra);
        gargStand = (f < g_GargoyleWakeTime) || (f >= SWARM_GARGOYLE_DIVE && !gargDive);
    }

    // ---- dash bomber (2026-10-18): lights itself with the player within g_DashBomberTrigger on our level, locks its
    //      yaw at the player, stands still g_DashBomberWindup seconds (flashing), then sprints straight along that yaw
    //      (see the dash below) until it touches / the fuse runs out (ContactCS blows it up).
    //      2026-10-20: it used to re-aim every step at 12 m/s = nearly impossible to dodge; now a side step is enough.
    //      fuse > 0 = seconds since lit (counted here, read by ContactCS / VS). moveSpeed 0 = training target: never lights ----
    bool dashing = false, dashWindup = false;
    if (extra.kind == SWARM_KIND_DASHBOMBER)
    {
        float f = extra.fuse;
        if (f > 0.0)
            f += g_Step;
        else if (g_PlayerAlive != 0u && enemies[i].moveSpeed > 0.01 && abs(g_PlayerPos.y - pos.y) < 1.5)
        {
            float2 toP = g_PlayerPos.xz - pos.xz;
            if (dot(toP, toP) <= g_DashBomberTrigger * g_DashBomberTrigger)
            {
                f = g_Step;
                enemies[i].yaw = atan2(toP.x, toP.y);   // the dash direction, locked now
            }
        }
        extra.fuse = f;
        enemyExtra[i].fuse = f;
        dashWindup = f > 0.0 && f <= g_DashBomberWindup;
        dashing = f > g_DashBomberWindup;
    }

    // ---- fixed elite told to stand (a wind-up, the flame breath, the vanish): stands, turning toward the player or along
    //      the CPU's locked direction (a run order with speed 0) ----
    if (eliteRow >= 0)
    {
        uint ecmd = g_EliteCmd[eliteRow].z;
        float4 emv = g_EliteMove[eliteRow];
        if (ecmd == SWARM_ELITE_CMD_STAND || ecmd == SWARM_ELITE_CMD_TELEPORT || (ecmd == SWARM_ELITE_CMD_MOVE && emv.z <= 0.01))
        {
            float2 face = (ecmd == SWARM_ELITE_CMD_MOVE) ? emv.xy : (g_PlayerPos.xz - pos.xz);
            if (dot(face, face) > 1e-6)
            {
                float want = atan2(face.x, face.y);
                float dy = want - enemies[i].yaw;
                dy -= 6.2831853 * floor((dy + 3.14159265) / 6.2831853);   // wrap to -pi..pi
                float turn = (ecmd == SWARM_ELITE_CMD_MOVE) ? 30.0 : 9.0;  // a locked line snaps, facing the player turns
                enemies[i].yaw += clamp(dy, -turn * g_Step, turn * g_Step);
            }
            enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
            return;
        }
    }

    // ---- hit stun / lit bomber / charger winding up or catching its breath: stand still ----
    // velocity 0 so the exit ramps back up through the lag below.
    // A lit bomber stays put until it blows up, so the player can outrun the blast.
    // A charger that has started its wind-up does not flinch (it would be pinned by the fire)
    bool lit = (extra.kind == SWARM_KIND_BOMBER) && (extra.fuse > 0.0);
    bool stunned = (enemies[i].animIndex == 2u) && (cphase == SWARM_CHARGE_CHASE) && !dashing;
    // a surfaced burrower stands where it came out (bites once, then digs in)
    bool surfaced = (extra.kind == SWARM_KIND_BURROWER) && !buried;
    if (stunned || lit || surfaced || casting || dashWindup || gargStand || cphase == SWARM_CHARGE_WINDUP || cphase == SWARM_CHARGE_RECOVER)
    {
        // y is the fall speed (MoveCS owns it): a stunned enemy in mid-air keeps falling
        enemies[i].velocity = float3(0, enemies[i].velocity.y, 0);
        return;
    }

    float3 oldV = enemies[i].velocity; // own slot: written by this thread last step
    float moveSpeed = enemies[i].moveSpeed * slowMul;
    if (eliteRow >= 0)   // the fixed elites' affixes (2026-10-25)
    {
        uint efl = g_EliteCmd[eliteRow].y;
        if ((efl & SWARM_ELITE_SWIFT) != 0u) moveSpeed *= g_EliteSwiftMul;
        if ((efl & SWARM_ELITE_ENRAGED) != 0u) moveSpeed *= g_EliteEnragedSpeedMul;
    }
    if (extra.kind == SWARM_KIND_BURROWER)
        moveSpeed *= buried ? g_BurrowBuriedSpeedMul : g_BurrowSurfaceSpeedMul;

    // ---- seek: follow the flow field, chase directly when close ----
    // the field routes around boxes and walls (the straight line used
    // to pin enemies against the far side of an obstacle). Near the
    // player, or where the field has no answer, fall back to the line
    float3 moveDir = float3(0, 0, 0);
    if (g_PlayerAlive != 0u)
    {
        float3 toPlayer = g_PlayerPos - pos;
        toPlayer.y = 0.0;
        float lenSq = dot(toPlayer, toPlayer);
        float direct = kDirectChaseCells * g_CellSize;

        // the level = the raw height cell under the centre (not the feet: an enemy held on the edge
        // while stepping off has its feet up on the plateau but belongs to the low cells' route now)
        float2 flow = FlowAt(pos.xz, SwarmTerrainHeightRaw(terrainHeight, pos.xz));
        // near but on another level (player on a plateau above / below):
        // the straight line runs into the cliff, keep following the field to a ramp
        bool sameLevel = abs(g_PlayerPos.y - pos.y) < 1.0;
        bool useFlow = (lenSq > direct * direct || !sameLevel) && (dot(flow, flow) > 0.01);
        if (SwarmFliesThrough(extra))
            useFlow = false; // ghosts / a flying gargoyle fly straight at the player (through walls and over plateaus)
        if (useFlow)
            moveDir = normalize(float3(flow.x, 0.0, flow.y));
        else if (lenSq > 1e-6)
            moveDir = toPlayer * rsqrt(lenSq);
        // casters keep their distance (sand mage 2026-10-17, necromancer / chain warden 2026-10-25). Too close = backs away
        // (the hard block below still stops it at walls), in the band = stands (separation only), too far = the usual chase
        float keepMin = 0.0, keepMax = 0.0;
        if (extra.kind == SWARM_KIND_SANDMAGE) { keepMin = g_SandMageKeepMin; keepMax = g_SandMageKeepMax; }
        else if (extra.kind == SWARM_KIND_NECRO) { keepMin = g_NecroKeepMin; keepMax = g_NecroKeepMax; }
        else if (extra.kind == SWARM_KIND_WARDEN) { keepMin = g_WardenKeepMin; keepMax = g_WardenKeepMax; }
        else if (extra.kind == SWARM_KIND_ELITE_B) { keepMin = g_FixedEliteKeepMin; keepMax = g_FixedEliteKeepMax; }   // the ranged fixed elite (2026-10-25)
        if (keepMax > 0.0 && sameLevel)
        {
            if (lenSq < keepMin * keepMin)
                moveDir = (lenSq > 1e-6) ? -toPlayer * rsqrt(lenSq) : float3(0, 0, 0);
            else if (lenSq <= keepMax * keepMax)
                moveDir = float3(0, 0, 0);
        }
        // a flying gargoyle (2026-10-25) circles the player between g_GargoyleDiveMin and 0.9 x the dive range, so
        // the dive starts from out there and comes down on the player (from right above it passed over the head)
        if (extra.kind == SWARM_KIND_GARGOYLE && extra.fuse < SWARM_GARGOYLE_DIVE && lenSq > 1e-6)
        {
            float3 n = toPlayer * rsqrt(lenSq);
            float spin = ((i & 1u) != 0u) ? 1.0 : -1.0;
            if (lenSq < g_GargoyleDiveMin * g_GargoyleDiveMin)
                moveDir = normalize(-n + float3(-n.z, 0.0, n.x) * (0.6 * spin));
            else if (lenSq <= 0.81 * g_GargoyleDiveRange * g_GargoyleDiveRange)
                moveDir = float3(-n.z, 0.0, n.x) * (0.7 * spin);
        }
    }

    // ---- separation: live neighbours in the 3x3 cells around us ----
    // was an all-pairs loop over the whole 4096 pool; the spatial hash
    // keeps it at (alive in 9 cells). g_SeparationRadius must stay
    // <= g_CellSize or neighbours two cells away get missed
    float3 sep = float3(0, 0, 0);
    float sepRadSq = g_SeparationRadius * g_SeparationRadius;

    int gx = (int) floor((pos.x - g_GridOrigin.x) / g_CellSize);
    int gz = (int) floor((pos.z - g_GridOrigin.z) / g_CellSize);
    for (int dz = -1; dz <= 1; ++dz)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            int cx = gx + dx, cz = gz + dz;
            if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                continue;
            uint cell = (uint) (cz * (int) g_GridW + cx);
            uint n = min(cellCount[cell], SWARM_BUCKET_CAP);
            for (uint k = 0u; k < n; ++k)
            {
                uint j = cellItems[cell * SWARM_BUCKET_CAP + k];
                if (j == i)
                    continue;

                float3 away = pos - enemies[j].position;
                // another level (on a plateau above / below, or falling past): not touching.
                // The crowd at a cliff foot used to hold the ones on the edge above back
                if (abs(away.y) > 1.5)
                    continue;
                away.y = 0.0;
                float dSq = dot(away, away);
                if (dSq > sepRadSq || dSq < 1e-6)
                    continue;

                float dist = sqrt(dSq);
                sep += away / dist * (1.0 - dist / g_SeparationRadius);
            }
        }
    }

    // ---- avoid: soft push away from blocked neighbour cells (not for ghosts / a flying gargoyle) ----
    bool ghost = SwarmFliesThrough(extra);
    float3 avoid = float3(0, 0, 0);
    float cs = g_CellSize;
    // cliffs going down are open while the player stands lower (SwarmDropAllowed); the avoid and the hard block share it
    bool allowDrop = SwarmDropAllowed(terrainHeight, pos.xz, pos.y - g_GroundY);
    // (cliff faces are deliberately not avoided: tried 2026-10-20, the push away from a terrace
    // side shoved enemies into the trees diagonally beside it, where they jammed)
    if (!ghost)
    {
        if (!SwarmIsWalkable(terrain, pos + float3(cs, 0, 0)))
            avoid.x -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(cs, 0, 0)))
            avoid.x += 1.0;
        if (!SwarmIsWalkable(terrain, pos + float3(0, 0, cs)))
            avoid.z -= 1.0;
        if (!SwarmIsWalkable(terrain, pos - float3(0, 0, cs)))
            avoid.z += 1.0;
    }

    // ---- compose the target velocity ----
    float3 target = moveDir * moveSpeed
                  + sep * g_SeparationPower
                  + avoid * g_AvoidPower;
    target.y = 0.0;

    // ---- speed cap: separation may stack up far beyond moveSpeed ----
    float maxSpeed = moveSpeed * g_MaxSpeedMul;
    float tLenSq = dot(target, target);
    if (tLenSq > maxSpeed * maxSpeed)
        target *= maxSpeed * rsqrt(tLenSq);

    // ---- inertia: exponential approach to the target ----
    // frame-rate independent because g_Step is the fixed step
    float k = 1.0 - exp(-g_VelocityLag * g_Step);
    float3 v = lerp(oldV, target, k);

    // ---- boss charge (2026-10-07): the CPU (BossAttacks) steers the boss straight along a line ----
    // no inertia (snaps into the dash), no slow, no stop at the player: it runs through.
    // The hard block below still stops it at walls and cliffs
    bool charging = (extra.kind == SWARM_KIND_BOSS) && (g_BossChargeOn > 0.5);
    if (charging)
        v = float3(g_BossChargeDir.x, 0.0, g_BossChargeDir.y) * g_BossChargeSpeed;
    // ---- fixed elite's run order (the rider's charges, 2026-10-25): straight along the CPU's line, through the player ----
    bool eliteDash = false;
    if (eliteRow >= 0 && g_EliteCmd[eliteRow].z == SWARM_ELITE_CMD_MOVE)
    {
        float4 emv = g_EliteMove[eliteRow];
        v = float3(emv.x, 0.0, emv.y) * emv.z;
        charging = true;
        eliteDash = emv.z > 6.0;
    }
    // ---- charger dash: straight along the yaw locked at the wind-up, through the player ----
    float dashSpeed = g_ChargerDashSpeed * slowMul;
    if (cphase == SWARM_CHARGE_DASH)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * dashSpeed;
        charging = true;
    }
    // ---- lit dash bomber (wind-up over): sprints straight along the yaw locked when it lit, through where the player
    //      was (ContactCS blows it up on touch). MoveCS turns the yaw toward the velocity, which is this yaw = no drift ----
    if (dashing)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * g_DashBomberDashSpeed;
        charging = true;
    }
    // ---- gargoyle's dive (2026-10-25): straight along the yaw locked at the start, coming down (MoveCS lowers it) ----
    if (gargDive)
    {
        float ys, yc;
        sincos(enemies[i].yaw, ys, yc);
        v = float3(ys, 0.0, yc) * g_GargoyleDiveSpeed;
        charging = true;
    }

    // ---- player is solid ----
    // Same idea as the terrain hard block, but against a circle:
    // drop the velocity component that would carry us inside the
    // contact radius, keep the tangential part so the crowd slides
    // around and rings the player instead of piling onto them.
    // An enemy already overlapping is pushed out gently.
    // Only on the player's level: an enemy on a plateau edge right above the
    // player is not touching it and has to step off and drop (2026-10-01)
    if (g_PlayerAlive != 0u && abs(g_PlayerPos.y - pos.y) < 1.5 && !charging)
    {
        float3 toP = g_PlayerPos - pos;
        toP.y = 0.0;
        float dist = length(toP);
        float contact = g_PlayerRadius + g_EnemyRadius * SwarmSlotScale(i, extra.kind);   // a berserk fixed elite stops farther out (2026-10-25)

        float3 n;
        if (dist > 1e-4)
            n = toP / dist;
        else
        {
            // dead centre: pick a direction from the slot index so
            // stacked enemies scatter instead of all pushing the same way
            float a = (float) i * 2.399;
            n = float3(cos(a), 0.0, sin(a));
            dist = 0.0;
        }

        float along = dot(v, n); // > 0 = moving toward the player
        float nextDist = dist - along * g_Step;

        if (along > 0.0 && nextDist < contact)
            v -= n * along;

        if (dist < contact)
            v -= n * (contact - dist) * g_PlayerPushOut;
    }

    // ---- hard block, applied LAST (after smoothing and the player push) ----
    // the smoothed velocity may still carry an old component into a
    // wall, and the player push-out used to come after this check and shove
    // enemies into trees / walls (2026-09-30). Look-ahead = body radius +
    // velocity * g_LookAhead, so the mesh stops at the wall face instead of
    // sinking half a body into it. A cliff (too steep, see SwarmSlopeOk)
    // counts as a wall: the axis that would climb or drop it is dropped, so
    // the enemy slides along the plateau side and the flow field leads it to
    // a ramp. The diagonal is checked too (both axes free but the corner
    // cell blocked used to let enemies cut through the corner).
    // An enemy already inside a blocked cell skips this: MoveCS walks it out.
    // With the player lower down (SwarmDropAllowed) a cliff going DOWN is no
    // wall: the enemy walks off the edge and falls (2026-10-01)
    if (!ghost && SwarmIsWalkable(terrain, pos))
    {
        float r = SwarmBodyWallRadius(extra.kind);   // same body radius as MoveCS / PushCS
        float feet = pos.y - g_GroundY;
        // a dashing charger looks only a couple of steps ahead: at 14 m/s the usual
        // look-ahead (0.35 s = 5 m) would end the dash 5 m short of any wall behind the player
        float look = (cphase == SWARM_CHARGE_DASH || dashing || eliteDash) ? 2.0 * g_Step : g_LookAhead;
        float ax = (v.x != 0.0) ? v.x * look + sign(v.x) * r : 0.0;
        float az = (v.z != 0.0) ? v.z * look + sign(v.z) * r : 0.0;

        bool blockX = ax != 0.0 && ProbeBlocked(pos, pos.xz + float2(ax, 0), r, allowDrop, feet);
        bool blockZ = az != 0.0 && ProbeBlocked(pos, pos.xz + float2(0, az), r, allowDrop, feet);
        float lostX = blockX ? v.x : 0.0;
        float lostZ = blockZ ? v.z : 0.0;
        if (blockX) v.x = 0.0;
        if (blockZ) v.z = 0.0;
        // The probe runs along the centre line. With the centre right on a cell border beside a
        // blocked cell (x exactly -28.00 next to a tree cell) it lands in that cell, so the axis
        // toward the open lane beside it was dropped and the enemy stood still or turned on the
        // spot for minutes (soak 2026-10-03). Probe to each side: if only one side is open, slide
        // that way (MoveCS side-steps the corner).
        // 2026-10-20: the side probes use the body test too (ProbeBlocked), a body radius plus a
        // margin out (at exactly one radius the body there still grazed the corner), and the slide
        // runs at least half the move speed: the blocked axis restarts from 0 every step (the
        // smoothing), so the speed it lost was ~0.5 m/s and the old "only when the other axis is
        // nearly still" rule hardly ever fired - the enemy sat beside the corner twitching
        // Only while the other axis is nearly still (relative to the move speed, not to the lost
        // speed): a slide that overrides a real velocity on the other axis fought the flow and the
        // enemy twitched back and forth (tried 2026-10-20)
        float side = r + 0.25;
        float slide = max(0.5 * moveSpeed, 0.0);
        if (blockZ && abs(v.x) < 0.25 * moveSpeed)
        {
            bool openL = !ProbeBlocked(pos, pos.xz + float2(-side, az), r, allowDrop, feet);
            bool openR = !ProbeBlocked(pos, pos.xz + float2(side, az), r, allowDrop, feet);
            if (openL != openR)
                v.x = (openL ? -1.0 : 1.0) * max(abs(lostZ), slide);
        }
        if (blockX && abs(v.z) < 0.25 * moveSpeed)
        {
            bool openL = !ProbeBlocked(pos, pos.xz + float2(ax, -side), r, allowDrop, feet);
            bool openR = !ProbeBlocked(pos, pos.xz + float2(ax, side), r, allowDrop, feet);
            if (openL != openR)
                v.z = (openL ? -1.0 : 1.0) * max(abs(lostX), slide);
        }
        // a cliff counts as a corner too (2026-10-20): plateau cells are walkable, so the walkable
        // test alone let an enemy push diagonally into the corner of a step terrace. The body
        // circle in MoveCS then clipped the corner on the step and on both slides, and its
        // side-step had no direction (the push-out pointed straight back along the velocity)
        if (v.x != 0.0 && v.z != 0.0 && ProbeBlocked(pos, pos.xz + float2(ax, az), r, allowDrop, feet))
        {
            // corner: keep the axis that has more room, drop the other
            if (abs(v.x) > abs(v.z)) v.z = 0.0; else v.x = 0.0;
        }
    }
    // a charger that rams a wall / cliff stops the dash there and catches its breath
    if (cphase == SWARM_CHARGE_DASH && dot(v.xz, v.xz) < 0.25 * dashSpeed * dashSpeed)
        enemyExtra[i].fuse = g_ChargerWindup + SwarmChargerDashTime() + g_Step;
    v.y = oldV.y;   // the fall speed is MoveCS's (gravity), the AI only steers xz
    enemies[i].velocity = v;
}