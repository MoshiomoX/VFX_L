// ============================================
// ParticleCubeVS.hlsl
// Draw cube particles: a unit cube mesh (VERTEX_3D) instanced once
// per alive cube particle (DrawIndexedInstancedIndirect).
//   world = Scale(size) * RotXYZ(rot3) * Translate(position)
// Outputs the standard model VS_OUTPUT so PS.hlsl (Lambert) shades it.
// Color comes from the particle (alpha ignored, cubes are opaque).
//
// ParticleCommon owns b0/b1, so the MVP block moves to b2 (World is
// unused: the per-instance world is built here).
// ============================================
#include "Common/ParticleCommon.hlsli"

#define MODEL_MVP_CB_REG b2
#include "../Common/ModelCommon.hlsli"

// t0..t4 are the model textures (declared by ModelCommon, unused in VS)
StructuredBuffer<GPUParticle> particles : register(t5);
StructuredBuffer<uint> aliveCube : register(t6);

float3x3 RotationXYZ(float3 deg)
{
    float3 r = radians(deg);
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);

    // row-vector convention (v * M), same handedness as XMMatrixRotation*
    float3x3 Rx = float3x3(1, 0, 0,
                           0, cx, sx,
                           0, -sx, cx);
    float3x3 Ry = float3x3(cy, 0, -sy,
                           0, 1, 0,
                           sy, 0, cy);
    float3x3 Rz = float3x3(cz, sz, 0,
                           -sz, cz, 0,
                           0, 0, 1);
    return mul(mul(Rx, Ry), Rz);
}

VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    uint particleIndex = aliveCube[instanceID];
    GPUParticle p = particles[particleIndex];

    if (p.isAlive < 0.5)
    {
        o.Position = float4(0, 0, -1, 1);
        return o;
    }

    float3x3 R = RotationXYZ(p.rot3);
    float3 worldPos = mul(input.Position * p.size, R) + p.position;

    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(mul(input.Normal, R));
    o.Tangent = normalize(mul(input.Tangent, R));
    o.UV = input.UV;
    o.Color = float4(p.color.rgb, 1.0);
    return o;
}
