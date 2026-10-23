// ============================================================
// LightClusters.h
// クラスタ単位の点光源の振り分け（Forward+、2026-10-23）。
//
//   カメラの視錐台を 画面 16×9 マス × 奥行き 24 段（奥ほど厚い指数分割）に切り、
//   毎フレーム 1 回、光の表がそろった後（CPU 分 + GPU の弾・範囲の追記）に
//   LightClusterCS がクラスタごとに「球が届いている光」のビットを立てる。
//   PS（Lighting.hlsli の PointLightShade）は自分のクラスタのビットだけを回す。
//   以前は 1 ピクセルごとに表の全部の光を回していた（2026-10-23 の計測：
//   画面に 1 つも届かない光 64 個だけで Release 1600×900 で +0.62 ms）。
//
//   作っていないフレーム（カメラの無い場面、切った時）は PS 側の t10 / t11 が null になり、
//   PS は前のとおり全部の光を回す（PointLightManager::BindPS が決める）。
//
//   HLSL 側：Shader/Common/LightClusters.hlsli（LightClusterInfo は同じ並び）
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <cstdint>

class CameraBase;
class ComputeShader;

// Shader/Common/LightClusters.hlsli の LightClusterInfo と同じ並び（160 bytes）
struct LightClusterInfo
{
    DirectX::SimpleMath::Vector4 viewProj[4];   // view * proj の行（行ベクトルの規約）
    DirectX::SimpleMath::Vector4 view[4];       // view の行（右手系：カメラの前 = -z）
    DirectX::SimpleMath::Vector2 projScale;     // proj._11, proj._22
    float    zNear = 1.0f;
    float    zFar = 250.0f;
    float    sliceScale = 0.0f;                 // kZ / ln(zFar / zNear)
    float    sliceBias = 0.0f;                  // -ln(zNear) * sliceScale
    uint32_t enabled = 0;
    uint32_t debugView = 0;
};
static_assert(sizeof(LightClusterInfo) == 160, "LightClusterInfo layout mismatch");

class LightClusters
{
public:
    static constexpr uint32_t kX = 16, kY = 9, kZ = 24;   // = LIGHT_CLUSTER_X / Y / Z
    static constexpr uint32_t kCount = kX * kY * kZ;
    static constexpr UINT kMaskSlot = 10;                 // PS t10
    static constexpr UINT kInfoSlot = 11;                 // PS t11

    struct Settings
    {
        bool  enabled = true;      // 切ると PS は全部の光を回す（前のやり方。見比べ用）
        bool  debugView = false;   // 1 ピクセルが回った光の数を色で（青 1 〜 緑 4 〜 赤 16+）
        float zNear = 1.0f;        // 奥行きの最初の段の境（m）。手前はカメラから
        float zFar = 250.0f;       // 最後の段の境（m）。その先は 1 段で無限まで
    };

    // maxLights = 光の表の上限（32 の倍数。ビットの語数を決める）
    bool Initialize(ID3D11Device* device, uint32_t maxLights);
    void Shutdown();

    void BeginFrame() { m_Built = false; }
    // 光の表（lights / count）がそろった後、そのフレームの描画に使うカメラで 1 回
    void Build(ID3D11DeviceContext* ctx, CameraBase& camera,
        ID3D11ShaderResourceView* lights, ID3D11ShaderResourceView* count);
    bool IsBuilt() const { return m_Built; }

    // t10 / t11。今フレームに作っていなければ null（PS は全部の光を回す）
    void BindPS(ID3D11DeviceContext* ctx);
    void UnbindPS(ID3D11DeviceContext* ctx);

    Settings& GetSettings() { return m_Settings; }
    void DrawImGui();

private:
    Settings m_Settings;
    bool m_Built = false;
    uint32_t m_Words = 0;   // 1 クラスタのビットの語数 = maxLights / 32

    std::shared_ptr<ComputeShader> m_CS;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_MaskBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_MaskSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_MaskUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_InfoBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_InfoSRV;
};
