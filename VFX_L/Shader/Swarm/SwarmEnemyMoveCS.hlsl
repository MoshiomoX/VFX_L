// ============================================================
// SwarmEnemyMoveCS.hlsl
// One fixed step of enemy integration. Runs after SwarmEnemyAICS
// so every velocity is final before any position moves.
//
// y sticks to g_GroundY + terrain height, except after stepping off a
// cliff (allowed toward a player lower down, 2026-10-01): then the enemy
// falls with kFallGravity (velocity.y, written only here) until it lands.
// The height field (GridWorld::Heights, SWARM_HEIGHT_SUB cells per grid
// cell) is 0 on flat ground and the walkable surface height on ramps
// (trapezoid blocks), so enemies climb slopes by following it. Boxes
// and walls stay 0 there and block through the walkable grid instead.
// yaw is stored in radians (the VS does sin/cos on it directly).
//
// Also counts alive enemies for the readback.
// ============================================================
#define SWARM_BOMBER_CB_REG b3   // kind body scale (elites / boss keep a bigger gap to walls)
#include "../Common/SwarmCommon.hlsli"

Buffer<uint> enemyStates : register(t0);
StructuredBuffer<float> terrainHeight : register(t1);
StructuredBuffer<uint> terrain : register(t2); // walkable grid (for the slide / walk-out)
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3); // kind: ghosts skip the walls
RWStructuredBuffer<SwarmEnemy> enemies : register(u0);
RWByteAddressBuffer counters : register(u1);
// knockback (the Gust, 2026-10-12): xy = velocity away from the player, z = seconds left.
// AreaDamageCS writes it; added to this step's motion here (not to the AI's velocity:
// its smoothing would stretch a 0.25 s shove into several meters) and decayed
RWStructuredBuffer<float4> enemyPush : register(u2);
// spike ball's orientation (quaternion, 2026-10-25): every step's roll is chained onto it here, SwarmSpikeBallVS draws
// with it. Zero = never rolled (SwarmSpikeBallRoll gives the slot's resting pose). Not reset on spawn: a ball's
// orientation has no meaning, a reused slot just starts from where the last ball stopped
RWStructuredBuffer<float4> enemyRoll : register(u3);
// the plain closest enemy (2026-10-27): same race as SWARM_CNT_NEAREST_KEY but on the real distance, no boss / elite
// priority. +0 key (AimResolveCS reads it and resets it to SWARM_NO_TARGET_KEY), +4 pos, +16 dist. Short-reach spells
// (the Gust, the Ice Spike line) aim with it; the wand aims with the priority one (SwarmCounters is full)
RWByteAddressBuffer closeTarget : register(u4);
// wall climbing (2026-10-29, the user: "every enemy can climb walls, or there are spots where nothing can reach you").
// x = seconds climbing (0 = not climbing), y = the top being climbed to (terrain space), zw = the direction into the face.
// Only this pass writes it (PushCS reads it: a climbing body is not shoved). Cleared while the slot is dead
RWStructuredBuffer<float4> enemyClimb : register(u5);

