#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/ShaderPath.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

// ============================================
// 初期化
// ============================================
bool GPUParticleSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles)
{
    m_Device = device;
    m_Context = context;
    m_MaxParticles = maxParticles;

    if (!CreateParticleBuffer(device))       return false;
    if (!CreateEmitterBuffer(device))        return false;
    if (!m_DeadList.Initialize(device, maxParticles)) return false;
    if (!LoadShaders(device))                return false;
    // if (!CreateRenderStates(device))         return false;
    if (!CreateColorKeyBuffer(device))       return false;
    if (!CreateDrawIndirectBuffer(device))   return false;
    if (!CreateAliveListBuffer(device, maxParticles)) return false;

    // DispatchEmit が使うので、必ず初回発射より前に作る。
    //   ここを忘れると deadCount が null → 全スレッドが return して
    //   一発も発射されない（しかもエラーは出ない）。
    if (!CreateDeadCountBuffer(device))      return false;

    // DeadList を全インデックスで初期化
    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = maxParticles;

    m_InitDeadListCS->WriteBuffer(context, 0, &dlcb);
    m_InitDeadListCS->Bind(context);

    // ※ここの initialCount = 0 は正当。
    //   「計数器を 0 にしてから maxParticles 個 Append する」一度きりの初期化。
    //   毎フレーム CPU の値で上書きするのとは意味が違う。
    m_DeadList.BindCSUAV(context, 0, 0);

    context->Dispatch((maxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(context, 0);
    m_InitDeadListCS->Unbind(context);

    // 初期化直後の一度だけ読む（起動時なので待っても問題ない）
    m_CurrentDeadCount = m_DeadList.ReadDeadCount(context);

    return true;
}

// ============================================
// バッファ作成
// ============================================
bool GPUParticleSystem::CreateParticleBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(GPUParticle) * m_MaxParticles;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(GPUParticle);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_ParticleBuffer);
    if (FAILED(hr)) return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = m_MaxParticles;

    hr = device->CreateUnorderedAccessView(m_ParticleBuffer.Get(), &uavDesc, &m_ParticleUAV);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = m_MaxParticles;

    hr = device->CreateShaderResourceView(m_ParticleBuffer.Get(), &srvDesc, &m_ParticleSRV);
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::CreateColorKeyBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(ColorKey) * MAX_COLOR_KEYS_TOTAL;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(ColorKey);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_ColorKeyBuffer);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_COLOR_KEYS_TOTAL;

    hr = device->CreateShaderResourceView(m_ColorKeyBuffer.Get(), &srvDesc, &m_ColorKeySRV);
    if (FAILED(hr)) return false;

    return true;
}

bool GPUParticleSystem::CreateEmitterBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(GPUEmitter) * MAX_EMITTERS;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(GPUEmitter);

    HRESULT hr = device->CreateBuffer(&desc, nullptr, &m_EmitterBuffer);
    if (FAILED(hr)) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_EMITTERS;

    hr = device->CreateShaderResourceView(m_EmitterBuffer.Get(), &srvDesc, &m_EmitterSRV);
    if (FAILED(hr)) return false;

    return true;
}

// ============================================
// DrawIndirect バッファ作成
// ============================================
bool GPUParticleSystem::CreateDrawIndirectBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(uint32_t) * 4;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

    // InstanceCount 累加方式：VertexCount=6(固定), InstanceCount=0(CSが累加)
    uint32_t initArgs[4] = { 6, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = initArgs;

    HRESULT hr = device->CreateBuffer(&desc, &initData, &m_DrawIndirectBuffer);
    if (FAILED(hr))
    {
        std::cout << "[Error] DrawIndirectBuffer creation failed" << std::endl;
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_R32_UINT;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 4;
    uavDesc.Buffer.Flags = 0;

    hr = device->CreateUnorderedAccessView(m_DrawIndirectBuffer.Get(), &uavDesc, &m_DrawIndirectUAV);
    if (FAILED(hr))
    {
        std::cout << "[Error] DrawIndirectUAV creation failed" << std::endl;
        return false;
    }

    std::cout << "[OK] DrawIndirectBuffer created" << std::endl;
    return true;
}

// ============================================
// AliveList バッファ作成
// ============================================
bool GPUParticleSystem::CreateAliveListBuffer(ID3D11Device* device, uint32_t maxParticles)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(uint32_t) * maxParticles;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(uint32_t);

    if (FAILED(device->CreateBuffer(&desc, nullptr, &m_AliveListBuffer)))
        return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.NumElements = maxParticles;

    if (FAILED(device->CreateUnorderedAccessView(m_AliveListBuffer.Get(), &uavDesc, &m_AliveListUAV)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.NumElements = maxParticles;

    if (FAILED(device->CreateShaderResourceView(m_AliveListBuffer.Get(), &srvDesc, &m_AliveListSRV)))
        return false;

    std::cout << "[OK] AliveList Buffer created" << std::endl;
    return true;
}

// ============================================
// 空き数バッファ作成
// CopyStructureCount の受け皿。shader が SRV で読む。
// CPU は Map しないので GPU を待たせない。
// ============================================
bool GPUParticleSystem::CreateDeadCountBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC d = {};
    d.ByteWidth = sizeof(uint32_t);
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateBuffer(&d, nullptr, &m_DeadCountBuffer)))
    {
        std::cout << "[Error] DeadCountBuffer creation failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R32_UINT;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = 1;
    if (FAILED(device->CreateShaderResourceView(
        m_DeadCountBuffer.Get(), &sd, &m_DeadCountSRV)))
    {
        std::cout << "[Error] DeadCountSRV creation failed" << std::endl;
        return false;
    }

    std::cout << "[OK] DeadCountBuffer created" << std::endl;
    return true;
}

