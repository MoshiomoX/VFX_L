// ============================================
// ParticleCommon.hlsli
// Shared definitions of the GPU particle system:
// structs + random functions + utilities
// ============================================

#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// ============================================
// renderMode (GPUParticle / GPUEmitter). Must match ParticleRenderMode
// and kParticleMesh* in GPUParticle.h
//   bits 0-7   : 0 = billboard / 1.. = mesh table index + 1 (0 = built-in cube)
//   bit  8     : glow (additive, unlit, fades with colour alpha)
//   bit  9     : face the velocity (forward axis follows it, rot3 unused)
//   bits 10-11 : forward axis (0 = +X / 1 = +Y / 2 = +Z / 3 = -Z, model Y stays up)
// Mesh particles are sorted into buckets = table index * 2 + glow; every
// bucket owns PARTICLE_MESH_BUCKET_CAP entries of the alive-mesh list
// ============================================
#define PARTICLE_MESH_SLOTS      16u
#define PARTICLE_MESH_BUCKET_CAP 16384u

// billboard only (bits 0-7 == 0): alpha blended instead of additive.
// Must match ParticleRenderMode::kBillboardAlpha
#define PARTICLE_BILLBOARD_ALPHA (1u << 12)

// any render mode: add the source's velocity (GPU projectile / area / orb)
// to the initial velocity. Only SwarmEmitCS reads it.
// Must match ParticleRenderMode::kInheritSourceVelocity
#define PARTICLE_INHERIT_SOURCE_VELOCITY (1u << 13)

// mesh glow only (2026-10-16): draw back faces only, so the mesh shows as a
// rim behind the opaque body (stone shot's purple shell). Same bit as the
// billboard toon flag; meshes never use the toon bits. Must match
// ParticleRenderMode::kMeshBehind. ParticleMeshVS passes it to the glow PS
#define PARTICLE_MESH_BEHIND (1u << 14)

// single body (2026-10-16): on a GPU projectile exactly one particle per
// frame, alive until the next frame = always exactly one drawn (rock, arrow,
// crystal). Emit rate / lifetime of the emitter are ignored. Only
// SwarmEmitCS reads it. Must match ParticleRenderMode::kSingleBody
#define PARTICLE_SINGLE_BODY (1u << 30)

// billboard only: toon look, bits 14-25 (2026-10-04). Decoded in
// GPUParticlePS (PARTICLE_TOON_*), packed by ParticleRenderMode::PackToon

// billboard only (2026-10-20, the Elden Ring style wind streaks): the quad is
// stretched along the particle's velocity instead of facing the camera
// upright. bit 9 (meshes use it for "face the velocity"; billboards never
// did) = streak, bits 16-23 = stretch in PARTICLE_STRETCH_UNIT seconds
// (length = speed x stretch, at least the size). Excludes the toon bits
// (16-25), so a streak is never toon. Must match ParticleRenderMode::kStreak
#define PARTICLE_STREAK (1u << 9)
#define PARTICLE_STRETCH_SHIFT 16u
#define PARTICLE_STRETCH_UNIT 0.004
bool ParticleIsStreak(int renderMode)
{
    uint m = (uint) renderMode;
    return (m & 0xFFu) == 0u && (m & PARTICLE_STREAK) != 0u;
}
float ParticleStretch(int renderMode)
{
    return (float) ((((uint) renderMode) >> PARTICLE_STRETCH_SHIFT) & 255u) * PARTICLE_STRETCH_UNIT;
}

// billboard only (2026-10-20): screen distortion (refraction) pass.
// bits 10-11 = strength level 0..3 (meshes use them for the forward axis),
// bit 15 = distortion only (invisible in the colour passes; the toon outline
// bit, meaningless without the toon bit 14). GPUParticleVS g_Pass 3 draws
// only the particles with a level > 0 into the offset target
// (ParticleDistortPS); Graphics::ApplyDistortion then bends the scene by it,
// after the 3D scene and before the UI (so the HUD is never bent).
// UpdateCS counts these particles (GPUParticleSystem::HasDistortion).
// Must match ParticleRenderMode::kDistortShift / kDistortOnly
#define PARTICLE_DISTORT_SHIFT 10u
#define PARTICLE_DISTORT_ONLY (1u << 15)
uint ParticleDistortLevel(int renderMode)
{
    uint m = (uint) renderMode;
    if ((m & 0xFFu) != 0u)
        return 0u;
    return (m >> PARTICLE_DISTORT_SHIFT) & 3u;
}
bool ParticleDistortOnly(int renderMode)
{
    uint m = (uint) renderMode;
    return (m & 0xFFu) == 0u && (m & (1u << 14)) == 0u && (m & PARTICLE_DISTORT_ONLY) != 0u;
}

