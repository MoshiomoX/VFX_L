// ============================================================
// ShadowMap.h
// 太陽（平行光）の影。3 段のカスケード（cascaded shadow map）。
//
//   段の分け方: カメラからの距離 0..splits[0] / ..splits[1] / ..splits[2] m を各段が受け持つ。
//     各段 = カメラの視錐台をその距離で切った塊を包む球。球の大きさは画角と距離だけで決まる
//     （カメラが回っても変わらない）ので、シャドウマップの 1 texel の大きさが揺れない。
//     さらに球の中心を光源から見て 1 texel 単位に揃える（カメラが動いても影の縁がちらつかない）
//   シャドウマップ: 1 段 size x size の D32F を Texture2DArray の 1 層に。各段を光源の正射影で描く。
//     影を落とす物は球より光源側に backDistance m まで入れる（高台の崖・木の影が切れない）
//   受け取り側: LightBuffer の shadow*（行列・距離・調整値）+ PS t9 / s2。
//     Lighting.hlsli の SunShadow が太陽の拡散・ハイライトに掛ける（環境光と点光源は残る）
//
// 使い方（シーンの Render の頭）:
//   shadow.Render(ctx, renderer, camera, sunDir, [&](const Matrix& v, const Matrix& p, int c) { 影を落とす物を描く });
//   graphics.RestoreRenderTarget();   // シーンの HDR RT に戻す
//   … 通常の描画（t9 は Render が積んだまま）…
//   shadow.Unbind(ctx);               // 次の Render で DSV にするので外す
// シーンの終わりに Disable(renderer)（Renderer は他のシーンと共有）。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <functional>

class Renderer;
class CameraBase;

class ShadowMap
{
public:
    static constexpr int kCascades = 3;

    struct Settings
    {
        bool  enabled = true;
        int   size = 2048;                                  // 1 段の辺（px）
        float splits[kCascades] = { 12.0f, 40.0f, 120.0f };  // 各段の受け持ち（カメラからの距離 m）
        float backDistance = 60.0f;   // 段の球より光源側に何 m までの物をシャドウマップに入れるか
        float strength = 0.8f;        // 0..1。1 = 影の中は太陽の光が全く届かない（環境光だけ）
        float normalOffset = 1.5f;    // 受け取る点を法線方向へずらす量（シャドウマップの texel 数）
        float compareBias = 0.0005f;  // 比較の深度バイアス（深度 0..1）
        int   rasterBias = 0;         // 描く側の深度バイアス（D3D11 DepthBias）
        float slopeBias = 2.0f;       // 描く側の傾き比例バイアス（SlopeScaledDepthBias）
        int   pcfRadius = 1;          // 0 = 1 点、1 = 3x3、2 = 5x5（各点が 2x2 の比較補間）
        float fadeStart = 0.85f;      // 最後の段の何割の距離から薄くし始めるか
        bool  showCascades = false;   // 段ごとに赤・緑・青を掛ける（調整用）
    };

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    using DrawCasters = std::function<void(const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj, int cascade)>;

    // 各段を描き、受け取り側の値を renderer へ、シャドウマップを PS t9 / s2 へ積む。
    // 切の時は renderer の影を切るだけ。終わった後の RT・ビューポートは呼んだ側が戻す
    void Render(ID3D11DeviceContext* ctx, Renderer& renderer, CameraBase& camera,
        const DirectX::SimpleMath::Vector3& sunDir, const DrawCasters& drawCasters);

    void Unbind(ID3D11DeviceContext* ctx) const;
    void Disable(Renderer& renderer) const;

    void DrawImGui();

    Settings& GetSettings() { return m_Settings; }
    float LastPassMs() const { return m_LastPassMs; }

private:
    bool CreateMaps(int size);
    bool CreateRasterState();

    ID3D11Device* m_Device = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>          m_Texture;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView>   m_DSV[kCascades];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
    Microsoft::WRL::ComPtr<ID3D11SamplerState>       m_CompareSampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>    m_Raster;
    int   m_MapSize = 0;
    int   m_RasterBias = 0x7FFFFFFF;   // 今の m_Raster の値（変わったら作り直す）
    float m_RasterSlope = -1.0f;

    Settings m_Settings;
    float m_LastPassMs = 0.0f;   // シャドウマップを描いた CPU 時間（調整用）
};
