// ============================================================
// SkyRenderer.h
// 全画面のグラデーションの空（Shader/Sky/SkyVS・SkyPS）。
//   場面の描画の一番最初に呼ぶ：深度を読まず書かず、画面全体を塗る
//   （クリア色の代わり。後から描く物は全部この上に乗る）。
//   色は線形 HDR（CompositePS が露出・トーンマップ・ガンマを掛ける）。
//   地平線の色は距離の霧の色にも使う（遠くの地形が空へ溶ける）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <d3d11.h>
#include <memory>

class VertexShader;
class PixelShader;
class CameraBase;

class SkyRenderer
{
public:
    struct Params
    {
        DirectX::SimpleMath::Vector3 zenith;
        DirectX::SimpleMath::Vector3 horizon;
        DirectX::SimpleMath::Vector3 below;      // 地平線の下（外周の崖の向こう）
        DirectX::SimpleMath::Vector3 sunDir;     // 光の進む向き（太陽から）
        DirectX::SimpleMath::Vector3 sunColor;
        float zenithCurve = 0.5f;                // 高さの指数（< 1 で地平線の帯が広い）
        float sunGlow = 0.35f;                   // 太陽の周りの光のにじみ
        float sunGlowPower = 24.0f;              // にじみの締まり（大きいほど小さい）
        float sunDisk = 4.0f;                    // 太陽の円盤（0 = 無し）
    };

    bool Initialize(ID3D11Device* device);
    void Render(ID3D11DeviceContext* context, CameraBase* camera, const Params& p) const;

private:
    // SkyPS の b0（row_major なので Transpose しない）
    struct SkyCB
    {
        DirectX::SimpleMath::Matrix invViewProj;
        DirectX::SimpleMath::Vector3 cameraPos; float zenithCurve;
        DirectX::SimpleMath::Vector3 zenith;    float sunGlow;
        DirectX::SimpleMath::Vector3 horizon;   float sunGlowPower;
        DirectX::SimpleMath::Vector3 below;     float sunDisk;
        DirectX::SimpleMath::Vector3 sunDir;    float pad0;
        DirectX::SimpleMath::Vector3 sunColor;  float pad1;
    };
    static_assert(sizeof(SkyCB) == 160, "SkyCB layout mismatch");

    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
};
