#pragma once
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
    // Renderer::SetAmbientColor は両方に同じ色を入れる（= 従来の一様な環境光）。
    // HLSL の LightBuffer（Shader/Common/Lighting.hlsli）はここまで。以下は C++ だけ
    Vector3 groundAmbientColor = { 0.1f, 0.1f, 0.1f };
    float   padding5 = 0.0f;                    // 16バイト境界

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