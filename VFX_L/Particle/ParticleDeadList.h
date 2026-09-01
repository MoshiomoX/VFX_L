#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

using Microsoft::WRL::ComPtr;

// ============================================
// Dead List管理
// AppendStructuredBuffer<uint>でGPU上の空き粒子インデックスを管理
//
// 流れ:
//   InitCS    → 全インデックスをDead Listに積む
//   EmitCS    → Consume()で空きを取得、新粒子を書き込む
//   UpdateCS  → 寿命切れの粒子をAppend()で返却
// ============================================
class ParticleDeadList
{
public:
    ParticleDeadList() = default;
    ~ParticleDeadList() = default;

    bool Initialize(ID3D11Device* device, uint32_t maxParticles);

    // Dead Listの現在のカウントをCPUに読み戻す
    uint32_t ReadDeadCount(ID3D11DeviceContext* context);

    // アクセサ
    ID3D11UnorderedAccessView* GetUAV() const { return m_UAV.Get(); }
    ID3D11ShaderResourceView* GetSRV() const { return m_SRV.Get(); }
    uint32_t GetMaxParticles() const { return m_MaxParticles; }

    // UAVをリセット付きでバインドする時に使う
    // initialCountはAppend/Consumeのカウンタ初期値
    void BindCSUAV(ID3D11DeviceContext* context, uint32_t slot, uint32_t initialCount);
    void UnbindCSUAV(ID3D11DeviceContext* context, uint32_t slot);

private:
    uint32_t m_MaxParticles = 0;

    // Dead List本体 (AppendStructuredBuffer<uint>)
    ComPtr<ID3D11Buffer>              m_Buffer;
    ComPtr<ID3D11UnorderedAccessView> m_UAV;
    ComPtr<ID3D11ShaderResourceView>  m_SRV;

    // カウント読み戻し用ステージングバッファ
    ComPtr<ID3D11Buffer>              m_CountStaging;
};