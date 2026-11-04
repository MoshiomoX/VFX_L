// ============================================================
// VFXBeamVS.hlsl
// Beam エントリ（CPU）。1 インスタンス = 光線 1 本の層 1 つ
// （instance / 3 = 光線、instance % 3 = 層）。SV_VertexID で始点から終点まで
// BEAM_SEGMENTS 枚の四角の帯を作り、光線の軸を中心に
// カメラの方へ回す。uv.x = 光線に沿った距離 (m)、
// uv.y = 横方向に -1..1。
// ============================================================
#include "../Common/BeamCommon.hlsli"

cbuffer BeamCB : register(b0)
{
    row_major float4x4 g_ViewProj;
    float3 g_CamPos;
    uint g_First;
    float g_Time;
    float3 _pad0;
};

StructuredBuffer<BeamItem> g_Beams : register(t0);

struct VSOut
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint beam : TEXCOORD1;
    nointerpolation uint layer : TEXCOORD2;
    nointerpolation float len : TEXCOORD3;
};

VSOut main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    VSOut o;
    o.beam = g_First + iid / BEAM_LAYERS;
    o.layer = iid % BEAM_LAYERS;
    BeamItem b = g_Beams[o.beam];

    static const float2 corners[6] =
    {
        float2(0, -1), float2(1, -1), float2(0, 1),
        float2(0, 1), float2(1, -1), float2(1, 1),
    };
    uint seg = vid / 6;
    float2 c = corners[vid % 6];
    float t = (seg + c.x) / (float) BEAM_SEGMENTS;

    float3 axis = b.end - b.start;
    float len = length(axis);
    axis = (len > 1e-4) ? axis / len : float3(0, 0, 1);
    float d = t * len;
    float3 p = b.start + axis * d;

    // 稲妻: 区間の境目を横に折る（2026-10-15）
    float flicker = 1.0;
    if (b.jitter > 0.0)
    {
        float phase = BeamLightningPhase(b, g_Time);
        p += BeamLightningOffset(b, seg + c.x, t, axis, phase);
        flicker = 0.7 + 0.6 * BeamHash(float2(phase, 3.3));
    }

    // 帯は軸を中心にカメラの方を向く
    float3 toCam = g_CamPos - p;
    float3 side = cross(axis, toCam);
    float sl = length(side);
    side = (sl > 1e-4) ? side / sl : float3(0, 1, 0);

    float hw = BeamHalfWidth(b, o.layer, d, len) * flicker;
    float3 world = p + side * (hw * c.y);

    o.position = mul(float4(world, 1.0), g_ViewProj);
    o.uv = float2(d, c.y);
    o.len = len;
    return o;
}