// any render mode: wind response, bits 26-29 = q (2026-10-12). UpdateCS
// relaxes the horizontal velocity toward the world wind at
// q / 15 * PARTICLE_WIND_RATE per second. 0 = ignores the wind.
// Must match ParticleRenderMode::kWindShift / PackWind
#define PARTICLE_WIND_SHIFT 26u
#define PARTICLE_WIND_RATE  3.0
float ParticleWindResponse(int renderMode)
{
    return (float) ((((uint) renderMode) >> PARTICLE_WIND_SHIFT) & 15u) / 15.0;
}

// ============================================
// Which atlas cell a particle shows (GPUEmitter.frameMode).
// Must match ParticleFrameMode in GPUParticle.h.
//   atlasIndex = first cell, frameCount = number of cells
// While animating, particle.atlasAnimate = (first << 12) | count and
// UpdateCS advances uvFrame over the lifetime. 0 = uvFrame stays put.
// ============================================
#define PARTICLE_FRAME_LEGACY  0 // atlasIndex >= 0 fixed, -1 = every cell of the grid over the lifetime
#define PARTICLE_FRAME_FIXED   1
#define PARTICLE_FRAME_ANIMATE 2
#define PARTICLE_FRAME_RANDOM  3

uint ParticleMeshSlotPlusOne(int renderMode)
{
    return ((uint) renderMode) & 0xFFu;
}

uint ParticleMeshBucket(int renderMode)
{
    uint slot = ParticleMeshSlotPlusOne(renderMode) - 1u;
    if (slot >= PARTICLE_MESH_SLOTS)
        slot = 0u; // unknown table index: draw it as the cube
    return slot * 2u + ((((uint) renderMode) >> 8u) & 1u);
}

// ============================================
// GPU particle (matches the C++ GPUParticle)
// 192 bytes (the static_assert in GPUParticle.h is the real guard)
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
    
    int textureIndex;
    int atlasRows;
    int atlasCols;
    int atlasAnimate;
    
    int colorKeyOffset;
    int colorKeyCount;
    int ownerID;
    int renderMode; // see PARTICLE_MESH_* above (0 billboard)
    // mesh only: 3-axis rotation in degrees. billboard keeps rotation / angularVel
    float3 rot3;
    uint trailStyle; // 0 = no trail, else style index + 1 (see ParticleTrail.hlsli)
    float3 angVel3;
    uint trailState; // bits 0-7 ring write position / bits 8-15 valid sample count
};
// ============================================
// GPU emitter (matches the C++ GPUEmitter, 336 bytes).
// maxParticles / particleOffset are never read by any shader (layout only)
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
    int sourceId;
    int sourceCount;
    float isActive;
    int emitterID;
    int atlasRows;
    int atlasCols;
    int atlasIndex;
    int textureIndex;
    int colorKeyoffset;
    int colorKeyCount;
    int ownerID;
    int frameCount; // frames used by PARTICLE_FRAME_ANIMATE / _RANDOM (was _pad2)
    // Mesh emit: model world matrix. row_major = same memory order as C++ Matrix
    row_major float4x4 world;
    int edgeMode; // 0 all vertices / 1 dissolve edge only
    int renderMode; // copied to the particle (see PARTICLE_MESH_*)
    int trailStyle; // 0 = no trail, else style index + 1. copied to the particle
    int frameMode; // PARTICLE_FRAME_* (was _padS2)
    // sweep emit: vector from this frame's emit position back to last
    // frame's. EmitCS scatters along it so a fast emitter leaves no gaps.
    // 0 = emit from a single point as before
    float3 sweep;
    float _padT;
};

