// ============================================================
// TerrainPS.hlsl - テクスチャ付きの地面 / 崖（2026-10-03）
// 戦闘の地形（CreateSteppedGrid + まとめた台地 / 坂 / 洞窟の屋根の箱）は
// 以前は頂点色だけだった。今は各ピクセルが Texture2DArray の 5 層から
// 1 つを選ぶ（ステージごと、TerrainSurface）:
//   0 地面（平ら）、1 道（坂）、2 崖（急な面）、
//   3 洞窟の床（g_CaveY より下の平ら）、4 岩（g_CaveY より下の急な面、洞窟の
//   壁と屋根）
// メッシュの生成側が層を指定した場合は頂点から（UV.x = 層 + 1）、
// そうでなければ法線と高さから決める。
// サンプリングはワールド空間のトライプラナー（繰り返し 1 回あたり g_Layer[i].x m）:
// 段々の地面は軸に揃っているが、坂はそうではないため。
// 色: テクスチャ自身の色に、頂点色の輝度 / 層の基準輝度（g_Layer[i].y）を
// 掛ける。旧来の色の斑が明暗のばらつきとして残り、タイリングを
// 目立たなくする。
// 法線マップ: whiteout / UDN のトライプラナーブレンド。
// b0 LightBuffer（Lighting.hlsli）、b2 TerrainCB（TerrainSurface::TerrainCB）
// ============================================================
#define MODEL_NO_TEXTURES
#include "../Common/ModelCommon.hlsli"
#include "../Common/Lighting.hlsli"

Texture2DArray terrainAlbedo : register(t0);
Texture2DArray terrainNormal : register(t1);
// 草レンダラーの地面マップ（a = ここに草が生える）、マップ全体。TerrainSurface::SetGrassCover がバインドする。
// 草の下の地面を暗くする（2026-11-02、ユーザー: 葉の間から明るい地面が見えるのは見栄えが悪い）
Texture2D<float4> g_GrassCover : register(t2);
// 砂漠の砂面（World/SandField、2026-11-02）。TerrainSurface::SetSandField がバインドする:
// t3 状態（x 温度、y 乾燥度 1 = 乾燥）、t4 へこみ（負 = 押し下げ）
Texture2D<float4> g_SandState : register(t3);
Texture2D<float> g_SandTrack : register(t4);

static const uint TERRAIN_LAYERS = 5u;
static const uint LAYER_GROUND = 0u;
static const uint LAYER_PATH = 1u;
static const uint LAYER_CLIFF = 2u;
static const uint LAYER_CAVE_FLOOR = 3u;
static const uint LAYER_ROCK = 4u;

cbuffer TerrainCB : register(b2)
{
    float4 g_Layer[TERRAIN_LAYERS]; // x 繰り返し 1 回あたりの m、y 頂点色の基準輝度、z 明るさの倍率（テクスチャの平均 -> 旧パレットの明るさ）
    float g_TintStrength;   // 0 = テクスチャの色だけ、1 = 輝度比を全部かける
    float g_NormalStrength; // 法線マップの接線 xy の倍率
    float g_FlatNy;         // normal.y がこれより上 = 平ら（地面 / 洞窟の床）
    float g_PathNy;         // normal.y がこれより上 = 坂（道）、下 = 崖 / 岩
    float g_CaveY;          // ワールド y がこれより下 = 洞窟の中
    float g_FlipNormalY;    // 1 = 法線マップの緑が下向き（DirectX 式）
    float g_GrassDark;      // 草の下の地面にこれを掛ける（1 = 無効）
    float g_GrassOn;        // > 0.5 = g_GrassCover がバインド済み
    float2 g_GrassOrigin;   // 草マップの最小の角のワールド xz
    float2 g_GrassSize;     // m
    float4 g_Sand;          // x > 0.5 = t3 / t4 がバインド済み、y 湿った砂の明るさ、z 熱い砂の色味、w へこみの法線の強さ
    float2 g_SandOrigin;
    float2 g_SandSize;
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

    // トライプラナーの重み（鋭くして、平らな地面は上からの投影だけを使う）
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

    // whiteout ブレンド: 接線 xy をワールド法線の対応する成分に足す
    float3 tX = float3(nX.xy + N.zy, abs(nX.z) * N.x);
    float3 tY = float3(nY.xy + N.xz, abs(nY.z) * N.y);
    float3 tZ = float3(nZ.xy + N.xy, abs(nZ.z) * N.z);
    float3 Nd = normalize(tX.zyx * w.x + tY.xzy * w.y + tZ.xyz * w.z);
    if (any(isnan(Nd)))
        Nd = N;

    // 旧来の色の斑を明暗のばらつきとして使う
    float lum = dot(input.Color.rgb, float3(0.2126, 0.7152, 0.0722));
    float ratio = clamp(lum / max(g_Layer[layer].y, 1e-3), 0.55, 1.45);
    float tint = lerp(1.0, ratio, g_TintStrength);

    float3 col = DecodeAlbedo(albedo) * (tint * g_Layer[layer].z);
    // 草の下: 地面を暗くして、葉の隙間が根元の陰に見えるようにする。
    // 上向きの地面だけ（草の生えた上面の下の崖面はそのまま）
    if (g_GrassOn > 0.5)
    {
        float2 guv = (P.xz - g_GrassOrigin) / g_GrassSize;
        float cover = g_GrassCover.SampleLevel(samplerState, guv, 0).a;
        cover *= saturate((N.y - g_PathNy) / max(1.0 - g_PathNy, 1e-3));
        col *= lerp(1.0, g_GrassDark, cover);
    }
    // 砂漠の砂: 湿り = 暗く少し寒色寄り、熱い = くすんだ赤寄り、へこみは法線を曲げる。
    // 上向きの地面だけ（砂であって、メサの崖ではない）
    if (g_Sand.x > 0.5)
    {
        float up = saturate((N.y - g_PathNy) / max(1.0 - g_PathNy, 1e-3));
        float2 suv = (P.xz - g_SandOrigin) / g_SandSize;
        float4 st = g_SandState.SampleLevel(samplerState, suv, 0);
        float wet = (1.0 - saturate(st.y)) * up;
        col *= lerp(float3(1.0, 1.0, 1.0), float3(g_Sand.y * 0.92, g_Sand.y * 0.97, g_Sand.y * 1.05), wet);
        float hot = saturate(st.x) * up * g_Sand.z;
        col = lerp(col, col * float3(1.25, 0.62, 0.42), hot);
        // へこみ: へこみマップの 1 テクセル分の中心差分
        uint tw, th;
        g_SandTrack.GetDimensions(tw, th);
        float2 du = float2(1.0 / tw, 0.0), dv = float2(0.0, 1.0 / th);
        float hx = g_SandTrack.SampleLevel(samplerState, suv + du, 0) - g_SandTrack.SampleLevel(samplerState, suv - du, 0);
        float hz = g_SandTrack.SampleLevel(samplerState, suv + dv, 0) - g_SandTrack.SampleLevel(samplerState, suv - dv, 0);
        float texel = g_SandSize.x / max((float) tw, 1.0);
        float2 slope = float2(hx, hz) / (2.0 * texel) * g_Sand.w * up;
        Nd = normalize(Nd - float3(slope.x, 0.0, slope.y));
        float dent = g_SandTrack.SampleLevel(samplerState, suv, 0);
        // へこみの底は暗く（曲げた法線はトゥーンの段階にほとんど飲まれるので、形はこれで見せる）
        col *= 1.0 - saturate(-dent * 1.6) * 0.45 * up;
    }
    float3 lit = ShadeLambert(Nd, col, P);
    return float4(lit, 1.0);
}
