// ============================================================
// TerrainPS.hlsl - textured ground / cliffs (2026-10-03)
// The battle terrain (CreateSteppedGrid + the merged plateau / ramp /
// roof boxes) used to be vertex colour only. Now each pixel picks one of
// 5 layers of a Texture2DArray (per stage, TerrainSurface):
//   0 ground (flat), 1 path (ramps), 2 cliff (steep faces),
//   3 cave floor (flat below g_CaveY), 4 rock (steep below g_CaveY, cave
//   walls and roof)
// The layer comes from the vertex (UV.x = layer + 1) when the mesh builder
// set one, otherwise from the normal and the height.
// Sampling is triplanar in world space (meters / g_Layer[i].x per repeat):
// the stepped ground is axis aligned, the ramps are not.
// Colour: the texture's own colour, times the vertex colour's luminance
// over the layer's reference luminance (g_Layer[i].y), so the old colour
// patches stay as light / dark variation and break up the tiling.
// Normal map: whiteout / UDN triplanar blend.
// b0 LightBuffer (Lighting.hlsli), b2 TerrainCB (TerrainSurface::TerrainCB)
// ============================================================
#define MODEL_NO_TEXTURES
#include "../Common/ModelCommon.hlsli"
#include "../Common/Lighting.hlsli"

Texture2DArray terrainAlbedo : register(t0);
Texture2DArray terrainNormal : register(t1);

static const uint TERRAIN_LAYERS = 5u;
static const uint LAYER_GROUND = 0u;
static const uint LAYER_PATH = 1u;
static const uint LAYER_CLIFF = 2u;
static const uint LAYER_CAVE_FLOOR = 3u;
static const uint LAYER_ROCK = 4u;

cbuffer TerrainCB : register(b2)
{
    float4 g_Layer[TERRAIN_LAYERS]; // x meters per repeat, y reference luminance of the vertex colour, z brightness gain (texture average -> old palette brightness)
    float g_TintStrength;   // 0 = texture colour only, 1 = full luminance ratio
    float g_NormalStrength; // tangent xy scale of the normal map
    float g_FlatNy;         // normal.y above this = flat (ground / cave floor)
    float g_PathNy;         // normal.y above this = ramp (path), below = cliff / rock
    float g_CaveY;          // world y below this = inside the mine
    float g_FlipNormalY;    // 1 = normal map green points down (DirectX style)
    float2 _terrainPad;
};

uint TerrainLayer(float3 N, float3 P, float uvx)
{
    uint explicitLayer = (uint) (uvx + 0.5);
    bool cave = P.y < g_CaveY;
    uint autoLayer = (N.y > g_FlatNy) ? (cave ? LAYER_CAVE_FLOOR : LAYER_GROUND)
                   : (N.y > g_PathNy) ? LAYER_PATH
                   : (cave ? LAYER_ROCK : LAYER_CLIFF);
    return (explicitLayer > 0u) ? min(explicitLayer - 1u, TERRAIN_LAYERS - 1u) : autoLayer;
}

float3 UnpackNormal(float4 t)
{
    float3 n = t.xyz * 2.0 - 1.0;
    n.y = (g_FlipNormalY > 0.5) ? -n.y : n.y;
    n.xy *= g_NormalStrength;
    return n;
}

float4 main(PS_INPUT input) : SV_TARGET
{
    float3 N = normalize(input.Normal);
    float3 P = input.WorldPos;
    uint layer = TerrainLayer(N, P, input.UV.x);
    float tile = max(g_Layer[layer].x, 0.1);

    // triplanar weights (sharpened so the flat ground takes only the top projection)
    float3 w = pow(abs(N), 4.0);
    w /= (w.x + w.y + w.z);
    float2 uvX = P.zy / tile;
    float2 uvY = P.xz / tile;
    float2 uvZ = P.xy / tile;
    float L = (float) layer;

    float3 albedo = 0.0;
    float3 nX = 0.0, nY = 0.0, nZ = 0.0;
    if (w.x > 0.001)
    {
        albedo += terrainAlbedo.Sample(samplerState, float3(uvX, L)).rgb * w.x;
        nX = UnpackNormal(terrainNormal.Sample(samplerState, float3(uvX, L)));
    }
    if (w.y > 0.001)
    {
        albedo += terrainAlbedo.Sample(samplerState, float3(uvY, L)).rgb * w.y;
        nY = UnpackNormal(terrainNormal.Sample(samplerState, float3(uvY, L)));
    }
    if (w.z > 0.001)
    {
        albedo += terrainAlbedo.Sample(samplerState, float3(uvZ, L)).rgb * w.z;
        nZ = UnpackNormal(terrainNormal.Sample(samplerState, float3(uvZ, L)));
    }

    // whiteout blend: tangent xy added onto the world normal's matching components
    float3 tX = float3(nX.xy + N.zy, abs(nX.z) * N.x);
    float3 tY = float3(nY.xy + N.xz, abs(nY.z) * N.y);
    float3 tZ = float3(nZ.xy + N.xy, abs(nZ.z) * N.z);
    float3 Nd = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);
    if (any(isnan(Nd)))
        Nd = N;

    // the old colour patches as light / dark variation
    float lum = dot(input.Color.rgb, float3(0.2126, 0.7152, 0.0722));
    float ratio = clamp(lum / max(g_Layer[layer].y, 1e-3), 0.55, 1.45);
    float tint = lerp(1.0, ratio, g_TintStrength);

    float3 col = DecodeAlbedo(albedo) * (tint * g_Layer[layer].z);
    float3 lit = ShadeLambert(Nd, col, P);
    return float4(lit, 1.0);
}