// ============================================
// Mesh emit source vertex layout (C++ EmitSourceLayout)
// byte offsets into a raw vertex buffer
// ============================================
struct EmitSourceLayout
{
    uint stride;
    uint posOffset;
    uint normalOffset;
    uint uvOffset;
    uint triangleCount; // 0 = vertex sampling only
    uint indexBytes; // 2 or 4
    uint _pad0;
    uint _pad1;
};
// ============================================
// ColorKey (a key frame of the colour over the lifetime)
// ============================================
struct ColorKey
{
    float4 color;
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
};
// ============================================
// GPU collider (the C++ GPUCollider was removed; nothing uses this struct)
// 48 bytes
// ============================================
struct GPUCollider
{
    float3 center; // Sphere: centre / AABB: min / Capsule: start
    float radius; // Sphere / Capsule: radius

    float3 extent; // AABB: max / Capsule: end
    int type; // 0=Sphere, 1=AABB, 2=Capsule

    float _pad0;
    float _pad1;
    float _pad2;
    float _pad3;
};
// ============================================
// Constant buffers (the buffers themselves are declared in each .hlsl)
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
// Random numbers (Wang hash)
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

// advance the seed and return a random number in 0.0 .. 1.0
float Random(inout uint seed)
{
    seed = WangHash(seed);
    return float(seed) / 4294967295.0;
}

// a random number in min .. max
float RandomRange(inout uint seed, float minVal, float maxVal)
{
    return lerp(minVal, maxVal, Random(seed));
}

// ============================================
// Pick the atlas cell(s) for a new particle (both emit paths call this).
// Uses its own seed so the attribute stream of the caller stays unchanged
// ============================================
void InitParticleFrame(int frameMode, int atlasIndex, int frameCount,
                       int atlasRows, int atlasCols, uint seed,
                       inout int uvFrame, inout int atlasAnimate)
{
    int first = max(atlasIndex, 0);
    int count = clamp(frameCount, 1, 4095);
    uvFrame = first;
    atlasAnimate = 0;

    if (frameMode == PARTICLE_FRAME_ANIMATE)
    {
        atlasAnimate = (first << 12) | count;
    }
    else if (frameMode == PARTICLE_FRAME_RANDOM)
    {
        uint frameSeed = seed ^ 0x85EBCA6Bu;
        uvFrame = first + min((int) (Random(frameSeed) * count), count - 1);
    }
    else if (frameMode == PARTICLE_FRAME_LEGACY && atlasIndex < 0)
    {
        uvFrame = 0;
        atlasAnimate = clamp(atlasRows * atlasCols, 1, 4095);
    }
}

// a random float3, each component in -1 .. 1
float3 RandomFloat3(inout uint seed)
{
    float x = Random(seed) * 2.0 - 1.0;
    float y = Random(seed) * 2.0 - 1.0;
    float z = Random(seed) * 2.0 - 1.0;
    return float3(x, y, z);
}

// a random direction on the unit sphere
float3 RandomOnSphere(inout uint seed)
{
    float z = Random(seed) * 2.0 - 1.0;
    float phi = Random(seed) * 6.28318530718;
    float r = sqrt(1.0 - z * z);
    return float3(r * cos(phi), r * sin(phi), z);
}

// a random point inside the unit sphere
float3 RandomInSphere(inout uint seed)
{
    float3 dir = RandomOnSphere(seed);
    float t = pow(Random(seed), 1.0 / 3.0);
    return dir * t;
}

// a random direction within the spread angle around dir
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

// a random float4 colour between min and max (per channel)
float4 RandomColor(inout uint seed, float4 minColor, float4 maxColor)
{
    float4 c;
    c.r = RandomRange(seed, minColor.r, maxColor.r);
    c.g = RandomRange(seed, minColor.g, maxColor.g);
    c.b = RandomRange(seed, minColor.b, maxColor.b);
    c.a = RandomRange(seed, minColor.a, maxColor.a);
    return c;
}

// age / lifetime (0.0 .. 1.0)
float GetLifeRatio(GPUParticle p)
{
    return (p.lifetime > 0.0) ? saturate(p.age / p.lifetime) : 1.0;
}

#endif