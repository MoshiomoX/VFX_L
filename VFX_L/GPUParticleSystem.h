#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include "GPUParticleEmitter.h"
#include "ParticleDeadList.h"
#include "ConstantBuffer.h"
#include "Shader.h"
#include "CameraBase.h"
#include "Texture.h"

using Microsoft::WRL::ComPtr;

// ============================================
// GPUParticleSystem
// 粒子プール・発射器プール・Dead Listを統括
// Emit → Update → Render の流れを管理
// ============================================
class GPUParticleSystem
{
public:
    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles);

    // 発射器の追加・削除
    int  AddEmitter(float emitRate, int maxParticlesPerEmitter);
    void RemoveEmitter(int emitterID);
    GPUParticleEmitter* GetEmitter(int emitterID);

    // 毎フレーム呼ぶ
    void Update(float deltaTime, float totalTime);
    void Render();

    // カメラ設定
    void SetCamera(CameraBase* camera) { m_Camera = camera; }

    // テクスチャ設定
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    // デバッグ情報
    uint32_t GetAliveCount() const { return m_MaxParticles - m_CurrentDeadCount; }
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }
    uint32_t GetMaxParticles() const { return m_MaxParticles; }
    int      GetEmitterCount() const { return static_cast<int>(m_Emitters.size()); }

private:
    // 初期化ヘルパー
    bool CreateParticleBuffer(ID3D11Device* device);
    bool CreateEmitterBuffer(ID3D11Device* device);
    bool LoadComputeShaders(ID3D11Device* device);
    bool CreateConstantBuffers(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);

    // 毎フレームの処理
    void UploadEmitters(ID3D11DeviceContext* context);
    void DispatchEmit(ID3D11DeviceContext* context);
    void DispatchUpdate(ID3D11DeviceContext* context);

    // particleOffset割り当て
    int AllocateParticleRange(int count);

    // デバイス参照
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    // 粒子プール
    uint32_t m_MaxParticles = 0;
    ComPtr<ID3D11Buffer>              m_ParticleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_ParticleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_ParticleSRV;

    // 発射器プール
    static const int MAX_EMITTERS = 128;
    std::vector<std::unique_ptr<GPUParticleEmitter>> m_Emitters;
    ComPtr<ID3D11Buffer>              m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_EmitterSRV;
    int m_NextEmitterID = 0;
    int m_ParticleAllocOffset = 0;  // 次に割り当てる開始位置

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
    Shader m_RenderShader;  // VS + PS
    ComPtr<ID3D11BlendState>        m_BlendState;        // 加算合成
    ComPtr<ID3D11DepthStencilState> m_DepthStencilState; // 深度書込OFF
    ComPtr<ID3D11RasterizerState>   m_RasterizerState;   // カリングOFF

    // 外部参照
    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;

    ComPtr<ID3D11SamplerState> m_SamplerState;
};