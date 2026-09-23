#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/ShaderPath.h"
#include "Graphics/Mesh/Vertex3D.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Model/Model.h"
#include "Graphics/PrimitiveBuilder.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cstddef>
#include <iostream>

#pragma comment(lib, "d3dcompiler.lib")

// kLayoutStatic が VERTEX_3D の実際の並びと一致していることの保証
static_assert(sizeof(VERTEX_3D) == GPUParticleSystem::kLayoutStatic.stride, "kLayoutStatic.stride != sizeof(VERTEX_3D)");
static_assert(offsetof(VERTEX_3D, position) == GPUParticleSystem::kLayoutStatic.posOffset, "kLayoutStatic.posOffset");
static_assert(offsetof(VERTEX_3D, normal) == GPUParticleSystem::kLayoutStatic.normalOffset, "kLayoutStatic.normalOffset");
static_assert(offsetof(VERTEX_3D, uv) == GPUParticleSystem::kLayoutStatic.uvOffset, "kLayoutStatic.uvOffset");

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
    if (!CreateSourceLayoutBuffer(device))   return false;
    if (!m_DeadList.Initialize(device, maxParticles)) return false;

    if (!LoadShaders(device))                return false;
    // if (!CreateRenderStates(device))         return false;
    if (!CreateColorKeyBuffer(device))       return false;
    if (!CreateDrawIndirectBuffer(device))   return false;
    if (!CreateAliveListBuffer(device, maxParticles)) return false;
    if (!CreateCubeResources(device))        return false;
    CreateTrailResources(device);   // 失敗しても粒子は動く（帯が出ないだけ）

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
// Mesh 発射源のレイアウト表（StructuredBuffer<EmitSourceLayout>）
// 全発射源分を 1 本に持ち、EmitCS は e.sourceId で引く
// ============================================
bool GPUParticleSystem::CreateSourceLayoutBuffer(ID3D11Device* device)
{
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(EmitSourceLayout) * MAX_EMIT_SOURCES;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    desc.StructureByteStride = sizeof(EmitSourceLayout);

    if (FAILED(device->CreateBuffer(&desc, nullptr, &m_SourceLayoutBuffer)))
    {
        std::cout << "[Error] SourceLayoutBuffer creation failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = MAX_EMIT_SOURCES;

    if (FAILED(device->CreateShaderResourceView(m_SourceLayoutBuffer.Get(), &srvDesc, &m_SourceLayoutSRV)))
    {
        std::cout << "[Error] SourceLayoutSRV creation failed" << std::endl;
        return false;
    }

    m_EmitSources.reserve(MAX_EMIT_SOURCES);
    m_SourceLayoutDirty = true;
    return true;
}

void GPUParticleSystem::UploadSourceLayouts(ID3D11DeviceContext* context)
{
    if (!m_SourceLayoutDirty) return;
    m_SourceLayoutDirty = false;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_SourceLayoutBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    EmitSourceLayout* dst = reinterpret_cast<EmitSourceLayout*>(mapped.pData);
    for (int i = 0; i < MAX_EMIT_SOURCES; ++i)
    {
        // 未使用枠は stride 0。EmitMesh は stride 0 を見て発射しない
        dst[i] = (i < (int)m_EmitSources.size() && m_EmitSources[i].used)
            ? m_EmitSources[i].layout : EmitSourceLayout{};
    }
    context->Unmap(m_SourceLayoutBuffer.Get(), 0);
}

// ============================================
// Mesh 発射源の登録 / 解除
// 空いた枠を再利用する。id は枠の添字
// ============================================
int GPUParticleSystem::RegisterEmitSource(ID3D11ShaderResourceView* rawSRV, uint32_t vertexCount,
    const EmitSourceLayout& layout,
    ID3D11ShaderResourceView* rawIndexSRV, uint32_t indexCount, uint32_t indexBytes)
{
    if (!rawSRV || vertexCount == 0 || layout.stride == 0)
    {
        std::cout << "[GPUParticleSystem] RegisterEmitSource: invalid source (srv="
            << (rawSRV ? "ok" : "null") << " count=" << vertexCount
            << " stride=" << layout.stride << ")" << std::endl;
        return -1;
    }

    int id = -1;
    for (int i = 0; i < (int)m_EmitSources.size(); ++i)
        if (!m_EmitSources[i].used) { id = i; break; }

    if (id < 0)
    {
        if ((int)m_EmitSources.size() >= MAX_EMIT_SOURCES)
        {
            std::cout << "[GPUParticleSystem] RegisterEmitSource: no free slot (max "
                << MAX_EMIT_SOURCES << ")" << std::endl;
            return -1;
        }
        m_EmitSources.emplace_back();
        id = (int)m_EmitSources.size() - 1;
    }

    EmitSource& s = m_EmitSources[id];
    s.srv = rawSRV;
    s.vertexCount = vertexCount;
    s.layout = layout;
    s.used = true;

    // 三角形発射：index buffer があれば面から出す
    s.indexSRV = rawIndexSRV;
    if (rawIndexSRV && indexCount >= 3 && (indexBytes == 2 || indexBytes == 4))
    {
        s.layout.triangleCount = indexCount / 3;
        s.layout.indexBytes = indexBytes;
    }
    else
    {
        s.indexSRV.Reset();
        s.layout.triangleCount = 0;
        s.layout.indexBytes = 0;
    }
    m_SourceLayoutDirty = true;

    if (!CreateEdgeBuffers(id))
    {
        m_EmitSources[id] = EmitSource{};
        return -1;
    }
    return id;
}

void GPUParticleSystem::UnregisterEmitSource(int id)
{
    if (!IsEmitSourceValid(id)) return;
    m_EmitSources[id] = EmitSource{};
    m_SourceLayoutDirty = true;
}

// ============================================
// 溶解の縁の頂点表（発射源ごと）
//   edgeBuffer      : AppendStructuredBuffer<uint>、容量 = 頂点数
//   edgeCountBuffer : CopyStructureCount の受け皿（EmitCS が Buffer<uint> で読む）
// ============================================
bool GPUParticleSystem::CreateEdgeBuffers(int sourceId)
{
    EmitSource& s = m_EmitSources[sourceId];

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(uint32_t) * s.vertexCount;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(uint32_t);
    if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &s.edgeBuffer)))
    {
        std::cout << "[Error] EdgeBuffer creation failed (source " << sourceId << ")" << std::endl;
        return false;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = s.vertexCount;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
    if (FAILED(m_Device->CreateUnorderedAccessView(s.edgeBuffer.Get(), &ud, &s.edgeUAV)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = s.vertexCount;
    if (FAILED(m_Device->CreateShaderResourceView(s.edgeBuffer.Get(), &sd, &s.edgeSRV)))
        return false;

    // 数。表を作る前は 0（EmitMesh は 0 を見て発射しない）
    const uint32_t zero = 0;
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = &zero;
    D3D11_BUFFER_DESC cd = {};
    cd.ByteWidth = sizeof(uint32_t);
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(m_Device->CreateBuffer(&cd, &init, &s.edgeCountBuffer)))
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC csd = {};
    csd.Format = DXGI_FORMAT_R32_UINT;
    csd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    csd.Buffer.NumElements = 1;
    if (FAILED(m_Device->CreateShaderResourceView(s.edgeCountBuffer.Get(), &csd, &s.edgeCountSRV)))
        return false;

    return true;
}

void GPUParticleSystem::SetSourceEdgeParams(int sourceId, const EdgeFilterParams& params)
{
    if (!IsEmitSourceValid(sourceId)) return;
    if (!params.noiseSRV) return;   // noise が無ければ縁は定義できない
    m_EmitSources[sourceId].edgeParams = params;
    m_EmitSources[sourceId].edgeRequested = true;
}

// ============================================
// EdgeFilterCS：1 スレッド 1 頂点で noise を引き、縁の頂点だけ Append
// initialCount = 0 で計数器を毎回リセット（表は毎フレーム作り直す）
// ============================================
void GPUParticleSystem::DispatchEdgeFilter(ID3D11DeviceContext* context, int sourceId)
{
    EmitSource& s = m_EmitSources[sourceId];
    if (!m_EdgeFilterCS || !s.edgeUAV) return;

    EdgeFilterCB cb = {};
    cb.noiseTiling = s.edgeParams.noiseTiling;
    cb.noiseScroll = s.edgeParams.noiseScroll;
    cb.threshold = s.edgeParams.threshold;
    cb.edge = s.edgeParams.edge;
    cb.vertexCount = s.vertexCount;
    cb.sourceId = (uint32_t)sourceId;

    m_EdgeFilterCS->WriteBuffer(context, 2, &cb);
    m_EdgeFilterCS->Bind(context);

    m_EdgeFilterCS->SetSRV(context, "emitSource", s.srv.Get());
    m_EdgeFilterCS->SetSRV(context, "sourceLayouts", m_SourceLayoutSRV.Get());
    m_EdgeFilterCS->SetSRV(context, "noiseTex", s.edgeParams.noiseSRV);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->CSSetSamplers(0, 1, &samp);

    m_EdgeFilterCS->SetUAV(context, "edgeIndices", s.edgeUAV.Get(), 0);
    m_EdgeFilterCS->BindUAVs(context);

    context->Dispatch((s.vertexCount + 255) / 256, 1, 1);

    m_EdgeFilterCS->UnbindSRVs(context);
    m_EdgeFilterCS->UnbindUAVs(context);

    // 数は GPU 上に置いたまま EmitCS へ（CPU へは戻さない）
    context->CopyStructureCount(s.edgeCountBuffer.Get(), 0, s.edgeUAV.Get());
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

    m_EdgeFilterCS = std::make_shared<ComputeShader>();
    hr = ShaderPath::Load(m_EdgeFilterCS.get(), device, L"Shader/Particle/EdgeFilterCS.hlsl");
    std::cout << "[LoadShaders] EdgeFilterCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_CubeVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_CubeVS.get(), device, L"Shader/Particle/ParticleCubeVS.hlsl");
    std::cout << "[LoadShaders] CubeVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_CubePS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_CubePS.get(), device, L"Shader/PS.hlsl");
    std::cout << "[LoadShaders] CubePS (Shader/PS): " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
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
    // Mesh 発射器は発射源ごとに Dispatch を分けるので、pass 別に数える。
    // 未登録の sourceId を指す Mesh 発射器はどの pass にも入らない（静かに捨てる）
    uint32_t requestedPlain = 0;
    uint32_t requestedPerSource[MAX_EMIT_SOURCES] = {};
    for (auto& e : emitters)
    {
        if (e.isActive <= 0.5f) continue;

        if (e.emitType == static_cast<int>(EmitType::Mesh))
        {
            if (IsEmitSourceValid(e.sourceId))
                requestedPerSource[e.sourceId] += e.emitCount;
        }
        else
        {
            requestedPlain += e.emitCount;
        }
    }

    UploadExternalEmitters(context, emitters, colorKeys);

    // 溶解の縁の表を、今フレーム依頼された発射源だけ作り直す（Emit の前）
    UploadSourceLayouts(context);
    for (int i = 0; i < (int)m_EmitSources.size(); ++i)
    {
        if (m_EmitSources[i].used && m_EmitSources[i].edgeRequested)
            DispatchEdgeFilter(context, i);
        m_EmitSources[i].edgeRequested = false;
    }

    DispatchEmit(context, requestedPlain, requestedPerSource);
    DispatchUpdate(context);
    DispatchTrail(context);   // 更新後の位置を帯の環へ記録する
}

void GPUParticleSystem::UploadExternalEmitters(ID3D11DeviceContext* context,
    const std::vector<GPUEmitter>& emitters,
    const std::vector<ColorKey>& colorKeys)
{
    if (!emitters.empty())
    {
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (SUCCEEDED(context->Map(m_EmitterBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            size_t count = (std::min)(emitters.size(), static_cast<size_t>(MAX_EMITTERS));
            memcpy(mapped.pData, emitters.data(), sizeof(GPUEmitter) * count);
            context->Unmap(m_EmitterBuffer.Get(), 0);
        }
    }

    // ---- colorKey: 静的区（Swarm 用、offset 0 起点）→ pending の順で1本に詰める ----
    // 静的区は Swarm の粒子が毎フレーム参照するので、
    // pending（CPU 側 emitter）が空でも書く必要がある
    const size_t staticN = m_StaticColorKeys.size();
    const size_t total = staticN + colorKeys.size();
    if (total == 0) return;

    D3D11_MAPPED_SUBRESOURCE cm = {};
    if (SUCCEEDED(context->Map(m_ColorKeyBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &cm)))
    {
        ColorKey* dst = reinterpret_cast<ColorKey*>(cm.pData);
        size_t written = 0;

        for (size_t i = 0; i < staticN && written < MAX_COLOR_KEYS_TOTAL; ++i)
            dst[written++] = m_StaticColorKeys[i];

        for (size_t i = 0; i < colorKeys.size() && written < MAX_COLOR_KEYS_TOTAL; ++i)
            dst[written++] = colorKeys[i];

        context->Unmap(m_ColorKeyBuffer.Get(), 0);
    }
}

// ============================================
// Emit ディスパッチ
// 発射数の上限は GPU が決める。CPU は「何発撃ちたいか」だけ渡す。
// ============================================
void GPUParticleSystem::DispatchEmit(ID3D11DeviceContext* context, uint32_t requestedPlain,
    const uint32_t* requestedPerSource)
{
    bool any = (requestedPlain > 0);
    for (int i = 0; i < MAX_EMIT_SOURCES && !any; ++i)
        any = (requestedPerSource[i] > 0);
    if (!any) return;

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

    UploadSourceLayouts(context);
    m_EmitCS->WriteBuffer(context, 0, &m_CachedGlobalCB);

    // pass 0: Mesh 以外（従来どおり 1 回）
    if (requestedPlain > 0)
        DispatchEmitPass(context, -1, requestedPlain);

    // pass 1..: Mesh 発射源ごと。玩家 + 精英の個位数なので回数は気にしない
    for (int i = 0; i < MAX_EMIT_SOURCES; ++i)
        if (requestedPerSource[i] > 0)
            DispatchEmitPass(context, i, requestedPerSource[i]);
}

void GPUParticleSystem::DispatchEmitPass(ID3D11DeviceContext* context, int activeSource, uint32_t requestedEmit)
{
    // 空き数を GPU 上のバッファへ写す（Copy のみ、Map しない = 待たない）。
    // ※pass ごとに写し直す。前の pass が Consume した分を見ないと
    //   deadCount の防波堤が古い値で判定し、計数器が下溢する
    context->CopyStructureCount(m_DeadCountBuffer.Get(), 0, m_DeadList.GetUAV());

    EmitPassCB pcb = {};
    pcb.activeSource = activeSource;
    m_EmitCS->WriteBuffer(context, 2, &pcb);
    m_EmitCS->Bind(context);

    // SRV は即時バインド
    m_EmitCS->SetSRV(context, "emitters", m_EmitterSRV.Get());
    m_EmitCS->SetSRV(context, "deadCount", m_DeadCountSRV.Get());
    m_EmitCS->SetSRV(context, "sourceLayouts", m_SourceLayoutSRV.Get());
    if (activeSource >= 0)
    {
        const EmitSource& s = m_EmitSources[activeSource];
        m_EmitCS->SetSRV(context, "emitSource", s.srv.Get());
        m_EmitCS->SetSRV(context, "emitIndices", s.indexSRV.Get());   // null なら頂点発射
        m_EmitCS->SetSRV(context, "edgeIndices", s.edgeSRV.Get());
        m_EmitCS->SetSRV(context, "edgeCount", s.edgeCountSRV.Get());
    }

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

    // 立方体側の args も毎フレーム戻す（5 uint なので Clear ではなく丸ごと書く）
    if (m_CubeModel && !m_CubeModel->GetSubMeshes().empty())
    {
        const UINT cubeArgs[5] = { m_CubeModel->GetSubMeshes()[0].mesh->GetIndexCount(), 0, 0, 0, 0 };
        context->UpdateSubresource(m_DrawIndirectCubeBuffer.Get(), 0, nullptr, cubeArgs, 0, 0);
    }

    m_UpdateCS->Bind(context);

    // SRV は即時バインド
    m_UpdateCS->SetSRV(context, "colorKeys", m_ColorKeySRV.Get());

    // UAV はキューに溜めて一括バインド
    m_UpdateCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    m_UpdateCS->SetUAV(context, "deadList", m_DeadList.GetUAV());
    m_UpdateCS->SetUAV(context, "g_DrawArgs", m_DrawIndirectUAV.Get());
    m_UpdateCS->SetUAV(context, "aliveList", m_AliveListUAV.Get());
    m_UpdateCS->SetUAV(context, "g_DrawArgsCube", m_DrawIndirectCubeUAV.Get());
    m_UpdateCS->SetUAV(context, "aliveCube", m_AliveCubeUAV.Get());
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

    // ---- 軌跡（帯）----
    RenderTrails(context);

    // ---- 立方体（不透明、深度書き込みあり）----
    RenderCubes(context);
}

// ============================================
// 立方体粒子の資源
//   単位立方体 Mesh（VERTEX_3D）+ aliveCube + 5 uint の args
// ============================================
bool GPUParticleSystem::CreateCubeResources(ID3D11Device* device)
{
    m_CubeModel = PrimitiveBuilder::CreateBox(device, { 0.5f, 0.5f, 0.5f }, { 1, 1, 1, 1 });
    if (!m_CubeModel || m_CubeModel->GetSubMeshes().empty())
    {
        std::cout << "[Error] cube mesh creation failed" << std::endl;
        return false;
    }

    m_WhiteTexture = std::make_shared<Texture>();
    m_WhiteTexture->CreateSolid(device, 255, 255, 255, 255);

    // aliveCube（aliveList と同じ作り）
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * m_MaxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(uint32_t);
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_AliveCubeBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = m_MaxParticles;
        if (FAILED(device->CreateUnorderedAccessView(m_AliveCubeBuffer.Get(), &ud, &m_AliveCubeUAV))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = m_MaxParticles;
        if (FAILED(device->CreateShaderResourceView(m_AliveCubeBuffer.Get(), &sd, &m_AliveCubeSRV))) return false;
    }

    // DrawIndexedInstancedIndirect の args
    // { IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation }
    {
        const UINT initArgs[5] = { m_CubeModel->GetSubMeshes()[0].mesh->GetIndexCount(), 0, 0, 0, 0 };
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(initArgs);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = initArgs;
        if (FAILED(device->CreateBuffer(&desc, &init, &m_DrawIndirectCubeBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_R32_UINT;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = 5;
        if (FAILED(device->CreateUnorderedAccessView(m_DrawIndirectCubeBuffer.Get(), &ud, &m_DrawIndirectCubeUAV))) return false;
    }

    std::cout << "[OK] Cube particle resources created" << std::endl;
    return true;
}

// ============================================
// 立方体粒子の描画
//   VS: ParticleCubeVS（MVP は b2、World は使わない）
//   PS: Shader/PS.hlsl（Lambert、LightBuffer は b0）
//   不透明・深度書き込みあり。ビルボードの後に描く
// ============================================
void GPUParticleSystem::RenderCubes(ID3D11DeviceContext* context)
{
    if (!m_CubeVS || !m_CubeVS->IsValid()) return;
    if (!m_CubePS || !m_CubePS->IsValid()) return;
    if (!m_CubeModel || m_CubeModel->GetSubMeshes().empty()) return;

    // ModelCommon.hlsli の MVPBuffer と同じ並び（row_major なので転置しない）
    struct { Matrix W, V, P; } mvp{ Matrix::Identity,
        m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix() };
    static_assert(sizeof(mvp) == 192, "MVPBuffer layout mismatch");

    LightBuffer light = m_Light;
    light.cameraPosition = m_Camera->GetPosition();

    RenderStates::Get().ApplyOpaque(context);

    m_CubeVS->Bind(context);
    m_CubePS->Bind(context);

    m_CubeVS->WriteBuffer(context, 2, &mvp);
    m_CubeVS->SetSRV(context, "particles", m_ParticleSRV.Get());
    m_CubeVS->SetSRV(context, "aliveCube", m_AliveCubeSRV.Get());

    m_CubePS->WriteBuffer(context, 0, &light);
    if (m_WhiteTexture)
        m_CubePS->SetTexture(context, 0, m_WhiteTexture.get());
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->PSSetSamplers(0, 1, &samp);

    m_CubeModel->GetSubMeshes()[0].mesh->DrawIndexedInstancedIndirect(
        context, m_DrawIndirectCubeBuffer.Get(), 0);

    RenderStates::Get().Restore(context);
    m_CubeVS->UnbindSRVs(context);
    m_CubePS->UnbindSRVs(context);
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

    // 帯の args も。粒子は全部 0 埋めしたので trailStyle / trailState も 0 に戻っている
    if (m_TrailReady)
    {
        const UINT trailArgs[4] = { 2 * (kTrailPoints + 1), 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_TrailArgsUAV.Get(), trailArgs);
    }

    if (m_CubeModel && !m_CubeModel->GetSubMeshes().empty())
    {
        const UINT cubeArgs[5] = { m_CubeModel->GetSubMeshes()[0].mesh->GetIndexCount(), 0, 0, 0, 0 };
        m_Context->UpdateSubresource(m_DrawIndirectCubeBuffer.Get(), 0, nullptr, cubeArgs, 0, 0);
    }
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
        // colorKey は「静的区（Swarm）→ 今フレームの pending」の順で並ぶ。
    // 静的区の分だけ offset をずらす
    int baseOffset = static_cast<int>(m_StaticColorKeys.size() + m_PendingColorKeys.size());

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

// ============================================
// 粒子の軌跡（帯）の資源
//   shader 3 本 + 位置の環 + 生存 list + indirect args + style 表
// ここが失敗しても粒子本体は動かす（帯が出ないだけ）
// ============================================
bool GPUParticleSystem::CreateTrailResources(ID3D11Device* device)
{
    m_TrailReady = false;

    m_TrailCS = std::make_shared<ComputeShader>();
    HRESULT hr = ShaderPath::Load(m_TrailCS.get(), device, L"Shader/Particle/ParticleTrailCS.hlsl");
    std::cout << "[LoadShaders] TrailCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_TrailVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_TrailVS.get(), device, L"Shader/Particle/ParticleTrailVS.hlsl");
    std::cout << "[LoadShaders] TrailVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_TrailPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_TrailPS.get(), device, L"Shader/Particle/ParticleTrailPS.hlsl");
    std::cout << "[LoadShaders] TrailPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    // ---- 位置の環：粒子数 × kTrailPoints × float3 ----
    {
        const UINT count = m_MaxParticles * kTrailPoints;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(float) * 3 * count;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(float) * 3;
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_TrailPointsBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = count;
        if (FAILED(device->CreateUnorderedAccessView(m_TrailPointsBuffer.Get(), &uav, &m_TrailPointsUAV))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = count;
        if (FAILED(device->CreateShaderResourceView(m_TrailPointsBuffer.Get(), &srv, &m_TrailPointsSRV))) return false;
    }

    // ---- 生存 list ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * m_MaxParticles;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(uint32_t);
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_TrailAliveBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_UNKNOWN;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = m_MaxParticles;
        if (FAILED(device->CreateUnorderedAccessView(m_TrailAliveBuffer.Get(), &uav, &m_TrailAliveUAV))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = m_MaxParticles;
        if (FAILED(device->CreateShaderResourceView(m_TrailAliveBuffer.Get(), &srv, &m_TrailAliveSRV))) return false;
    }

    // ---- indirect args：1 instance = 帯 1 本 = triangle strip 2 * (点数 + 先頭) 頂点 ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

        const uint32_t initArgs[4] = { 2 * (kTrailPoints + 1), 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = initArgs;
        if (FAILED(device->CreateBuffer(&desc, &init, &m_TrailArgsBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R32_UINT;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 4;
        if (FAILED(device->CreateUnorderedAccessView(m_TrailArgsBuffer.Get(), &uav, &m_TrailArgsUAV))) return false;
    }

    // ---- style 表（CPU から書く）----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(ParticleTrailStyleGPU) * MAX_TRAIL_STYLES;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(ParticleTrailStyleGPU);
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_TrailStyleBuffer))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = MAX_TRAIL_STYLES;
        if (FAILED(device->CreateShaderResourceView(m_TrailStyleBuffer.Get(), &srv, &m_TrailStyleSRV))) return false;
    }

    m_TrailStyles.assign(MAX_TRAIL_STYLES, TrailStyleSlot{});
    m_TrailStylesDirty = true;
    m_TrailReady = true;

    std::cout << "[OK] Particle trail resources created ("
        << (sizeof(float) * 3 * m_MaxParticles * kTrailPoints) / (1024 * 1024) << " MB ring)" << std::endl;
    return true;
}

// ============================================
// style の登録 / 更新 / 解除
// ============================================
int GPUParticleSystem::RegisterTrailStyle(const ParticleTrailStyle& style)
{
    if (!m_TrailReady) return -1;
    for (int i = 0; i < (int)m_TrailStyles.size(); ++i)
    {
        if (m_TrailStyles[i].used) continue;
        m_TrailStyles[i].style = style;
        m_TrailStyles[i].used = true;
        m_TrailStylesDirty = true;
        return i;
    }
    return -1;
}

void GPUParticleSystem::UpdateTrailStyle(int id, const ParticleTrailStyle& style)
{
    if (id < 0 || id >= (int)m_TrailStyles.size() || !m_TrailStyles[id].used) return;

    // GPU へ上げる部分が変わった時だけ表を上げ直す（毎フレーム呼ばれる前提）
    const ParticleTrailStyleGPU a = m_TrailStyles[id].style.ToGPU();
    const ParticleTrailStyleGPU b = style.ToGPU();
    if (memcmp(&a, &b, sizeof(a)) != 0) m_TrailStylesDirty = true;

    m_TrailStyles[id].style = style;
}

void GPUParticleSystem::UnregisterTrailStyle(int id)
{
    if (id < 0 || id >= (int)m_TrailStyles.size()) return;
    m_TrailStyles[id] = TrailStyleSlot{};   // texture の参照もここで放す
}

void GPUParticleSystem::UploadTrailStyles(ID3D11DeviceContext* context)
{
    if (!m_TrailStylesDirty) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_TrailStyleBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

    auto* dst = static_cast<ParticleTrailStyleGPU*>(mapped.pData);
    for (int i = 0; i < MAX_TRAIL_STYLES; ++i)
        dst[i] = m_TrailStyles[i].used ? m_TrailStyles[i].style.ToGPU() : ParticleTrailStyleGPU{};

    context->Unmap(m_TrailStyleBuffer.Get(), 0);
    m_TrailStylesDirty = false;
}

// ============================================
// 帯の記録（UpdateCS の直後）
//   帯を持つ生存粒子を list に積み、時間の格子を跨いだ粒子は位置を環へ足す
// ============================================
void GPUParticleSystem::DispatchTrail(ID3D11DeviceContext* context)
{
    if (!m_TrailReady) return;

    // args は毎フレーム戻す（style が 1 つも無い時も。前フレームの数で描かないため）
    const UINT trailArgs[4] = { 2 * (kTrailPoints + 1), 0, 0, 0 };
    context->ClearUnorderedAccessViewUint(m_TrailArgsUAV.Get(), trailArgs);

    bool anyStyle = false;
    for (const auto& s : m_TrailStyles) if (s.used) { anyStyle = true; break; }
    if (!anyStyle) return;   // 満池 dispatch を 1 回省く

    UploadTrailStyles(context);

    DeadListCB dlcb = {};
    dlcb.deadCount = 0;
    dlcb.maxParticles = m_MaxParticles;

    m_TrailCS->WriteBuffer(context, 0, &m_CachedGlobalCB);
    m_TrailCS->WriteBuffer(context, 1, &dlcb);
    m_TrailCS->Bind(context);

    m_TrailCS->SetSRV(context, "trailStyles", m_TrailStyleSRV.Get());

    m_TrailCS->SetUAV(context, "particles", m_ParticleUAV.Get());
    m_TrailCS->SetUAV(context, "trailPoints", m_TrailPointsUAV.Get());
    m_TrailCS->SetUAV(context, "trailAlive", m_TrailAliveUAV.Get());
    m_TrailCS->SetUAV(context, "g_TrailArgs", m_TrailArgsUAV.Get());
    m_TrailCS->BindUAVs(context);

    context->Dispatch((m_MaxParticles + 255) / 256, 1, 1);

    m_TrailCS->UnbindSRVs(context);
    m_TrailCS->UnbindUAVs(context);
}

// ============================================
// 帯の描画
//   頂点 buffer は無い。1 instance = 粒子 1 個、SV_VertexID で triangle strip を辿る。
//   貼图と合成方法が style ごとに違うので、使用中の style の数だけ draw する
//   （他の style の instance は VS が捨てる）
// ============================================
void GPUParticleSystem::RenderTrails(ID3D11DeviceContext* context)
{
    if (!m_TrailReady || !m_Camera) return;

    bool anyStyle = false;
    for (const auto& s : m_TrailStyles) if (s.used) { anyStyle = true; break; }
    if (!anyStyle) return;

    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();
    m_TrailVS->WriteBuffer(context, 0, &rcb);

    m_TrailVS->SetSRV(context, "particles", m_ParticleSRV.Get());
    m_TrailVS->SetSRV(context, "trailAlive", m_TrailAliveSRV.Get());
    m_TrailVS->SetSRV(context, "trailStyles", m_TrailStyleSRV.Get());
    m_TrailVS->SetSRV(context, "trailPoints", m_TrailPointsSRV.Get());

    // 長さ方向（U）は繰り返すので Wrap
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->PSSetSamplers(0, 1, &samp);

    m_TrailVS->Bind(context);
    m_TrailPS->Bind(context);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    for (int i = 0; i < (int)m_TrailStyles.size(); ++i)
    {
        const auto& slot = m_TrailStyles[i];
        if (!slot.used) continue;

        const bool alpha = (slot.style.blend == 1);

        TrailDrawCB cb = {};
        cb.styleSlot = (uint32_t)(i + 1);
        cb.premultiply = alpha ? 1u : 0u;   // AlphaBlend() は ONE / INV_SRC_ALPHA（乗算済み alpha）
        cb.time = m_CachedGlobalCB.totalTime;
        m_TrailVS->WriteBuffer(context, 1, &cb);
        m_TrailPS->WriteBuffer(context, 1, &cb);

        Texture* tex = slot.style.texture ? slot.style.texture.get() : m_WhiteTexture.get();
        if (tex) m_TrailPS->SetTexture(context, 0, tex);

        // どちらも深度は読むだけ・両面
        if (alpha) RenderStates::Get().ApplyAlphaBlend(context);
        else       RenderStates::Get().ApplyAdditiveBillboard(context);

        context->DrawInstancedIndirect(m_TrailArgsBuffer.Get(), 0);
    }

    RenderStates::Get().Restore(context);
    m_TrailVS->UnbindSRVs(context);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
