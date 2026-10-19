// ============================================================
// SwarmDamageTextCS.hlsl
// Floating damage numbers (2026-10-17). Once per frame, one thread per
// enemy slot. Compares the slot's hp with what it was last frame
// (prevHp) and turns the drop into an event the CPU reads back and draws
// as a number above the enemy (UI/DamageNumbers).
//
// Done here instead of in the damage passes: HitCS has all 8 UAVs of
// D3D11.0 in use, and one place catches projectiles, area ticks, toxin
// drain and contact the same way.
//
// Merging: hits land many times a second on one enemy (beam ticks, poison
// drain). The drop is accumulated per slot (pending.x direct, .y poison)
// and emitted once the first drop is g_DtxMerge seconds old, or at once
// when the enemy dies (then with the kill flag; the remaining hp counts).
// Poison: the part of the drop that matches the toxin drained this frame
// (enemyPoison.x went down) is counted as poison, the rest as direct.
//
// Event ring (dmgEvents, RAW): [0] = events written so far, then 16 bytes
// x SWARM_MAX_DAMAGE_TEXTS of float3 position (above the body) + uint
// packed (SWARM_DTX_* bits: amount x10 in the low 24 bits). The CPU reads
// it back like the split ring (3 staging buffers, 2-3 frames late).
//
// KillAll clears prevHp and pending, so wiping the field prints nothing.
// A slot that dies and is reused within the same frame is missed (rare).
//
// A slot that turns DEAD with its hp intact was not killed: a bomber / dash
// bomber blowing itself up, the sand mage's tornado expiring (hp 1,000,000),
// a tornado that is "untargetable". Those print nothing (2026-10-19: the
// tornado used to print "1000000 killed" when it faded). The kill flag only
// goes on when the hp really reached zero this frame.
// ============================================================
#define SWARM_AI_CB_REG b2
#define SWARM_BOMBER_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

cbuffer SwarmDamageTextCB : register(b4)
{
    float g_DtxStep;    // frame dt
    float g_DtxMerge;   // seconds a direct drop waits for more before it is emitted
    uint g_DtxOn;       // 0 = only keep prevHp / pending up to date
    float g_DtxPoisonMerge;   // same for toxin drain (slow trickle: merge longer so it reads as one number)
};

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
StructuredBuffer<float4> enemyPoison : register(t2);   // x = toxin still to drain
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t3);

RWBuffer<uint> prevHp : register(u0);                  // hp (fixed) seen last frame, 0 for a dead slot
RWStructuredBuffer<float4> pending : register(u1);     // x direct, y poison (hp), z seconds since the first drop, w toxin seen last frame
RWByteAddressBuffer dmgEvents : register(u2);

static const uint SWARM_MAX_DAMAGE_TEXTS = 512u;   // Swarm::kMaxDamageTexts
static const uint SWARM_DTX_AMOUNT_MASK = 0x00FFFFFFu;   // amount x 10
static const uint SWARM_DTX_POISON = 1u << 24;           // mostly toxin drain -> green
static const uint SWARM_DTX_KILL = 1u << 25;             // the enemy died -> bigger
static const uint SWARM_DTX_ELEMENT_SHIFT = 26u;         // bits 26..29: element of the last hit (state word), the number's colour

float HpOf(uint raw)
{
    // a killed enemy's hp underflowed (InterlockedAdd past zero): treat as 0
    return (raw > 0x7FFFFFFFu) ? 0.0 : (float) raw / SWARM_HP_SCALE;
}

void Emit(uint i, float direct, float poison, bool kill, uint elem)
{
    float total = direct + poison;
    if (total < 0.05)
        return;
    SwarmEnemy e = enemies[i];
    uint kind = enemyExtra[i].kind;
    float3 center;
    float er, eh;
    SwarmKindCapsule(kind, e.position, center, er, eh);
    float3 pos = center + float3(0.0, eh + er + 0.25, 0.0);   // just above the head

    uint amount = min((uint) round(total * 10.0), SWARM_DTX_AMOUNT_MASK);
    uint packed = amount;
    if (poison > direct) packed |= SWARM_DTX_POISON;
    if (kill) packed |= SWARM_DTX_KILL;
    packed |= (elem & 0xFu) << SWARM_DTX_ELEMENT_SHIFT;

    uint n;
    dmgEvents.InterlockedAdd(0, 1u, n);
    uint at = 16u + (n % SWARM_MAX_DAMAGE_TEXTS) * 16u;
    dmgEvents.Store3(at, asuint(pos));
    dmgEvents.Store(at + 12u, packed);
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_MaxEnemies)
        return;

    uint hpRaw = enemies[i].hp;
    uint state = enemyStates[i];
    bool dead = state == SWARM_DEAD;
    uint elem = SwarmStateElement(state);
    uint prevRaw = prevHp[i];
    float4 p = pending[i];
    float toxinNow = enemyPoison[i].x;
    float drain = max(0.0, p.w - toxinNow);   // toxin that drained this frame (a new dose only raises it)
    p.w = toxinNow;

    if (g_DtxOn == 0u)
    {
        prevHp[i] = dead ? 0u : hpRaw;
        pending[i] = float4(0.0, 0.0, 0.0, toxinNow);
        return;
    }

    // hp is read as it is even for a dead slot: a kill leaves it at 0 (or underflowed -> 0),
    // a self-destruct / expiry leaves it where it was -> no drop, nothing printed
    float hp = HpOf(hpRaw);
    float prev = HpOf(prevRaw);
    if (prevRaw != 0u && hp < prev)
    {
        float drop = prev - hp;
        float poisonPart = min(drop, drain);
        p.x += drop - poisonPart;
        p.y += poisonPart;
    }
    if (p.x + p.y > 0.0)
        p.z += g_DtxStep;

    if (dead)
    {
        // was alive last frame and the hp really ran out: everything pending goes out flagged as the kill.
        // Died with hp left (blew itself up, tornado faded): whatever was pending goes out as a plain number
        if (prevRaw != 0u && !SwarmUntargetable(enemyExtra[i]))
            Emit(i, p.x, p.y, hp <= 0.0, elem);
        p.xyz = float3(0.0, 0.0, 0.0);
    }
    else
    {
        // direct hits and the toxin trickle go out on their own clocks (both measured from the first drop)
        if (p.x > 0.0 && p.z >= g_DtxMerge) { Emit(i, p.x, 0.0, false, elem); p.x = 0.0; }
        if (p.y > 0.0 && p.z >= g_DtxPoisonMerge) { Emit(i, 0.0, p.y, false, elem); p.y = 0.0; }
        if (p.x + p.y <= 0.0) p.z = 0.0;
    }
    prevHp[i] = dead ? 0u : hpRaw;
    pending[i] = p;
}
