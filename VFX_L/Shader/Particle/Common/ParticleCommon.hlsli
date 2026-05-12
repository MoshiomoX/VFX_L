    // ============================================
// ParticleCommon.hlsli
// GPU粒子システム共通定義
// 構造体 + 乱数関数 + ユーティリティ
// ============================================

#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// ============================================
// GPU粒子構造体 (C++側のGPUParticleと一致)
// 128バイト
// ============================================
struct GPUParticle
{
    float3 position;
    float size;
    float3 velocity;
    float age;
    float3 acceleration;
    float drag;
    float4 color;
    float4 startColor;
    float4 endColor;
    float startSize;
    float endSize;
    float rotation;
    float angularVel;
    float lifetime;
    float isAlive;
    int uvFrame;
    uint seed;
};

// ============================================
// GPU発射器構造体 (C++側のGPUEmitterと一致)
// ============================================
struct GPUEmitter
{
    float3 position;
    int emitType;
    float3 direction;
    float spreadAngle;
    float3 shapeSize;
    float _pad0;
    int emitCount;
    int maxParticles;
    int particleOffset;
    float emitRate;
    float2 speedRange;
    float2 lifetimeRange;
    float4 sizeRange;
    float4 startColorMin;
    float4 startColorMax;
    float4 endColorMin;
    float4 endColorMax;
    float3 gravity;
    float dragCoeff;
    float2 rotationRange;
    float2 angularVelRange;
    int meshVertexOffset;
    int meshVertexCount;
    float isActive;
    int emitterID;
};

// ============================================
// Mesh発射用頂点
// ============================================
struct EmitMeshVertex
{
    float3 position;
    float area;
    float3 normal;
    float _pad0;
};

// ============================================
// 定数バッファ (Buffer宣言は各.hlslで行う)
// ============================================
cbuffer GlobalCB : register(b0)
{
    float g_DeltaTime;
    float g_TotalTime;
    uint g_BaseSeed;
    int g_EmitterCount;
};

cbuffer DeadListCB : register(b1)
{
    uint g_DeadCount;
    uint g_MaxParticles;
    uint g_DLPad0;
    uint g_DLPad1;
};

// ============================================
// 乱数生成 (Wang Hash)
// ============================================
uint WangHash(uint seed)
{
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

// シードを進めて0.0~1.0の乱数を返す
float Random(inout uint seed)
{
    seed = WangHash(seed);
    return float(seed) / 4294967295.0;
}

// min~maxの範囲の乱数
float RandomRange(inout uint seed, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, Random(seed));
}

// -1~1の範囲のfloat3乱数
float3 RandomFloat3(inout uint seed)
{
    float x = Random(seed) * 2.0 - 1.0;
    float y = Random(seed) * 2.0 - 1.0;
    float z = Random(seed) * 2.0 - 1.0;
    return float3(x, y, z);
}

// 単位球面上のランダムな方向
float3 RandomOnSphere(inout uint seed)
{
    float z = Random(seed) * 2.0 - 1.0;
    float phi = Random(seed) * 6.28318530718;
    float r = sqrt(1.0 - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

// 単位球内のランダムな点
float3 RandomInSphere(inout uint seed)
{
    float3 dir = RandomOnSphere(seed);
    float t = pow(Random(seed), 1.0 / 3.0);
    return dir * t;
}

// 指定方向を中心に拡散角内のランダムな方向
float3 RandomInCone(inout uint seed, float3 dir, float angleInDegrees)
{
    float angleRad = radians(angleInDegrees);
    float cosAngle = cos(angleRad);

    float z = lerp(cosAngle, 1.0, Random(seed));
    float phi = Random(seed) * 6.28318530718;
    float r = sqrt(1.0 - z * z);
    float3 localDir = float3(r * cos(phi), r * sin(phi), z);

    float3 up = abs(dir.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 right = normalize(cross(up, dir));
    up = cross(dir, right);

    return right * localDir.x + up * localDir.y + dir * localDir.z;
}

// float4の色をmin~maxでランダム生成
float4 RandomColor(inout uint seed, float4 minColor, float4 maxColor)
{
    float4 c;
    c.r = RandomRange(seed, minColor.r, maxColor.r);
    c.g = RandomRange(seed, minColor.g, maxColor.g);
    c.b = RandomRange(seed, minColor.b, maxColor.b);
    c.a = RandomRange(seed, minColor.a, maxColor.a);
    return c;
}

// 寿命比率 (0.0 ~ 1.0)
float GetLifeRatio(GPUParticle p)
{
    return (p.lifetime > 0.0) ? saturate(p.age / p.lifetime) : 1.0;
}

#endif