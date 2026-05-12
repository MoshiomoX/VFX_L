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
	DirectionalLight directionalLight;// 平行光源
	Vector3 ambientColor = { 0.1f, 0.1f, 0.1f };// 環境光
	float padding;// パディング（16バイトアライメント用）
	Vector3 cameraPosition;/// カメラ位置
	float padding2;// パディング（16バイトアライメント用）
};