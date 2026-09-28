#pragma once
#include <cstddef>
#include <SimpleMath.h>

using namespace DirectX::SimpleMath;

struct DirectionalLight
{
	// デフォルトは上からの白色光
    Vector3 direction = { 0.0f, -1.0f, 0.0f };
	float padding1;// パディング（16バイトアライメント用）
	Vector3 color = { 1.0f, 1.0f, 1.0f };// 光の色
	float intensity = 1.0f;// 光の強さ
};

struct LightBuffer
{
    DirectionalLight directionalLight;          // 平行光源
    Vector3 ambientColor = { 0.1f, 0.1f, 0.1f };// 環境光（半球の上 = 空の色。上向きの面ほどこちら）
    float   padding = 0.0f;                     // 16バイト境界
    Vector3 cameraPosition;                     // カメラ位置
    float   padding2 = 0.0f;                    // 16バイト境界

    // 環境光の半球の下 = 地面の色（下向きの面ほどこちら）。
    // Renderer::SetAmbientColor は両方に同じ色を入れる（= 従来の一様な環境光）
    Vector3 groundAmbientColor = { 0.1f, 0.1f, 0.1f };
    float   padding5 = 0.0f;                    // 16バイト境界

    // 距離の霧（Lighting.hlsli の ApplyFog。陰影の後で fogColor へ寄せる）。
    // fogMax = 0 で無し（既定。戦闘場面だけ SceneLighting が入れ、終わる時に切る）
    Vector3 fogColor = { 0.0f, 0.0f, 0.0f };    // 線形 HDR（空の地平線の色に合わせる）
    float   fogStart = 0.0f;                    // カメラからの距離 m。ここから濃くなり始める
    float   fogEnd = 1.0f;                      // ここで fogMax
    float   fogMax = 0.0f;                      // 0..1
    // 1 = 模型の色貼図を sRGB として線形へ戻してから陰影を付ける（PS / PBR_PS の DecodeAlbedo）。
    //     貼図は UNORM で読んでいるので、0 のままだと最後のガンマで色が白っぽく浮く
    float   albedoSrgb = 0.0f;
    float   fogPad = 0.0f;                      // 16バイト境界

    // 太陽の影（3 段の級聯。Graphics/Light/ShadowMap が毎フレーム書く。Lighting.hlsli の SunShadow）。
    // 影図そのものは PS t9（Texture2DArray）+ 比較用サンプラー s2 で、ここは行列と調整値だけ。
    // shadowSplits.w = 0 で影無し（既定。戦闘場面だけ ShadowMap が入れ、終わる時に切る）。
    // HLSL の LightBuffer（Shader/Common/Lighting.hlsli）はここまで。以下は C++ だけ
    Matrix  shadowViewProj[3];                  // 各段の 光源 view * 正射影（行ベクトル規約）
    Vector4 shadowSplits = { 0, 0, 0, 0 };      // x/y/z = 各段の受け持ち（カメラからの距離 m）, w = 1 で有効
    Vector4 shadowTexelWorld = { 0, 0, 0, 0 };  // x/y/z = 各段の影図 1 texel が世界で何 m か（法線ずらしの単位）
    Vector4 shadowParams = { 0, 0, 0, 0 };      // x = 1 / 影図の辺, y = 法線方向へのずらし（texel 数）,
                                                // z = 濃さ 0..1, w = PCF の半径（texel。0 = 1 点）
    Vector4 shadowParams2 = { 0, 0, 0, 0 };     // x = 最後の段で薄くし始める割合（距離 / 最後の段の距離）,
                                                // y = 比較の深度バイアス, z = 1 で段ごとに色を付ける（調整用）, w = 未使用

    // ★ここから追加：各テクスチャの有無（1=あり, 0=なし）
    float   hasAlbedo = 0.0f;
    float   hasNormal = 0.0f;
    float   hasMetallic = 0.0f;
    float   hasRoughness = 0.0f;                // ← この4つで1行(16byte)

    float   hasAO = 0.0f;
    Vector3 padding3 = { 0,0,0 };           // ← float + float3 で1行(16byte)

    // ★テクスチャ欠落時のフォールバック値
    Vector3 defaultAlbedo = { 1.0f, 1.0f, 1.0f };
    float   defaultMetallic = 0.0f;            // ← float3 + float で1行(16byte)

    float   defaultRoughness = 1.0f;
    Vector3 padding4 = { 0,0,0 };       // ← float + float3 で1行(16byte)
};
// HLSL の cbuffer は先頭からの並びをそのまま読む（Shader::WriteBuffer は反射した大きさだけ写す）
static_assert(offsetof(LightBuffer, shadowViewProj) == 112, "LightBuffer: shadow block must follow the fog block");
static_assert(offsetof(LightBuffer, hasAlbedo) == 368, "LightBuffer: HLSL part must end at 368 bytes");