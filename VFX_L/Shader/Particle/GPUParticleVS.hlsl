    #include "Common/ParticleCommon.hlsli"

cbuffer ParticleRenderCB : register(b0)
{
    matrix g_View;
    matrix g_Projection;
    float3 g_CameraPosition;
    // Which billboards this draw takes (2026-10-09, fire / black smoke flickered):
    //   0 = all (one blended pass, as before)
    //   1 = solid toon only (drawn first, depth WRITTEN, PS cuts alpha < 0.5)
    //   2 = everything except solid toon (blended, depth read only)
    //   3 = distortion pass (2026-10-20): only particles with a distortion
    //       level, into the offset target (ParticleDistortPS); colour.a x level / 3
    // The alive list is filled with InterlockedAdd, so its order changes every
    // frame; nearly opaque toon puffs drawn in that order swap which one is on
    // top each frame. With depth written they occlude each other by distance.
    float g_Pass;
};

// same bit as PARTICLE_TOON in GPUParticlePS (ParticleRenderMode::kToon)
#define PARTICLE_TOON_BIT (1u << 14)
// toon puffs at least this opaque (start alpha x 1.3) count as solid
static const float SOLID_TOON_OPACITY = 0.85;

StructuredBuffer<GPUParticle> particles : register(t0);
StructuredBuffer<uint> aliveList : register(t1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 uv : TEXCOORD0;
    nointerpolation uint atlasRows : TEXCOORD1;
    nointerpolation uint atlasCols : TEXCOORD2;
    nointerpolation uint uvFrame : TEXCOORD3;
    nointerpolation uint sheet : TEXCOORD4; // ParticleSheets index (particle.textureIndex)
    nointerpolation uint alphaBlend : TEXCOORD5; // 1 = alpha blended, 0 = additive
    nointerpolation uint renderMode : TEXCOORD6; // toon bits (PARTICLE_TOON_*, GPUParticlePS)
    // toon: x = life left relative to the start alpha (erodes the shape), y = opacity (start alpha x 1.3)
    nointerpolation float2 toonFade : TEXCOORD7;
    nointerpolation uint solidPass : TEXCOORD8;  // 1 = drawn in the depth-writing pass (PS cuts alpha < 0.5)
};

VSOutput main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VSOutput output = (VSOutput) 0;

    uint particleIndex = aliveList[instanceID]; // ← 改成用 instanceID
    GPUParticle p = particles[particleIndex];

    if (p.isAlive < 0.5)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }

    static const float2 corners[6] =
    {
        float2(-1, 1), float2(1, 1), float2(-1, -1),
        float2(-1, -1), float2(1, 1), float2(1, -1),
    };

    static const float2 uvs[6] =
    {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };

    uint cornerIndex = vertexID; // ← 直接用 vertexID（0~5）
    float2 corner = corners[cornerIndex];
    output.uv = uvs[cornerIndex];

    output.atlasRows = p.atlasRows;
    output.atlasCols = p.atlasCols;
    output.uvFrame = p.uvFrame;
    output.sheet = (uint) max(p.textureIndex, 0);
    output.alphaBlend = (((uint) p.renderMode) & PARTICLE_BILLBOARD_ALPHA) ? 1u : 0u;
    output.renderMode = (uint) p.renderMode;
    float startA = max(p.startColor.a, 1e-3);
    output.toonFade = float2(saturate(p.color.a / startA), saturate(startA * 1.3));

    // pass split (see g_Pass): a particle not taken by this pass becomes a zero-area triangle
    bool solid = (((uint) p.renderMode) & PARTICLE_TOON_BIT) != 0u && output.alphaBlend != 0u
              && output.toonFade.y >= SOLID_TOON_OPACITY;
    uint distortLevel = ParticleDistortLevel(p.renderMode);
    bool distortOnly = ParticleDistortOnly(p.renderMode);
    bool distortPass = (g_Pass > 2.5);
    if (distortPass)
    {
        if (distortLevel == 0u)
        {
            output.position = float4(0, 0, -1, 1);
            return output;
        }
        // folded strength; the PS reads it. A distort-only puff carries the strength in colour.a (nothing
        // else reads it). A visible one keeps a faint colour.a for the colour pass, so the refraction uses
        // the life fade (colour.a / start a) instead and stays strong (2026-10-20: wind = refraction + a little white)
        float fadeNow = distortOnly ? p.color.a : output.toonFade.x;
        output.color.a = fadeNow * (float) distortLevel / 3.0;
    }
    else if ((g_Pass > 0.5 && g_Pass < 1.5 && !solid) || (g_Pass > 1.5 && solid) || distortOnly)
    {
        output.position = float4(0, 0, -1, 1);
        return output;
    }
    output.solidPass = (solid && g_Pass > 0.5 && !distortPass) ? 1u : 0u;

    float3 camRight = float3(g_View[0][0], g_View[1][0], g_View[2][0]);
    float3 camUp = float3(g_View[0][1], g_View[1][1], g_View[2][1]);

    float3 worldPos;
    if (ParticleIsStreak(p.renderMode))
    {
        // streak (2026-10-20): the long axis follows the velocity (projected onto the view
        // plane so the quad still faces the camera), the short axis = size. uv top = the head.
        // A particle that has not started moving yet is drawn upright
        float3 v = p.velocity;
        float sp = length(v);
        float3 toCam = normalize(g_CameraPosition - p.position);
        float3 axis = (sp > 1e-3) ? v / sp : camUp;
        axis -= toCam * dot(axis, toCam);
        float al = length(axis);
        axis = (al > 1e-3) ? axis / al : camUp;
        float3 side = normalize(cross(toCam, axis));
        float halfLen = max(p.size * 0.5, sp * ParticleStretch(p.renderMode) * 0.5);
        worldPos = p.position + axis * (corner.y * halfLen) + side * (corner.x * p.size * 0.5);
    }
    else
    {
        float cosR = cos(p.rotation);
        float sinR = sin(p.rotation);
        float2 rotated;
        rotated.x = corner.x * cosR - corner.y * sinR;
        rotated.y = corner.x * sinR + corner.y * cosR;

        float halfSize = p.size * 0.5;
        worldPos = p.position
                 + camRight * rotated.x * halfSize
                 + camUp * rotated.y * halfSize;
    }

    float4 viewPos = mul(float4(worldPos, 1.0), g_View);
    output.position = mul(viewPos, g_Projection);
    output.color = distortPass ? float4(p.color.rgb, output.color.a) : p.color;

    return output;
}
