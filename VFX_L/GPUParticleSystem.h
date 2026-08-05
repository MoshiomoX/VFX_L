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
 
    void Render();
    void ResetSystem();

    // --- 2段階方式 ---
  // 各 VFXEffect が自分の emitter を積む（1フレームに何度でも呼べる）
    void SubmitEmitters(const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);

    // 1フレームに1度だけ呼ぶ。積まれた全 emitter で Emit + Update を実行
    void Flush(float dt, float totalTime);

    // 現在積まれている emitter 数（デバッグ表示用）

    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    size_t GetPendingEmitterCount() const { return m_PendingEmitters.size(); }
    size_t GetMaxEmitters()         const { return MAX_EMITTERS; }
    size_t GetDroppedEmitterCount() const { return m_DroppedEmitters; }
    uint32_t GetAliveCount() const { return m_MaxParticles - m_CurrentDeadCount; }
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }
    uint32_t GetMaxParticles() const { return m_MaxParticles; }
private:
    void Update(float deltaTime, float totalTime,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);
    bool CreateParticleBuffer(ID3D11Device* device);
    bool CreateEmitterBuffer(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);
    bool CreateColorKeyBuffer(ID3D11Device* device);
    bool CreateDrawIndirectBuffer(ID3D11Device* device); 
    bool CreateAliveListBuffer(ID3D11Device* device, uint32_t maxParticles);

    void DispatchEmit(ID3D11DeviceContext* context, uint32_t totalEmit);
    void DispatchUpdate(ID3D11DeviceContext* context);

    void UploadExternalEmitters(ID3D11DeviceContext* context,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    uint32_t m_MaxParticles = 0;
    ComPtr<ID3D11Buffer>              m_ParticleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_ParticleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_ParticleSRV;

    // AliveList（DrawIndirect 用、存活粒子の index を格納）
    ComPtr<ID3D11Buffer>              m_AliveListBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_AliveListUAV;
    ComPtr<ID3D11ShaderResourceView>  m_AliveListSRV;

    static const int MAX_EMITTERS = 1024;
    ComPtr<ID3D11Buffer>               m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>   m_EmitterSRV;

    ParticleDeadList m_DeadList;
    uint32_t m_CurrentDeadCount = 0;

    std::shared_ptr<ComputeShader>   m_InitDeadListCS;
    std::shared_ptr<ComputeShader>   m_EmitCS;
    std::shared_ptr<ComputeShader>   m_UpdateCS;
    std::shared_ptr<VertexShader>    m_RenderVS;
    std::shared_ptr<PixelShader>     m_RenderPS;
    // フレーム内の積み上げ用（GPU バッファではなく CPU 側の一時領域）
    std::vector<GPUEmitter> m_PendingEmitters;
    std::vector<ColorKey>   m_PendingColorKeys;
    size_t                  m_DroppedEmitters = 0;   // 上限超過で捨てた数
    GlobalCB m_CachedGlobalCB = {};

    ComPtr<ID3D11BlendState>        m_BlendState;
    ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    ComPtr<ID3D11RasterizerState>   m_RasterizerState;

    ComPtr<ID3D11Buffer> m_ColorKeyBuffer;
    ComPtr<ID3D11ShaderResourceView> m_ColorKeySRV;
    static const int MAX_COLOR_KEYS_TOTAL = 1024;

    // DrawIndirect 用
    ComPtr<ID3D11Buffer>              m_DrawIndirectBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_DrawIndirectUAV;

    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;
    ComPtr<ID3D11SamplerState> m_SamplerState;
};