// ============================================
// シェーダー読み込み
// ============================================
bool GPUParticleSystem::LoadShaders(ID3D11Device* device)
{
    m_InitDeadListCS = std::make_shared<ComputeShader>();
    HRESULT hr = ShaderPath::Load(m_InitDeadListCS.get(), device,
        L"Shader/Particle/InitDeadListCS.hlsl");
    std::cout << "[LoadShaders] InitDeadListCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_EmitCS = std::make_shared<ComputeShader>();
    hr = ShaderPath::Load(m_EmitCS.get(), device, L"Shader/Particle/ParticleEmitCS.hlsl");
    std::cout << "[LoadShaders] EmitCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_UpdateCS = std::make_shared<ComputeShader>();
    hr = ShaderPath::Load(m_UpdateCS.get(), device, L"Shader/Particle/ParticleUpdateCS.hlsl");
    std::cout << "[LoadShaders] UpdateCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_RenderVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_RenderVS.get(), device, L"Shader/Particle/GPUParticleVS.hlsl");
    std::cout << "[LoadShaders] RenderVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_RenderPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_RenderPS.get(), device, L"Shader/Particle/GPUParticlePS.hlsl");
    std::cout << "[LoadShaders] RenderPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    return true;
}
// ============================================
// 毎フレーム更新
//
// 毎フレームの ReadDeadCount（CopyStructureCount + Map READ）は廃止した。
// Map READ は GPU の完了を待つため、パイプラインを毎フレーム断ち切っていた。
// 空き数は GPU 上の deadCount バッファ経由で shader が直接読む。
//
// ---- 計測結果（内訳診断より）----
//   Flush 合計   約 0.14 ms
//     Upload     0.018 ms (15%)  Map + memcpy（245KB。emitter 数に比例）
//     Emit       0.076 ms (63%)  ほぼ全部 CopyStructureCount の固定コスト
//     Update     0.025 ms (21%)  Clear + 満池 dispatch
//
//   Emit が重いのは CopyStructureCount が命令キューのフラッシュを
//   誘発するため。emitter 数と無関係な固定コスト（500 でも 1024 でも同じ）。
//   VFX OFF 時に 0.0002 ms へ落ちることで裏付け済み。
//
//   ただし 1 フレーム（12〜36ms）の 0.6% 程度であり、
//   これを削るには CPU 側で消費数を記帳する必要があって
//   「空き数の真実は GPU にしかない」という本改修の前提を壊す。
//   よって現時点では最適化しない。
// ============================================
void GPUParticleSystem::Update(float deltaTime, float totalTime,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    auto context = m_Context;

    m_CachedGlobalCB = {};
    m_CachedGlobalCB.deltaTime = deltaTime;
    m_CachedGlobalCB.totalTime = totalTime;
    m_CachedGlobalCB.baseSeed = static_cast<uint32_t>(totalTime * 1000.0f);
    m_CachedGlobalCB.emitterCount = static_cast<int>(emitters.size());

    // CPU が知っているのは「何発撃ちたいか」だけ。
    // 空き数に合わせた clamp はここでは行わない（shader 側が deadCount で止める）。
    // CPU 側で clamp しても Dispatch は 256 スレッド粒度なので必ず溢れ、
    // 意味を持たなかった。
    uint32_t requestedEmit = 0;
    for (auto& e : emitters)
    {
        if (e.isActive > 0.5f)
            requestedEmit += e.emitCount;
    }

    UploadExternalEmitters(context, emitters, colorKeys);
    DispatchEmit(context, requestedEmit);
    DispatchUpdate(context);
}

