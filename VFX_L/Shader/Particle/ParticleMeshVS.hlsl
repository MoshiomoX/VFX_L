// ============================================
// ParticleMeshVS.hlsl
// メッシュパーティクルを描く: 登録したモデル（VERTEX_3D）のサブメッシュ 1 つを、
// 1 つのバケツの生きているパーティクルごとに 1 インスタンスで描く
// （DrawIndexedInstancedIndirect。数は meshCounts から来る）。
//   local = (vertex - center) * invExtent * size   （一番長い辺 = size）
//   world = local * R + position
//   R = RotXYZ(rot3)、または「速度の方を向く」: 前方の軸を速度に合わせる
//       （rot3 は使わない。ほぼ止まっているときは rot3 に戻る）
// 標準のモデルの VS_OUTPUT を出すので、PS.hlsl（Lambert、受光パス）と
// ParticleMeshGlowPS（加算パス）でシェーディングできる。
//
// b0/b1 は ParticleCommon が使うので MVP のブロックは b2 に移す（World は
// 使わない: インスタンスごとのワールド行列はここで作る）。MeshDrawCB は b3。
// ============================================
#include "Common/ParticleCommon.hlsli"
#define MODEL_MVP_CB_REG b2
#include "../Common/ModelCommon.hlsli"

// 1 回の描画 = 1 つのバケツのサブメッシュ 1 つ
cbuffer MeshDrawCB : register(b3)
{
    uint g_BucketBase; // bucket * PARTICLE_MESH_BUCKET_CAP
    uint g_InstanceCap; // PARTICLE_MESH_BUCKET_CAP
    float g_InvExtent; // 1 / モデルのバウンディングの一番長い辺
    float _padM0;
    float3 g_Center; // モデルのバウンディングの中心
    float _padM1;
};

// t0..t4 はモデルのテクスチャ（ModelCommon が宣言、VS では使わない）
StructuredBuffer<GPUParticle> particles : register(t5);
StructuredBuffer<uint> aliveMesh : register(t6);

float3x3 RotationXYZ(float3 deg)
{
    float3 r = radians(deg);
    float cx = cos(r.x), sx = sin(r.x);
    float cy = cos(r.y), sy = sin(r.y);
    float cz = cos(r.z), sz = sin(r.z);
    // 行ベクトルの規約（v * M）、XMMatrixRotation* と同じ手系
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

// 各行 = ローカルの X / Y / Z の行き先。ローカルの `axis` を `fwd` に合わせる。
// look-to の基底（x, y, z = fwd）を作ってから 90 度回し、選んだ軸が
// fwd を向くようにする。行列式は +1 のまま
float3x3 FaceDirection(float3 fwd, uint axis)
{
    float3 z = fwd;
    float3 up = (abs(z.y) < 0.99) ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 x = normalize(cross(up, z));
    float3 y = cross(z, x);
    // 出口は 1 つ: ここで途中 return すると fxc が X4000 を出す
    float3x3 R = float3x3(x, y, z); // ローカル +Z -> fwd
    if (axis == 0u)
        R = float3x3(z, y, -x); // ローカル +X -> fwd
    else if (axis == 1u)
        R = float3x3(x, z, -y); // ローカル +Y -> fwd
    else if (axis == 3u)
        R = float3x3(-x, y, -z); // ローカル -Z -> fwd（ローカル +Y は上のまま: 地面に寝かせてモデリングした岩は寝たまま）
    return R;
}

VS_OUTPUT main(VS_INPUT input, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;
    o.Position = float4(0, 0, -1, 1); // 最後まで行かなければカリングされる

    // バケツの上限より先には何も書いていない（UpdateCS）
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
    // 発光パス: ParticleMeshGlowPS は接線を読まないので、.x で
    // 「裏面だけ」のフラグ（PARTICLE_MESH_BEHIND）を運ぶ。受光のパーティクルは
    // PS.hlsl のために本物の接線のまま
    if (((mode >> 8u) & 1u) != 0u)
        o.Tangent = float3((mode & PARTICLE_MESH_BEHIND) != 0u ? 1.0 : 0.0, 0.0, 0.0);
    o.UV = input.UV;
    // 受光パス: 不透明、アルファは使わない。発光パス: アルファで消えていく
    o.Color = p.color;
    return o;
}
