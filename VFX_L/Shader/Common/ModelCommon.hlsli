// ============================================================
// ModelCommon.hlsli
// 全モデルの VS / PS が共有するインターフェース：
//   - MVP 定数バッファ（VS b0、row_major、C++ の MVPBuffer と一致）
//   - 頂点入力 / VS から PS へのレイアウト
//   - テクスチャスロットの取り決め
//   - 標準のモデル VS 本体
//
// シェーダーがそのスロットを別のことに使うときは、#include の前に
// レジスタのマクロを上書きできる（SWARM_FRAME_CB_REG と
// 同じやり方）。
// ============================================================
#ifndef MODEL_COMMON_HLSLI
#define MODEL_COMMON_HLSLI

#ifndef MODEL_MVP_CB_REG
#define MODEL_MVP_CB_REG b0
#endif

cbuffer MVPBuffer : register(MODEL_MVP_CB_REG)
{
    row_major matrix World;
    row_major matrix View;
    row_major matrix Projection;
};

// ---- 頂点レイアウト（PrimitiveBuilder / モデルのローダー） ----
struct VS_INPUT
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
};

// ---- VS -> PS。全モデルのシェーダーで 1 つのレイアウト。 ----
// 接線を持たないシェーダーは 0 を書く。接線が要らない PS は
// 単に無視する。
struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD1;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD0;
    float4 Color : COLOR;
};
typedef VS_OUTPUT PS_INPUT;

// ---- テクスチャスロット（Material がスロット番号でバインドする） ----
//   t0 アルベド / t1 法線 / t2 メタリック / t3 ラフネス / t4 AO
// そこに別のものをバインドする PS（TerrainPS：Texture2DArray）は
// include の前に MODEL_NO_TEXTURES を定義する。
#ifndef MODEL_NO_TEXTURES
Texture2D albedoTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicTexture : register(t2);
Texture2D roughnessTexture : register(t3);
Texture2D aoTexture : register(t4);
#endif
SamplerState samplerState : register(s0);

// ---- 標準のモデル VS ----
VS_OUTPUT ModelVS(VS_INPUT input)
{
    VS_OUTPUT o;
    float4 worldPos = mul(float4(input.Position, 1.0), World);
    o.WorldPos = worldPos.xyz;
    o.Position = mul(mul(worldPos, View), Projection);
    o.Normal = normalize(mul(input.Normal, (float3x3) World));
    // 接線のないメッシュ（ゼロベクトル）は NaN ではなく 0 のままにする。PerturbNormal はそのとき法線を変えない
    float3 tw = mul(input.Tangent, (float3x3) World);
    o.Tangent = (dot(tw, tw) > 1e-12) ? normalize(tw) : float3(0.0, 0.0, 0.0);
    o.UV = input.UV;
    o.Color = input.Color;
    return o;
}

#endif