void GPUParticleSystem::UploadExternalEmitters(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    if (emitters.empty()) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return;

    size_t count = (std::min)(emitters.size(), static_cast<size_t>(MAX_EMITTERS));
    memcpy(mapped.pData, emitters.data(), sizeof(GPUEmitter) * count);
    context->Unmap(m_EmitterBuffer.Get(), 0);

    if (!colorKeys.empty())
    {
        size_t keyCount = (std::min)(colorKeys.size(), static_cast<size_t>(MAX_COLOR_KEYS_TOTAL));
        D3D11_MAPPED_SUBRESOURCE colorMapped = {};
        hr = context->Map(m_ColorKeyBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &colorMapped);
        if (SUCCEEDED(hr))
        {
            memcpy(colorMapped.pData, colorKeys.data(), sizeof(ColorKey) * keyCount);
            context->Unmap(m_ColorKeyBuffer.Get(), 0);
        }
    }
}

// ============================================
// Emit ディスパッチ
// 発射数の上限は GPU が決める。CPU は「何発撃ちたいか」だけ渡す。
// ============================================
void GPUParticleSystem::DispatchEmit(ID3D11DeviceContext* context, uint32_t requestedEmit)
{
    if (requestedEmit == 0) return;

    //deadCount が未作成だと全スレッドが return して無音で発射されなくなる。
    //   一度だけ警告して原因を分かるようにする。
    if (!m_DeadCountBuffer || !m_DeadCountSRV)
    {
        static bool warned = false;
        if (!warned)
        {
            std::cout << "[Error] DeadCountBuffer is null "
                "(CreateDeadCountBuffer が呼ばれていない)" << std::endl;
            warned = true;
        }
        return;
    }

    // 空き数を GPU 上のバッファへ写す（Copy のみ、Map しない = 待たない）
    context->CopyStructureCount(m_DeadCountBuffer.Get(), 0, m_DeadList.GetUAV());

    m_EmitCS->WriteBuffer(context, 0, &m_CachedGlobalCB);
    m_EmitCS->Bind(context);

    // SRV は即時バインド
    m_EmitCS->SetSRV(context, "emitters", m_EmitterSRV.Get());
    m_EmitCS->SetSRV(context, "deadCount", m_DeadCountSRV.Get());

    // UAV はキューに溜めて一括バインド
    m_EmitCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    //initialCount = -1（保持）。CPU の値を渡すと計数器を毎フレーム上書きし、
    //   下溢バグを隠す膏薬になる。計数器は GPU が自分で維持する。
    m_EmitCS->SetUAV(context, "deadList", m_DeadList.GetUAV(), (UINT)-1);
    m_EmitCS->BindUAVs(context);

    // 過大でも構わない。shader が deadCount で自分を止める。
    context->Dispatch((requestedEmit + 255) / 256, 1, 1);

    m_EmitCS->UnbindSRVs(context);
    m_EmitCS->UnbindUAVs(context);
}

// ============================================
// Update ディスパッチ（DrawIndirect + AliveList + 一括バインド）
// ============================================
void GPUParticleSystem::DispatchUpdate(ID3D11DeviceContext* context)
{
    // ※deadCount は UpdateCS 側で未使用（g_MaxParticles しか読んでいない）。
    //   maxParticles は削除不可：id.x の上限判定に使われている。
    //     ここを消すと粒子が一切更新されなくなる。
    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = m_MaxParticles;

    m_UpdateCS->WriteBuffer(context, 0, &m_CachedGlobalCB);
    m_UpdateCS->WriteBuffer(context, 1, &dlcb);

    // DrawIndirectArgs リセット（InstanceCount 累加方式）
    UINT clearValues[4] = { 6, 0, 0, 0 };
    context->ClearUnorderedAccessViewUint(m_DrawIndirectUAV.Get(), clearValues);

    m_UpdateCS->Bind(context);

    // SRV は即時バインド
    m_UpdateCS->SetSRV(context, "colorKeys", m_ColorKeySRV.Get());

    // UAV はキューに溜めて一括バインド
    m_UpdateCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    m_UpdateCS->SetUAV(context, "deadList", m_DeadList.GetUAV());
    m_UpdateCS->SetUAV(context, "g_DrawArgs", m_DrawIndirectUAV.Get());
    m_UpdateCS->SetUAV(context, "aliveList", m_AliveListUAV.Get());
    m_UpdateCS->BindUAVs(context);

    context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_UpdateCS->UnbindSRVs(context);
    m_UpdateCS->UnbindUAVs(context);
}

