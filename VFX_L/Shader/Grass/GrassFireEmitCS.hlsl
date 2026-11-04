// ============================================================
// GrassFireEmitCS.hlsl
// 燃えている草から出るパーティクル（World/GrassFireField、2026-10-12）。
// 炎のマップの 1 テクセルに 1 スレッド。カメラの近くで燃えているテクセルは、
// GrassFire のレシピ（VFXId::GrassFire）の全エミッタから地面の高さで
// 発生率 x 強さ x g_RateMul だけ出す。
// 砂漠の砂（World/SandField、2026-11-02/03）でも走らせる: 陽炎は
// 炎と同じく z を読み、舞う砂（g_Channel 1）は w を読む。
//
// SwarmSystem（Swarm/SwarmSystemFire.cpp）が弾 / 範囲のエミッタの直後に
// ディスパッチするので、空きプールの予約（emitBudget）を共有する:
// 炎がデッドリストを取りすぎることはない。
// ============================================================
#include "../Particle/Common/EmitInit.hlsli"

// SwarmSystem::FireEmitCB と一致させること（b2。b0 / b1 は ParticleCommon が使う）
cbuffer GrassFireEmitCB : register(b2)
{
    float2 g_MapOrigin;
    float2 g_MapSize;
    float3 g_CamPos;
    float g_EmitRadius;     // カメラからこれより遠いテクセルは何も出さない
    float g_Ignite;         // 燃焼中 = 温度がこれより上（GrassFireCS と同じ）
    float g_RateMul;        // 全エミッタの発生率に掛ける（燃えているテクセルあたり）
    uint g_FireVfx;         // VFXId::GrassFire のレシピ番号
    float g_HeightOffset;   // 地面からこれだけ上で出す
    uint g_Channel;         // 0 = 燃焼（x 温度、y 燃料、z 燃焼中）、1 = 舞う砂（w = 強さ）
    float3 _channelPad;
};

// = SwarmRecipe（Shader/Common/SwarmCommon.hlsli）、32 バイト
struct FireRecipe
{
    uint particleStart;
    uint particleCount;
    uint modelStart;
    uint modelCount;
    uint lightStart;
    uint lightCount;
    uint spriteStart;
    uint spriteCount;
};

Texture2D<float4> fireMap : register(t0);        // （温度、燃料、燃焼中）/ 砂: w = 舞っている量
Texture2D<float4> groundMap : register(t1);      // a = 草
StructuredBuffer<FireRecipe> recipes : register(t2);
StructuredBuffer<GPUEmitter> emitters : register(t3);
Buffer<uint> deadCount : register(t4);
Texture2D<float> heightMap : register(t5);       // GridWorld の高さ（GrassRenderer のテクスチャ）

RWStructuredBuffer<GPUParticle> particles : register(u0);
ConsumeStructuredBuffer<uint> deadList : register(u1);
RWByteAddressBuffer emitBudget : register(u2);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    fireMap.GetDimensions(w, h);
    if (id.x >= w || id.y >= h)
        return;

    float4 f = fireMap.Load(int3(id.xy, 0));
    if (g_Channel == 1u ? (f.w < 0.02) : (f.z < 0.5))
        return;

    float2 uv = (float2(id.xy) + 0.5) / float2(w, h);
    float2 xz = g_MapOrigin + uv * g_MapSize;
    if (distance(xz, g_CamPos.xz) > g_EmitRadius)
        return;

    uint gw, gh;
    groundMap.GetDimensions(gw, gh);
    if (groundMap.Load(int3(int2(uv * float2(gw, gh)), 0)).a < 0.5)
        return;

    FireRecipe r = recipes[g_FireVfx];
    if (r.particleCount == 0u)
        return;

    // 燃え始めは激しく燃え、燃料がほぼ尽きたテクセルはくすぶるだけ
    // （舞う砂: 砂のフィールドがもう強さを 0..1 で書いてある）
    float intensity = (g_Channel == 1u) ? f.w : saturate((f.x - g_Ignite) * 2.0) * saturate(f.y * 4.0);
    if (intensity <= 0.0)
        return;

    uint hw, hh;
    heightMap.GetDimensions(hw, hh);
    float groundY = heightMap.Load(int3(int2(uv * float2(hw, hh)), 0));
    float3 base = float3(xz.x, groundY + g_HeightOffset, xz.y);

    uint seed = g_BaseSeed ^ (id.x * 73856093u) ^ (id.y * 19349663u);

    for (uint ei = 0u; ei < r.particleCount; ++ei)
    {
        GPUEmitter e = emitters[r.particleStart + ei];
        e.position = base + e.position;   // エントリの位置は地面からのオフセット

        uint k = StochasticCount(e.emitRate * g_RateMul * intensity, g_DeltaTime, seed);
        if (k == 0u)
            continue;

        // ---- 空きプールから予約する（このフレームの SwarmEmitCS と共有） ----
        uint mine;
        emitBudget.InterlockedAdd(0, k, mine);
        uint freeNow = deadCount[0];
        if (mine >= freeNow)
            return;
        if (mine + k > freeNow)
            k = freeNow - mine;

        for (uint n = 0u; n < k; ++n)
        {
            uint particleIndex = deadList.Consume();
            uint s = seed + (ei * 131u + n) * 1099u;
            float3 pos = e.position;
            float3 vel = float3(0, 0, 0);
            EmitShape(e, s, pos, vel);
            particles[particleIndex] = InitParticleFromEmitter(e, s, pos, vel);
        }
    }
}
