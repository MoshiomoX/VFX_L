// ============================================================
// PointLightManager.h
// フレーム単位の点光源リスト。全モデル PS が t6/t7 から読む
// （Shader/Common/PointLights.hlsli）。
//
//   CPU 側（VFX の Light entry、編集器のプレビュー、CPU 施放の範囲攻撃）は
//   Add() で積む。GPU 側（弾・範囲）は SwarmSystem が同じバッファに
//   CS で追記する（SwarmLightCollectCS）。PS はどちらも区別しない。
//
//   1 フレームの流れ:
//     BeginFrame()  … CPU リストを空にする（Application が Update の前に）
//     Add()         … 各所から
//     Upload()      … CPU リストを GPU バッファへ、数を [0] に書く。
//                     同フレーム 2 回目以降は何もしない（SwarmSystem が
//                     追記の前に呼び、SceneBase::Render が保険で呼ぶ）
//     BindPS()      … 描画直前に t6/t7 を張る（Renderer / SkinnedModelGPU / Swarm）
//
//   上限は kMaxLights。超えた分は捨てる（先着順）
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <vector>
#include <cstdint>

// Shader/Common/PointLights.hlsli の PointLight と一致（32 bytes）
struct PointLight
{
    DirectX::SimpleMath::Vector3 position;
    float radius = 3.0f;
    DirectX::SimpleMath::Vector3 color = { 1, 1, 1 };
    float intensity = 1.0f;
};
static_assert(sizeof(PointLight) == 32, "PointLight layout mismatch");

class PointLightManager
{
public:
    static constexpr uint32_t kMaxLights = 64;   // = MAX_POINT_LIGHTS
    static constexpr UINT kLightSlot = 6;        // PS t6
    static constexpr UINT kCountSlot = 7;        // PS t7

    static PointLightManager& Get()
    {
        static PointLightManager instance;
        return instance;
    }

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    void BeginFrame();
    void Add(const PointLight& light);
    void Add(const DirectX::SimpleMath::Vector3& pos, const DirectX::SimpleMath::Vector3& color,
        float radius, float intensity)
    {
        Add(PointLight{ pos, radius, color, intensity });
    }

    // CPU リストを GPU へ（同フレーム 1 回だけ実行される）
    void Upload(ID3D11DeviceContext* ctx);

    void BindPS(ID3D11DeviceContext* ctx);
    void UnbindPS(ID3D11DeviceContext* ctx);

    // GPU 側の追記用（SwarmSystem）
    ID3D11UnorderedAccessView* GetLightUAV() const { return m_LightUAV.Get(); }
    ID3D11UnorderedAccessView* GetCountUAV() const { return m_CountUAV.Get(); }

    int GetCpuCount() const { return (int)m_Cpu.size(); }
    bool IsUploaded() const { return m_Uploaded; }

private:
    PointLightManager() = default;

    std::vector<PointLight> m_Cpu;
    bool m_Uploaded = false;

    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_LightBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_LightSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_LightUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_CountBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_CountSRV;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CountUAV;
};