// ============================================
// レンダリング（DrawInstancedIndirect + AliveList）
// ============================================
void GPUParticleSystem::Render()
{
    if (!m_Camera) return;
    if (!m_RenderVS || !m_RenderVS->IsValid()) return;
    if (!m_RenderPS || !m_RenderPS->IsValid()) return;

    auto context = m_Context;

    // --- レンダーCB（view/proj は転置して送る）---
    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();

    m_RenderVS->WriteBuffer(context, 0, &rcb);
    m_RenderVS->SetSRV(context, "particles", m_ParticleSRV.Get());
    m_RenderVS->SetSRV(context, "aliveList", m_AliveListSRV.Get());

    if (m_Texture)
        m_RenderPS->SetTexture(context, 0, m_Texture.get());

    ID3D11SamplerState* samp = RenderStates::Get().LinearClamp();
    context->PSSetSamplers(0, 1, &samp);
    RenderStates::Get().ApplyAdditiveBillboard(context);

    m_RenderVS->Bind(context);
    m_RenderPS->Bind(context);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    // GPU が決めた instanceCount で描画（CPU は数を知らない）
    context->DrawInstancedIndirect(m_DrawIndirectBuffer.Get(), 0);

    // 次のパスへステートを持ち越さない
    RenderStates::Get().Restore(context);
    m_RenderVS->UnbindSRVs(context);
}

// ============================================
// システムリセット
// ============================================
void GPUParticleSystem::ResetSystem()
{
    std::vector<GPUParticle> emptyParticles(m_MaxParticles, GPUParticle{});
    m_Context->UpdateSubresource(m_ParticleBuffer.Get(), 0, nullptr, emptyParticles.data(), 0, 0);

    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = m_MaxParticles;

    m_InitDeadListCS->WriteBuffer(m_Context, 0, &dlcb);
    m_InitDeadListCS->Bind(m_Context);

    // ※ここの initialCount = 0 も正当（一度きりのリセット）
    m_DeadList.BindCSUAV(m_Context, 0, 0);
    m_Context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_DeadList.UnbindCSUAV(m_Context, 0);
    m_InitDeadListCS->Unbind(m_Context);

    // リセット時の一度だけ読む（毎フレームではないので許容）
    m_CurrentDeadCount = m_DeadList.ReadDeadCount(m_Context);

    // DrawIndirectArgs もリセット
    UINT clearValues[4] = { 6, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_DrawIndirectUAV.Get(), clearValues);
}

// ============================================
// emitter を積む
//   colorKeyOffset は呼び出し側ではローカル（0 起点）なので、
//   合併後の配列における位置へ付け替える。
// ============================================
void GPUParticleSystem::SubmitEmitters(const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    // 上限を超える分は捨てる（降級：特効が出ないだけ。クラッシュも上書きもしない）
    size_t space = (m_PendingEmitters.size() < MAX_EMITTERS)
        ? (MAX_EMITTERS - m_PendingEmitters.size()) : 0;
    size_t count = (emitters.size() < space) ? emitters.size() : space;

    if (count < emitters.size())
        m_DroppedEmitters += (emitters.size() - count);

    // colorKey は合併後配列の末尾に追加され、その分 offset がずれる
    int baseOffset = static_cast<int>(m_PendingColorKeys.size());

    for (size_t i = 0; i < count; ++i)
    {
        GPUEmitter e = emitters[i];
        e.colorKeyOffset += baseOffset;   // ローカル offset → 全体 offset へ補正
        m_PendingEmitters.push_back(e);
    }

    for (const auto& k : colorKeys)
        m_PendingColorKeys.push_back(k);
}

// ============================================
// 1フレーム分をまとめて GPU へ（1フレーム1回だけ）
// ============================================
void GPUParticleSystem::Flush(float dt, float totalTime)
{
    // emitter が空でも呼ぶ：UpdateCS を走らせて既存粒子を進める必要がある
    Update(dt, totalTime, m_PendingEmitters, m_PendingColorKeys);

    m_PendingEmitters.clear();
    m_PendingColorKeys.clear();
    m_DroppedEmitters = 0;
}