// ============================================================
// EffectTrailCS.hlsl
// エフェクトのトレイルのインスタンス 1 つに 1 スレッド。ParticleTrailCS のすぐ後に走る。
//
//   1. このフレームのアンカーを反映（リセット / 先頭を動かす / 何もしない）
//   2. 最新の点から minDistance 離れたら、先頭をリングに
//      確定する
//   3. age == lifetime の所で尾を切り、それより後ろの点を捨てる
//   4. まだ見える -> 生存リストに足す（DrawInstancedIndirect）
//
// データのレイアウトは Common/EffectTrail.hlsli を参照。
// ============================================================
#include "Common/ParticleTrail.hlsli"
#include "Common/EffectTrail.hlsli"

cbuffer EffectTrailCB : register(b0)
{
    float g_Now; // GlobalCB.totalTime / TrailDrawCB.time と同じ時計
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
        return; // 使われていないインスタンス

    TrailStyle s = trailStyles[a.styleSlot - 1u];
    float lifetime = max(s.lifetime, 0.01);
    uint base = i * EFFECT_TRAIL_POINTS;
    EffectTrailState st = states[i];

    // ---- 1. アンカー ----
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

        // ---- 2. 確定 ----
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

    // ---- 3. 尾 ----
    float cutTime = g_Now - lifetime;
    bool visible = (st.headTime > cutTime);

    st.validCount = 0u;
    st.tailPos = st.headPos;
    st.tailTime = st.headTime;
    st.tailDist = st.headDist;

    if (visible && st.ringCount > 0u)
    {
        // 新しい -> 古いの順にたどる。prev = 一つ前（先頭側）の点（先頭から始まる）
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
                // 切れ目は prev（生きている）と p（期限切れ）の間にある
                float f = saturate((prevTime - cutTime) / max(prevTime - p.time, 1e-6));
                st.tailPos = lerp(prevPos, p.position, f);
                st.tailTime = cutTime;
                st.tailDist = lerp(prevDist, p.distance, f);
                st.validCount = j;
                // p 自体は残す: 次のフレームも切れ目はそこへ向けて補間する
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
            // 何も期限切れでない（出来たばかりのトレイル、またはリングが満杯）: 一番古い点が尾
            st.tailPos = prevPos;
            st.tailTime = prevTime;
            st.tailDist = prevDist;
            st.validCount = st.ringCount - 1u;
        }
    }

    states[i] = st;

    // ---- 4. 描画リスト ----
    if (!visible)
        return;

    uint slot;
    InterlockedAdd(g_Args[1], 1u, slot);
    trailAlive[slot] = i;
}
