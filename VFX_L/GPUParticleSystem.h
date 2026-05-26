#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include "GPUParticle.h"
#include "ParticleDeadList.h"
#include "ConstantBuffer.h"
#include "Shader.h"
#include "CameraBase.h"
#include "Texture.h"

using Microsoft::WRL::ComPtr;

class GPUParticleSystem
{
public:
    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles);

    // 毎フレーム外部からEmitterデータを受け取って処理
    void Update(float deltaTime, float totalTime,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);
    void Render();

    void ResetSystem();

    // カメラ設定
    void SetCamera(CameraBase* camera) { m_Camera = camera; }

    // テクスチャ設定
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    // デバッグ情報
    uint32_t GetAliveCount() const { return m_MaxParticles - m_CurrentDeadCount; }
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }
    uint32_t GetMaxParticles() const { return m_MaxParticles; }

private:
    bool CreateParticleBuffer(ID3D11Device* device);
    bool CreateEmitterBuffer(ID3D11Device* device);
    bool LoadComputeShaders(ID3D11Device* device);
    bool CreateConstantBuffers(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);
    bool CreateColorKeyBuffer(ID3D11Device* device);

    void UploadEmitters(ID3D11DeviceContext* context,
        const std::vector<GPUEmitter>& emitters);
    void UploadColorKeys(ID3D11DeviceContext* context,
        const std::vector<ColorKey>& colorKeys);
    void DispatchEmit(ID3D11DeviceContext* context,
        const std::vector<GPUEmitter>& emitters);
    void DispatchUpdate(ID3D11DeviceContext* context);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    // 粒子プール
    uint32_t m_MaxParticles = 0;
    ComPtr<ID3D11Buffer>              m_ParticleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_ParticleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_ParticleSRV;

    // 発射器Buffer（データは外部から毎フレーム受け取る）
    static const int MAX_EMITTERS = 128;
    ComPtr<ID3D11Buffer>              m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_EmitterSRV;

    // Dead List
    ParticleDeadList m_DeadList;
    uint32_t m_CurrentDeadCount = 0;

    // Compute Shaders
    Shader m_InitDeadListCS;
    Shader m_EmitCS;
    Shader m_UpdateCS;

    // 定数バッファ
    ConstantBuffer<GlobalCB>         m_GlobalCB;
    ConstantBuffer<DeadListCB>       m_DeadListCB;
    ConstantBuffer<ParticleRenderCB> m_RenderCB;

    // レンダリング
    Shader m_RenderShader;
    ComPtr<ID3D11BlendState>        m_BlendState;
    ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    ComPtr<ID3D11RasterizerState>   m_RasterizerState;

    // ColorKeyBuffer
    ComPtr<ID3D11Buffer> m_ColorKeyBuffer;
    ComPtr<ID3D11ShaderResourceView> m_ColorKeySRV;
    static const int MAX_COLOR_KEYS_TOTAL = 1024;

    // 外部参照
    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;
    ComPtr<ID3D11SamplerState> m_SamplerState;
};