// counts the enemy for the readback and lets it compete for "closest to the player"
// (XZ distance only, same as everything else in this pipeline)
void CountAlive(uint i, float3 pos)
{
    uint prev;
    counters.InterlockedAdd(SWARM_CNT_ALIVE_ENEMIES, 1u, prev);
    float3 dp = pos - g_PlayerPos;
    float dist = sqrt(dp.x * dp.x + dp.z * dp.z);
    // the key sorts by SwarmAimKeyDist (boss / elite first unless a mob is close, 2026-10-26); AimResolveCS
    // writes the winner's real distance back for the CPU
    float keyDist = SwarmAimKeyDist(enemyExtra[i].kind, dist);
    uint key = (asuint(keyDist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK);
    if (!SwarmUntargetable(enemyExtra[i]))   // a burrower under the sand / a tornado is not a target (2026-10-17)
    {
        counters.InterlockedMin(SWARM_CNT_NEAREST_KEY, key, prev);
        closeTarget.InterlockedMin(0, (asuint(dist) & SWARM_DIST_MASK) | (i & SWARM_SLOT_MASK), prev);
    }
}

// m/s a body that touches a wall / cliff face is eased back out
static const float kWallPushOut = 2.0;
static const float kWallPushOutFalling = 4.0;

// falling off a plateau (2026-10-01): m/s^2, same as the battle scene's gravity
static const float kFallGravity = 25.0;
// the ground may drop this much in one step and the enemy still sticks to it
// (walking down a ramp drops ~0.05m a step; stepping off a cliff edge drops
// ~0.5m a step through the bilinear blur, so that one starts a fall)
static const float kSnapDown = 0.25;

// one step from -> to: the centre lands on a walkable cell without a
// cliff on the way, and (bodyCheck) the body circle there touches nothing.
// easing out of a face (!bodyCheck) the cliff test uses the raw heights
// (SwarmRawSlopeOk: the bilinear one would not let it step away).
// allowDrop (player lower down): a cliff going down passes and lower ground
// under the body does not count, so the enemy walks off the edge.
// footH = the feet height (terrain space) the body's rises are measured from
bool StepOk(float2 from, float2 to, float radius, bool bodyCheck, bool allowDrop, float footH)
{
    bool ok = SwarmIsWalkable(terrain, float3(to.x, 0.0, to.y));
    if (ok)
        ok = bodyCheck ? (SwarmStepHeightOk(terrainHeight, from, to, allowDrop) || SwarmRawStepOk(terrainHeight, from, to, allowDrop))
                       : SwarmRawSlopeOk(terrainHeight, from, to);
    if (ok && bodyCheck)
        ok = (SwarmBodyContact(terrain, terrainHeight, to, radius, !allowDrop, footH).z == 0.0);
    return ok;
}

// the side-step around a corner: StepOk with the body check, but the centre's slope is
// the raw one. Next to a face the bilinear ground is blurred up (the last 0.25m before
// it), so stepping away from the face looked like a steep drop to SwarmStepHeightOk and
// the side-step was refused (soak 2026-10-03: an enemy pinned beside a ramp side)
bool SideStepOk(float2 from, float2 to, float radius, bool allowDrop, float footH)
{
    return SwarmIsWalkable(terrain, float3(to.x, 0.0, to.y))
        && SwarmRawSlopeOk(terrainHeight, from, to)
        && SwarmBodyContact(terrain, terrainHeight, to, radius, !allowDrop, footH).z == 0.0;
}

// a cliff under the body circle: the highest and lowest raw height cell differ by this much
static const float kEdgeCliff = 0.8;

// ---- wall climbing (2026-10-29) ----
// The mob terrain (SwarmSystemClimb.cpp) gives the obstacles inside the field (rocks, tree trunks, ruin walls, pillars)
// their top as height and lets the AI push into a rising face it wants to go up (ProbeBlocked's allowClimb). When the
// step below then barely moves and a face rises in front higher than the body's step margin, the enemy starts climbing:
// it stays pressed to the face and only y goes up at g_ClimbSpeed; at the top it steps onto it. It lets go (and falls,
// Settle) when it no longer wants to go that way (the field turned / it froze), it is shoved, the face is no longer
// right in front (the crowd moved it), or it has been at it for kClimbMaxTime
static const float kClimbMinRise = SWARM_BODY_CLIFF_RISE;   // lower steps are taken by the walk itself
static const float kClimbAhead = 0.3;                       // the face is looked for this far past the body
static const float kClimbMaxTime = 10.0;

// the top of the face ahead (raw height cells): the highest of three probes across the heading, or -1e30 when none
// of them is walkable (the outer border is no face to climb)
float FaceTopAhead(float2 from, float2 dir, float radius)
{
    float2 side = float2(-dir.y, dir.x) * (radius * 0.6);
    float2 p = from + dir * (radius + kClimbAhead);
    float top = -1e30;
    [unroll]
    for (int k = -1; k <= 1; ++k)
    {
        float2 q = p + side * (float) k;
        if (SwarmIsWalkable(terrain, float3(q.x, 0.0, q.y)))
            top = max(top, SwarmTerrainHeightRaw(terrainHeight, q));
    }
    return top;
}

// y: stick to the ground, or fall to it (velocity.y = fall speed, only this pass writes it).
// Dropping off an edge (allowDrop): the highest ground under the body circle holds the
// enemy up, so it neither sinks into the bilinear blur at the plateau edge nor starts
// falling while half its body is still over the plateau (the face used to cut through
// it on the way down). It falls once the whole circle is past the edge
void Settle(inout SwarmEnemy e, float radius, bool allowDrop)
{
    float ground = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz);
    // the hold applies whether or not a drop is open (2026-10-20, was allowDrop only): an enemy
    // standing at a plateau edge used to sink into the bilinear dip there (up to half the face)
    {
        float feet = e.position.y - g_GroundY;
        float hi = SwarmTerrainHeightRaw(terrainHeight, e.position.xz), lo = hi;
        [unroll]
        for (int k = 0; k < 8; ++k)
        {
            float a = (float) k * 0.7853982;
            float h = SwarmTerrainHeightRaw(terrainHeight, e.position.xz + float2(cos(a), sin(a)) * radius);
            hi = max(hi, h);
            lo = min(lo, h);
        }
        // still standing on the edge (feet level with the high side, or the centre's own
        // raw cell IS the high side: spawned / pushed into the bilinear dip at the edge, the
        // feet start below the plateau and would otherwise stay there, 2026-10-20), not a
        // body at the cliff foot that only touches the face
        float rawHere = SwarmTerrainHeightRaw(terrainHeight, e.position.xz);
        if (hi - lo > kEdgeCliff && (feet >= hi - kSnapDown || rawHere >= hi - SWARM_BODY_CLIFF_RISE))
            ground = max(ground, g_GroundY + hi);
    }
    if (e.position.y > ground + kSnapDown)
    {
        e.velocity.y -= kFallGravity * g_Step;
        e.position.y = max(e.position.y + e.velocity.y * g_Step, ground);
        if (e.position.y <= ground)
            e.velocity.y = 0.0;   // landed
    }
    else
    {
        e.position.y = ground;
        e.velocity.y = 0.0;
    }
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;
    if (enemyStates[i] == SWARM_DEAD)
    {
        enemyClimb[i] = float4(0, 0, 0, 0);   // a reused slot does not start out climbing
        return;
    }

    SwarmEnemy e = enemies[i];
    float4 climb = enemyClimb[i];
    // ---- a fixed elite's teleport (the shadow swordsman, 2026-10-25): onto the ground at the CPU's spot, facing the player ----
    if (SwarmIsFixedElite(enemyExtra[i].kind))
    {
        int row = SwarmEliteEntry(i);
        if (row >= 0 && g_EliteCmd[row].z == SWARM_ELITE_CMD_TELEPORT)
        {
            float2 t = g_EliteMove[row].xy;
            float2 toP = g_PlayerPos.xz - t;
            float3 tp = float3(t.x, SwarmTerrainHeight(terrainHeight, t) + g_GroundY, t.y);
            enemies[i].position = tp;
            enemies[i].velocity = float3(0, 0, 0);
            enemyClimb[i] = float4(0, 0, 0, 0);
            if (dot(toP, toP) > 1e-6)
                enemies[i].yaw = atan2(toP.x, toP.y);
            CountAlive(i, tp);   // still alive this step (2026-10-25: the early return used to skip the count)
            return;
        }
    }
    float2 startXZ = e.position.xz;   // where this step starts (the spike ball rolls by what it really moved)
    // body radius against walls, and whether cliffs going down are open (player lower down)
    float radius = SwarmBodyWallRadius(enemyExtra[i].kind);
    bool allowDrop = SwarmDropAllowed(terrainHeight, e.position.xz, e.position.y - g_GroundY);

    // knockback (the Gust): this step's shove, decaying over SWARM_PUSH_TIME. Rides on the
    // velocity through the integrate below (wall / cliff checks included), taken off before the facing
    float2 pushV = float2(0.0, 0.0);
    {
        float4 pk = enemyPush[i];
        if (pk.z > 0.0)
        {
            pushV = pk.xy * saturate(pk.z / SWARM_PUSH_TIME);
            pk.z -= g_Step;
            if (pk.z <= 0.0)
                pk = float4(0.0, 0.0, 0.0, 0.0);
            enemyPush[i] = pk;
        }
    }
    e.velocity.xz += pushV;

    if (e.animIndex == 2u && dot(pushV, pushV) < 1e-4)
    {
        // ---- hit stun: hold position and facing, only the clock runs ----
        // HitCS set animTime = 0 when it landed the hit
        e.animTime += g_Step;
        if (e.animTime >= g_HitStun)
        {
            e.animIndex = 0u;
            e.animTime = 0.0;
        }
        if (climb.x <= 0.0)
            Settle(e, radius, allowDrop);   // hit in mid-air: keeps falling (hit while climbing: hangs on the face)
    }
    else
    {
        // ---- integrate ----
        // The AI's hard block is a look-ahead on cell granularity and the
        // player push / separation can still carry a velocity into a wall, so
        // the step itself is checked here (2026-09-30, enemies used to end up
        // inside trees and walls): a step that ends in a blocked cell or over
        // a cliff is reduced to the axis that stays free (slide along the
        // wall), and dropped if neither does.
        // An enemy that is already inside a blocked cell (pushed in, spawned
        // in) is walked toward the nearest free cell instead, ignoring the
        // walls on the way (they are what it is escaping from).
        float2 from = e.position.xz;
        if (SwarmFliesThrough(enemyExtra[i]))   // the tornado (2026-10-17) and a flying gargoyle (2026-10-25) fly like a ghost
        {
            // ghost: flies straight, through walls and over plateaus (y still follows the ground below)
            e.position.xz = from + e.velocity.xz * g_Step;
        }
        else if (climb.x > 0.0)
        {
            // ---- climbing a wall (2026-10-29): pressed to the face, only y goes up; at the top it steps onto it ----
            float2 cdir = climb.zw;
            float feet = e.position.y - g_GroundY;
            float top = FaceTopAhead(from, cdir, radius);
            bool hold = dot(e.velocity.xz - pushV, cdir) > 0.25 * max(e.moveSpeed, 0.5)   // still wants to go that way
                     && dot(pushV, pushV) < 1.0                                          // not being shoved
                     && climb.x < kClimbMaxTime
                     && top > feet - 0.05;                                               // the face is still right in front
            if (!hold)
                climb = float4(0, 0, 0, 0);   // lets go: Settle below drops it
            else
            {
                climb.y = top;
                feet = min(feet + g_ClimbSpeed * g_Step, top);
                climb.x += g_Step;
                e.position.y = g_GroundY + feet;
                e.velocity.y = 0.0;
                if (feet >= top - 1e-3)
                {
                    // at the top: the body steps onto it (Settle's edge hold then keeps it up there)
                    float2 onto = from + cdir * (radius + kClimbAhead + 0.1);
                    if (SwarmIsWalkable(terrain, float3(onto.x, 0.0, onto.y)))
                        e.position.xz = onto;
                    climb = float4(0, 0, 0, 0);
                }
            }
        }
        else if (!SwarmIsWalkable(terrain, e.position))
        {
            float2 best = from;
            float bestD = 1e30;
            int gx = (int) floor((from.x - g_GridOrigin.x) / g_CellSize);
            int gz = (int) floor((from.y - g_GridOrigin.z) / g_CellSize);
            for (int dz = -2; dz <= 2; ++dz)
                for (int dx = -2; dx <= 2; ++dx)
                {
                    int cx = gx + dx, cz = gz + dz;
                    if (cx < 0 || cx >= (int) g_GridW || cz < 0 || cz >= (int) g_GridD)
                        continue;
                    if (terrain[cz * g_GridW + cx] == 0u)
                        continue;
                    float2 c = float2(g_GridOrigin.x + (cx + 0.5) * g_CellSize,
                                      g_GridOrigin.z + (cz + 0.5) * g_CellSize);
                    float d = dot(c - from, c - from);
                    if (d < bestD) { bestD = d; best = c; }
                }
            if (bestD < 1e29)
            {
                float2 dir = best - from;
                float len = length(dir);
                float stepLen = min(len, max(e.moveSpeed, 2.0) * g_Step);
                if (len > 1e-4)
                    e.position.xz = from + dir / len * stepLen;
            }
        }
        else
        {
            // The body circle has to stay clear of walls and cliff faces too, not
            // just the centre (2026-10-01, see SwarmBodyContact). A body that
            // already touches one (pushed in by the crowd, spawned there) eases
            // out at kWallPushOut while keeping the part of its velocity that
            // runs along the wall. When the touches cancel out (squeezed from
            // both sides) only the centre is checked, as before.
            // rises are measured from the feet: on the ground = the raw ground under the centre (as
            // before); stepping off / falling = the real feet (the plateau behind is no wall until
            // the feet sink below it, then it eases the body away from the face on the way down)
            // Only while dropping is allowed: at a cliff foot the bilinear ground lifts the feet
            // too, and with drops counted the low side would then touch as well, cancel the
            // push-out and pin the body in the face (self test clip / climb)
            float rawHere = SwarmTerrainHeightRaw(terrainHeight, from);
            float feet = e.position.y - g_GroundY;
            float footH = allowDrop ? max(rawHere, feet) : rawHere;
            bool airborne = (feet > SwarmTerrainHeight(terrainHeight, from) + kSnapDown);   // falling (Settle)
            float3 touch = SwarmBodyContact(terrain, terrainHeight, from, radius, !allowDrop, footH);
            bool easeOut = (touch.z > 0.0 && dot(touch.xy, touch.xy) > 0.01);
            bool bodyCheck = (touch.z == 0.0);

            float2 v = e.velocity.xz;
            if (easeOut)
            {
                float2 outDir = -normalize(touch.xy);
                float into = -dot(v, outDir);   // > 0 = toward the wall
                if (into > 0.0)
                    v += outDir * into;
                // falling past a cliff face: clear it faster, the face would cut the body on the way down
                v += outDir * (airborne ? kWallPushOutFalling : kWallPushOut);
            }

            float2 next = from + v * g_Step;
            float2 nx = float2(next.x, from.y);
            float2 nz = float2(from.x, next.y);
            // both axes carry real speed (a sliver on one axis made that axis' slide "succeed"
            // with no progress, so the side-step below never ran: soak 2026-10-03)
            float sp = length(v);
            bool both = (abs(v.x) > 0.15 * sp) && (abs(v.y) > 0.15 * sp);
            // ---- start climbing (2026-10-29), checked before the slides / side-steps (they would walk it along the
            //      face instead): the step is refused and straight ahead of where the AI wants to go a face rises higher
            //      than the walk can step. The AI only lets a velocity push into a face it means to climb ----
            bool stepOk = StepOk(from, next, radius, bodyCheck, allowDrop, footH);
            bool startClimb = false;
            if (g_ClimbSpeed > 0.0 && !airborne && !stepOk)
            {
                float2 want = e.velocity.xz - pushV;
                float spv = length(want);
                if (spv > 0.3 * max(e.moveSpeed, 0.5))
                {
                    float2 cdir = want / spv;
                    float2 p = from + cdir * (radius + kClimbAhead);
                    float top = SwarmIsWalkable(terrain, float3(p.x, 0.0, p.y)) ? SwarmTerrainHeightRaw(terrainHeight, p) : -1e30;
                    if (top - feet > kClimbMinRise)
                    {
                        climb = float4(g_Step, top, cdir);
                        startClimb = true;
                    }
                }
            }
            if (startClimb)
            {
                // stays where it is this step; the climb branch takes it from the next one
            }
            else if (stepOk)
                e.position.xz = next;
            else if (both && StepOk(from, nx, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nx;
            else if (both && StepOk(from, nz, radius, bodyCheck, allowDrop, footH))
                e.position.xz = nz;
            else if (bodyCheck)
            {
                // corner ahead to the side (2026-10-03, soak test): the AI only probes
                // straight ahead along each axis, so a wall / plateau corner diagonally
                // in front lets it keep pushing while the body circle here clips the
                // corner -> the step and both slides fail and the enemy stood still
                // for good. Side-step away from the touching points, across the
                // velocity, until the straight path clears
                float3 tn = SwarmBodyContact(terrain, terrainHeight, next, radius, !allowDrop, footH);
                if (tn.z > 0.0 && dot(tn.xy, tn.xy) > 0.01 && sp > 1e-4)
                {
                    float2 dirV = v / sp;
                    float2 away = -normalize(tn.xy);
                    float2 side = away - dirV * dot(away, dirV);
                    if (dot(side, side) > 1e-4)
                    {
                        float2 ns = from + normalize(side) * sp * g_Step;
                        if (SideStepOk(from, ns, radius, allowDrop, footH))
                            e.position.xz = ns;
                    }
                    else
                    {
                        // the push-out points straight back along the velocity (two touching points
                        // either side of the heading, e.g. a terrace corner dead ahead): no side to
                        // prefer, so try both perpendiculars and take the first that is open (2026-10-20)
                        float2 perp = float2(-dirV.y, dirV.x) * sp * g_Step;
                        if (SideStepOk(from, from + perp, radius, allowDrop, footH))
                            e.position.xz = from + perp;
                        else if (SideStepOk(from, from - perp, radius, allowDrop, footH))
                            e.position.xz = from - perp;
                    }
                }
            }
        }
        if (SwarmFliesThrough(enemyExtra[i]))   // the tornado (2026-10-17) and a flying gargoyle (2026-10-25) fly like a ghost
        {
            // ghosts hover over everything: snap to the ground below, no falling (a gargoyle at its flying height)
            e.position.y = g_GroundY + SwarmTerrainHeight(terrainHeight, e.position.xz) + SwarmBodyHover(enemyExtra[i]);
            e.velocity.y = 0.0;
        }
        else if (climb.x <= 0.0)
            Settle(e, radius, allowDrop);   // climbing sets y itself

        // ---- facing: turn toward the velocity at a bounded rate (the shove is not a direction it chose) ----
        e.velocity.xz -= pushV;
        // a boss standing in a wind-up (the CPU's charge order at speed 0) turns to the order's direction, and the floating
        // djinn turns to the player while it hangs still (2026-10-29: it used to keep the last walking direction)
        float2 faceDir = e.velocity.xz;
        if (enemyExtra[i].kind == SWARM_KIND_BOSS && dot(faceDir, faceDir) <= 0.01)
        {
            if (g_BossChargeOn > 0.5)
                faceDir = g_BossChargeDir;
            else if (g_BossFly > 0.5)
                faceDir = g_PlayerPos.xz - e.position.xz;
        }
        if (dot(faceDir, faceDir) > 0.01)
        {
            float targetYaw = atan2(faceDir.x, faceDir.y);
            float delta = targetYaw - e.yaw;
            delta = atan2(sin(delta), cos(delta)); // wrap to [-PI, PI]
            float maxTurn = g_TurnSpeed * g_Step;
            e.yaw += clamp(delta, -maxTurn, maxTurn);
        }
        // ---- animation clock (the enemy VS's walk cycle, the shield / mound bob, the wind source's fade-in read it) ----
        e.animTime += g_Step;
    }

    // ---- spike ball (2026-10-25): chain this step's roll onto its orientation. The VS used to turn it by
    //      animTime x speed / radius, which multiplied every small change of speed by the time walked and snapped
    //      the pose on every turn. Rolling d meters about up x travel turns it d / radius ----
    if (enemyExtra[i].kind == SWARM_KIND_SPIKEBALL)
    {
        float2 moved = e.position.xz - startXZ;
        float dist = length(moved);
        if (dist > 1e-5)
        {
            float3 axis = normalize(cross(float3(0, 1, 0), float3(moved.x, 0.0, moved.y) / dist));
            float angle = dist * g_SpikeBallRollPerMeter / max(SwarmKindScale(SWARM_KIND_SPIKEBALL), 1e-3);
            float4 q = SwarmQuatMul(SwarmQuatAxisAngle(axis, angle), SwarmSpikeBallRoll(enemyRoll[i], i));
            enemyRoll[i] = normalize(q);
        }
    }

    enemyClimb[i] = climb;
    enemies[i].position = e.position;
    enemies[i].velocity.y = e.velocity.y;   // fall speed (xz is the AI's)
    enemies[i].yaw = e.yaw;
    enemies[i].animTime = e.animTime;
    enemies[i].animIndex = e.animIndex;

    CountAlive(i, e.position);
}