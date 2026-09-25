// ============================================
// ParticleMeshVS.hlsl
// Draw mesh particles: one submesh of a registered model (VERTEX_3D)
// instanced once per alive particle of one bucket
// (DrawIndexedInstancedIndirect; the count comes from meshCounts).
//   local = (vertex - center) * invExtent * size   (longest side = size)
//   world = local * R + position
//   R = RotXYZ(rot3), or "face velocity": the forward axis follows the
//       velocity (rot3 unused; falls back to rot3 when nearly still)
// Outputs the standard model VS_OUTPUT so PS.hlsl (Lambert, lit pass)
// and ParticleMeshGlowPS (additive pass) can shade it.
//
// ParticleCommon owns b0/b1, so the MVP block moves to b2 (World is
// unused: the per-instance world is built here). MeshDrawCB is b3.
// ============================================
#include "Common/ParticleCommon.hlsli"
#define MODEL_MVP_CB_REG b2
#include "../Common/ModelCommon.hlsli"

// one draw = one submesh of one bucket
cbuffer MeshDrawCB : register(b3)
{
    uint g_BucketBase; // bucket * PARTICLE_MESH_BUCKET_CAP
    uint g_InstanceCap; // PARTICLE_MESH_BUCKET_CAP
    float g_InvExtent; // 1 / longest side of the model bounds
    float _padM0;
    float3 g_Center; // model bounds center
    float _padM1;
};

// t0..t4 are the model textures (declared by ModelCommon, unused in VS)
StructuredBuffer<GPUParticle> particles : register(t5);
StructuredBuffer<uint> aliveMesh : register(t6);

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

// rows = where local X / Y / Z go. Local `axis` is mapped onto `fwd`.
// Built as a look-to basis (x, y, z = fwd) and then rotated by 90 degrees
// so the chosen axis carries fwd; the determinant stays +1
float3x3 FaceDirection(float3 fwd, uint axis)
{
    float3 z = fwd;
    float3 up = (abs(z.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 x = normalize(cross(up, z));
    float3 y = cross(z, x);
    // single exit: fxc warns X4000 on early returns here
    float3x3 R = float3x3(x, y, z); // local +Z -> fwd
    if (axis == 0u)
        R = float3x3(z, y, -x); // local +X -> fwd
    else if (axis == 1u)
        R = float3x3(x, z, -y); // local +Y -> fwd
    return R;
}

VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    o.Position = float4(0, 0, -1, 1); // culled unless we get to the end

    // past the bucket cap nothing was written (UpdateCS)
    if (instanceID >= g_InstanceCap)
        return o;

    uint particleIndex = aliveMesh[g_BucketBase + instanceID];
    GPUParticle p = particles[particleIndex];
    if (p.isAlive < 0.5)
        return o;

    uint mode = (uint) p.renderMode;
    float3x3 R = RotationXYZ(p.rot3);
    float speedSq = dot(p.velocity, p.velocity);
    if (((mode >> 9u) & 1u) != 0u && speedSq > 1e-6)
        R = FaceDirection(p.velocity * rsqrt(speedSq), (mode >> 10u) & 3u);

    float3 local = (input.Position - g_Center) * (g_InvExtent * p.size);
    float3 worldPos = mul(local, R) + p.position;
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = normalize(mul(input.Normal, R));
    o.Tangent = normalize(mul(input.Tangent, R));
    o.UV = input.UV;
    // lit pass: opaque, alpha unused. glow pass: alpha fades it out
    o.Color = p.color;
    return o;
}
