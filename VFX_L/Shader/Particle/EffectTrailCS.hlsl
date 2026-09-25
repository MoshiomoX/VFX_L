// ============================================================
// EffectTrailCS.hlsl
// One thread per effect trail instance, right after ParticleTrailCS.
//
//   1. apply this frame's anchor (reset / move the head / nothing)
//   2. commit the head into the ring when it is minDistance away
//      from the newest point
//   3. cut the tail where age == lifetime, drop points behind it
//   4. still visible -> add to the alive list (DrawInstancedIndirect)
//
// See Common/EffectTrail.hlsli for the data layout.
// ============================================================
#include "Common/ParticleTrail.hlsli"
#include "Common/EffectTrail.hlsli"

cbuffer EffectTrailCB : register(b0)
{
    float g_Now; // same clock as GlobalCB.totalTime / TrailDrawCB.time
    uint g_TrailCount;
    uint2 _padCB;
};

StructuredBuffer<EffectTrailAnchor> anchors : register(t0);
StructuredBuffer<TrailStyle> trailStyles : register(t1);

RWStructuredBuffer<EffectTrailState> states : register(u0);
RWStructuredBuffer<EffectTrailPoint> points : register(u1);
RWStructuredBuffer<uint> trailAlive : register(u2);
RWBuffer<uint> g_Args : register(u3); // [1] = InstanceCount

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= g_TrailCount)
        return;

    EffectTrailAnchor a = anchors[i];
    if (a.styleSlot == 0u)
        return; // unused instance

    TrailStyle s = trailStyles[a.styleSlot - 1u];
    float lifetime = max(s.lifetime, 0.01);
    uint base = i * EFFECT_TRAIL_POINTS;
    EffectTrailState st = states[i];

    // ---- 1. anchor ----
    if (a.command == EFFECT_TRAIL_CMD_RESET)
    {
        st.headPos = a.position;
        st.headTime = g_Now;
        st.headDist = 0.0;
        st.ringHead = 0u;
        st.ringCount = 0u;
    }

    if (a.command != EFFECT_TRAIL_CMD_NONE)
    {
        float moved = distance(a.position, st.headPos);
        if (moved > 1e-5)
        {
            st.headDist += moved;
            st.headPos = a.position;
            st.headTime = g_Now;
        }

        // ---- 2. commit ----
        bool commit = (st.ringCount == 0u);
        if (!commit)
            commit = distance(st.headPos, points[base + st.ringHead].position) >= max(a.minDistance, 0.001);

        if (commit)
        {
            st.ringHead = (st.ringCount == 0u) ? 0u : (st.ringHead + 1u) % EFFECT_TRAIL_POINTS;

            EffectTrailPoint p;
            p.position = st.headPos;
            p.time = st.headTime;
            p.distance = st.headDist;
            points[base + st.ringHead] = p;

            st.ringCount = min(st.ringCount + 1u, EFFECT_TRAIL_POINTS);
        }
    }

    // ---- 3. tail ----
    float cutTime = g_Now - lifetime;
    bool visible = (st.headTime > cutTime);

    st.validCount = 0u;
    st.tailPos = st.headPos;
    st.tailTime = st.headTime;
    st.tailDist = st.headDist;

    if (visible && st.ringCount > 0u)
    {
        // walk newest -> oldest. prev = the point in front (starts at the head)
        float3 prevPos = st.headPos;
        float prevTime = st.headTime;
        float prevDist = st.headDist;
        bool cut = false;

        [loop]
        for (uint j = 0u; j < st.ringCount; ++j)
        {
            EffectTrailPoint p = points[base + EffectTrailSlot(st.ringHead, j)];
            if (p.time <= cutTime)
            {
                // the cut lies between prev (alive) and p (expired)
                float f = saturate((prevTime - cutTime) / max(prevTime - p.time, 1e-6));
                st.tailPos = lerp(prevPos, p.position, f);
                st.tailTime = cutTime;
                st.tailDist = lerp(prevDist, p.distance, f);
                st.validCount = j;
                // keep p itself: next frame the cut still interpolates toward it
                st.ringCount = j + 1u;
                cut = true;
                break;
            }
            prevPos = p.position;
            prevTime = p.time;
            prevDist = p.distance;
        }

        if (!cut)
        {
            // nothing expired (young trail, or the ring is full): the oldest point is the tail
            st.tailPos = prevPos;
            st.tailTime = prevTime;
            st.tailDist = prevDist;
            st.validCount = st.ringCount - 1u;
        }
    }

    states[i] = st;

    // ---- 4. draw list ----
    if (!visible)
        return;

    uint slot;
    InterlockedAdd(g_Args[1], 1u, slot);
    trailAlive[slot] = i;
}
