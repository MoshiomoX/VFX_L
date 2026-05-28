#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include "GPUParticleEmitter.h"
#include "ParticleDeadList.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "ComputeShader.h"
#include "CameraBase.h"
#include "Texture.h"

using Microsoft::WRL::ComPtr;

class GPUParticleSystem
{
public:
    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles);

    int  AddEmitter(float emitRate, int maxParticlesPerEmitter);
    void RemoveEmitter(int emitterID);
    GPUParticleEmitter* GetEmitter(int emitterID);

    void ResetAllocator() { m_ParticleAllocOffset = 0; }

    // GPUParticleSystem.h の public に追加
    void Update(float deltaTime, float totalTime,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);
    void Render();
    void ResetSystem();

    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    uint32_t GetAliveCount() const { return m_MaxParticles - m_CurrentDeadCount; }
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }
    uint32_t GetMaxParticles() const { return m_MaxParticles; }
    int      GetEmitterCount() const { return static_cast<int>(m_Emitters.size()); }

private:
    bool CreateParticleBuffer(ID3D11Device* device);
    bool CreateEmitterBuffer(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);
    bool CreateColorKeyBuffer(ID3D11Device* device);

    void UploadEmitters(ID3D11DeviceContext* context);
    void DispatchEmit(ID3D11DeviceContext* context, uint32_t totalEmit);
    void DispatchUpdate(ID3D11DeviceContext* context);

    void UploadExternalEmitters(ID3D11DeviceContext* context,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);

    int AllocateParticleRange(int count);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    uint32_t m_MaxParticles = 0;
    ComPtr<ID3D11Buffer>              m_ParticleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_ParticleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_ParticleSRV;

    static const int MAX_EMITTERS = 128;
    std::vector<std::unique_ptr<GPUParticleEmitter>> m_Emitters;
    ComPtr<ID3D11Buffer>              m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_EmitterSRV;
    int m_NextEmitterID = 0;
    int m_ParticleAllocOffset = 0;

    ParticleDeadList m_DeadList;
    
    uint32_t m_CurrentDeadCount = 0;

    // ===== 変更点: シェーダーを個別に管理 =====
    std::shared_ptr<ComputeShader> m_InitDeadListCS;
    std::shared_ptr<ComputeShader> m_EmitCS;
    std::shared_ptr<ComputeShader> m_UpdateCS;
    std::shared_ptr<VertexShader>  m_RenderVS;
    std::shared_ptr<PixelShader>   m_RenderPS;

    // ===== 変更点: GlobalCBをキャッシュ =====
    GlobalCB m_CachedGlobalCB = {};

    ComPtr<ID3D11BlendState>        m_BlendState;
    ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    ComPtr<ID3D11RasterizerState>   m_RasterizerState;

    ComPtr<ID3D11Buffer> m_ColorKeyBuffer;
    ComPtr<ID3D11ShaderResourceView> m_ColorKeySRV;
    static const int MAX_COLOR_KEYS_TOTAL = 1024;

    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;
    ComPtr<ID3D11SamplerState> m_SamplerState;
};