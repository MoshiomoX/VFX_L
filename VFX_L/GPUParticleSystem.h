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
#include "RenderStates.h"
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

    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }

    size_t GetPendingEmitterCount() const { return m_PendingEmitters.size(); }
    size_t GetMaxEmitters()         const { return MAX_EMITTERS; }
    size_t GetDroppedEmitterCount() const { return m_DroppedEmitters; }
    uint32_t GetMaxParticles()      const { return m_MaxParticles; }

    // ============================================
    // emitter を積む余地があるか
    //
    // 呼び出し側が重い収集処理（CollectAndDispatch）に入る前に
    // これを確認することで、積んでも捨てられるだけの計算を省ける。
    //
    // 計測根拠：投射物 4000 のとき Dropped が 2976 に達していた。
    // つまり VFX 収集の 3/4 は完全な無駄だった。
    // FPS は VFX ON で 27.6 / OFF で 84.2（投射物数は同じ 4000）。
    // ============================================
    bool HasEmitterSpace() const { return m_PendingEmitters.size() < MAX_EMITTERS; }

    // ============================================
    // 暫定実装（第5段階の ownerID 方式で置き換える）
    //
    // 毎フレームの ReadDeadCount（Map READ = GPU 待ち）を廃止したため、
    // 正確な生存数は GPU 上にしか無い。CPU は知らない。
    //
    // 0 を返すと VFXState_Finishing が即 Stopped へ飛んでしまい、
    // 粒子が空中に残ったまま演出が終わる。
    // よって「まだ居るかもしれない」= 1 を返し、Finishing の終了判定は
    // VFXStates 側の時間兜底（timeInState > 3.0f）に任せる。
    // ★この 2 つは必ずセットで扱う。片方だけ変えると状態機が壊れる。
    // ============================================
    uint32_t GetAliveCount() const { return 1; }

    // 初期化/リセット直後の値のまま。ImGui の目安表示にのみ使う。
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }

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
    bool CreateDeadCountBuffer(ID3D11Device* device);

    // 引数名は requestedEmit。
    // 「撃ちたい数」であって「撃てる数」ではない。
    // 空き数に合わせた clamp は shader 側が deadCount で行う。
    void DispatchEmit(ID3D11DeviceContext* context, uint32_t requestedEmit);
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

    // 空き数の GPU 内受け渡し（CopyStructureCount の受け皿。CPU は Map しない）
    ComPtr<ID3D11Buffer>              m_DeadCountBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_DeadCountSRV;

    static const int MAX_EMITTERS = 1024;
    ComPtr<ID3D11Buffer>               m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>   m_EmitterSRV;

    ParticleDeadList m_DeadList;

    // 初期化/リセット時にだけ更新される。毎フレームの回読は廃止した。
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

    // ComPtr<ID3D11BlendState>        m_BlendState;
    // ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    // ComPtr<ID3D11RasterizerState>   m_RasterizerState;

    ComPtr<ID3D11Buffer> m_ColorKeyBuffer;
    ComPtr<ID3D11ShaderResourceView> m_ColorKeySRV;
    static const int MAX_COLOR_KEYS_TOTAL = 1024;

    // DrawIndirect 用
    ComPtr<ID3D11Buffer>              m_DrawIndirectBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_DrawIndirectUAV;

    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;
    //   ComPtr<ID3D11SamplerState> m_SamplerState;
};