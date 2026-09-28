// ============================================================
// GrassTrampleCS.hlsl
// The trample map: one texel per small patch of the whole map,
// xy = the direction the grass is pushed, length = how hard (0..1).
// Every frame the old value fades (g_TrampleDecay) and the player's
// disc stamps a push away from its centre; the stronger one wins.
// Ping-pong: reads last frame's map, writes the other one (typed UAV
// loads of R16G16_FLOAT are not guaranteed on D3D11).
// ============================================================
#include "GrassCommon.hlsli"

Texture2D<float2> g_TrampleIn : register(t0);
RWTexture2D<float2> g_TrampleOut : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    g_TrampleOut.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float2 v = g_TrampleIn.Load(int3(id.xy, 0)) * g_TrampleDecay;
    if (g_TrampleRadius > 0.0)
    {
        float2 xz = g_MapOrigin + (float2(id.xy) + 0.5) / float2(w, h) * g_MapSize;
        float2 d = xz - g_Player.xz;
        float dist = length(d);
        if (dist < g_TrampleRadius)
        {
            // flat-topped falloff: the whole disc is pressed, the edge softly
            float s = sqrt(1.0 - dist / g_TrampleRadius);
            float2 push = (dist > 1e-3) ? d / dist * s : float2(0.0, 0.0);
            if (dot(push, push) > dot(v, v))
                v = push;
        }
    }
    g_TrampleOut[id.xy] = v;
}
