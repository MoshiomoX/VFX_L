// ============================================================
// GPUParticleEffectTrail.cpp
// GPUParticleSystem のうち「特効の位置で動く帯」（VFX の Trail entry）の部分。
//
//   CPU : 枠の貸し借りと、帯ごとの錨（先頭の位置 + 命令）を毎フレーム上げるだけ
//   GPU : EffectTrailCS が点の追加・寿命切れ・尾の切り詰めをして生存 list を積み、
//         EffectTrailVS が環から帯を組む（PS は粒子の帯と同じ ParticleTrailPS）
//
// 見た目は粒子の帯と同じ style 表。帯 1 本が style の参照を 1 つ持つので、
// 登録者（entry）が先に解除しても、切り離された帯は消え終わるまで描ける
// ============================================================
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/ShaderPath.h"
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    // StructuredBuffer（DEFAULT、UAV + SRV）。uav が null なら SRV だけ作る
    bool MakeStructured(ID3D11Device* device, UINT stride, UINT count,
        ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11UnorderedAccessView>* uav,
        ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = stride * count;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = stride;
        if (FAILED(device->CreateBuffer(&desc, nullptr, &buf))) return false;

        if (uav)
        {
            D3D11_UNORDERED_ACCESS_VIEW_DESC u = {};
            u.Format = DXGI_FORMAT_UNKNOWN;
            u.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            u.Buffer.NumElements = count;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &u, uav->GetAddressOf()))) return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC s = {};
        s.Format = DXGI_FORMAT_UNKNOWN;
        s.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        s.Buffer.NumElements = count;
        return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &s, &srv));
    }

    // 1 instance = 帯 1 本 = triangle strip（先頭 + 環 + 尾）× 左右 2 頂点
    constexpr UINT kEffectTrailVertexCount = 2 * (kEffectTrailPoints + 2);
}

// ============================================
// 資源
//   shader 2 本 + 錨（dynamic）+ 状態 + 点の環 + 生存 list + indirect args
// ここが失敗しても粒子と粒子の帯は動く（特効の帯が出ないだけ）
// ============================================
bool GPUParticleSystem::CreateEffectTrailResources(ID3D11Device* device)
{
    m_EffectTrailReady = false;

    m_EffectTrailCS = std::make_shared<ComputeShader>();
    HRESULT hr = ShaderPath::Load(m_EffectTrailCS.get(), device, L"Shader/Particle/EffectTrailCS.hlsl");
    std::cout << "[LoadShaders] EffectTrailCS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    m_EffectTrailVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_EffectTrailVS.get(), device, L"Shader/Particle/EffectTrailVS.hlsl");
    std::cout << "[LoadShaders] EffectTrailVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) return false;

    const UINT trails = MAX_EFFECT_TRAILS;

    // ---- 錨：CPU が毎フレーム全枠を書く ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(EffectTrailAnchorGPU) * trails;
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(EffectTrailAnchorGPU);
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_EffectAnchorBuffer))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Format = DXGI_FORMAT_UNKNOWN;
        srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srv.Buffer.NumElements = trails;
        if (FAILED(device->CreateShaderResourceView(m_EffectAnchorBuffer.Get(), &srv, &m_EffectAnchorSRV))) return false;
    }

    // ---- 状態・点の環・生存 list（GPU だけが書く）----
    if (!MakeStructured(device, sizeof(EffectTrailStateGPU), trails,
        m_EffectStateBuffer, &m_EffectStateUAV, m_EffectStateSRV)) return false;
    if (!MakeStructured(device, sizeof(EffectTrailPointGPU), trails * kEffectTrailPoints,
        m_EffectPointBuffer, &m_EffectPointUAV, m_EffectPointSRV)) return false;
    if (!MakeStructured(device, sizeof(uint32_t), trails,
        m_EffectAliveBuffer, &m_EffectAliveUAV, m_EffectAliveSRV)) return false;

    // ---- indirect args ----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;

        const uint32_t initArgs[4] = { kEffectTrailVertexCount, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = initArgs;
        if (FAILED(device->CreateBuffer(&desc, &init, &m_EffectArgsBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uav = {};
        uav.Format = DXGI_FORMAT_R32_UINT;
        uav.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements = 4;
        if (FAILED(device->CreateUnorderedAccessView(m_EffectArgsBuffer.Get(), &uav, &m_EffectArgsUAV))) return false;
    }

    m_EffectTrails.assign(MAX_EFFECT_TRAILS, EffectTrailSlot{});
    m_EffectTrailReady = true;

    std::cout << "[OK] Effect trail resources created (" << MAX_EFFECT_TRAILS << " trails x "
        << kEffectTrailPoints << " points)" << std::endl;
    return true;
}

// ============================================
// 枠の貸し借り
// ============================================
int GPUParticleSystem::CreateEffectTrail(int styleId, const Vector3& pos, float minDistance)
{
    if (!m_EffectTrailReady) return -1;
    if (styleId < 0 || styleId >= (int)m_TrailStyles.size() || !m_TrailStyles[styleId].used) return -1;

    for (int i = 0; i < (int)m_EffectTrails.size(); ++i)
    {
        EffectTrailSlot& t = m_EffectTrails[i];
        if (t.state != EffectTrailSlot::State::Free) continue;

        t = EffectTrailSlot{};
        t.state = EffectTrailSlot::State::Attached;
        t.styleId = styleId;
        t.position = pos;
        t.minDistance = minDistance;
        t.reset = true;
        AddRefTrailStyle(styleId);   // 消え終わるまで style を残す
        return i;
    }
    return -1;
}

void GPUParticleSystem::MoveEffectTrail(int id, const Vector3& pos, float minDistance)
{
    if (id < 0 || id >= (int)m_EffectTrails.size()) return;
    EffectTrailSlot& t = m_EffectTrails[id];
    if (t.state != EffectTrailSlot::State::Attached) return;

    t.position = pos;
    t.minDistance = minDistance;
    t.moved = true;
}

void GPUParticleSystem::ReleaseEffectTrail(int id)
{
    if (id < 0 || id >= (int)m_EffectTrails.size()) return;
    EffectTrailSlot& t = m_EffectTrails[id];
    if (t.state != EffectTrailSlot::State::Attached) return;

    // 最後の点が寿命を使い切るまで待つ。時刻は前回の Flush のもの（ここは Flush より前に呼ばれる）
    // なので、1 フレーム分の余裕を足しておく
    const float lifetime = m_TrailStyles[t.styleId].style.lifetime;
    t.state = EffectTrailSlot::State::Fading;
    t.fadeEnd = m_CachedGlobalCB.totalTime + lifetime + 0.1f;
}

int GPUParticleSystem::GetEffectTrailCount() const
{
    int n = 0;
    for (const auto& t : m_EffectTrails)
        if (t.state != EffectTrailSlot::State::Free) ++n;
    return n;
}

// ============================================
// 記録（DispatchTrail の直後）
//   消え終わった枠を返してから、全枠の錨を上げて EffectTrailCS を回す
// ============================================
void GPUParticleSystem::DispatchEffectTrail(ID3D11DeviceContext* context)
{
    m_EffectTrailDrawn = false;
    if (!m_EffectTrailReady) return;

    // args は毎フレーム戻す（前フレームの数で描かないため）。
    // R32_UINT の UAV を Clear すると 4 要素とも Values[0] になり、InstanceCount が
    // 頂点数から数え始めて生存 list の外（古い値 = 大抵 0 番の帯）を百回以上重ね描きする。丸ごと書く
    const UINT args[4] = { kEffectTrailVertexCount, 0, 0, 0 };
    context->UpdateSubresource(m_EffectArgsBuffer.Get(), 0, nullptr, args, 0, 0);

    const float now = m_CachedGlobalCB.totalTime;

    bool any = false;
    for (auto& t : m_EffectTrails)
    {
        if (t.state == EffectTrailSlot::State::Fading && now >= t.fadeEnd)
        {
            UnregisterTrailStyle(t.styleId);   // CreateEffectTrail で足した参照
            t = EffectTrailSlot{};
        }
        if (t.state != EffectTrailSlot::State::Free) any = true;
    }
    if (!any) return;

    UploadTrailStyles(context);

    // ---- 錨を上げる（使っていない枠は styleSlot = 0）----
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(context->Map(m_EffectAnchorBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    auto* dst = static_cast<EffectTrailAnchorGPU*>(mapped.pData);
    for (int i = 0; i < (int)m_EffectTrails.size(); ++i)
    {
        EffectTrailSlot& t = m_EffectTrails[i];
        EffectTrailAnchorGPU a = {};
        if (t.state != EffectTrailSlot::State::Free)
        {
            a.position = t.position;
            a.minDistance = t.minDistance;
            a.styleSlot = (uint32_t)(t.styleId + 1);
            a.command = (uint32_t)(t.reset ? EffectTrailCommand::Reset
                : t.moved ? EffectTrailCommand::Move
                : EffectTrailCommand::None);
        }
        dst[i] = a;
        t.reset = false;
        t.moved = false;
    }
    context->Unmap(m_EffectAnchorBuffer.Get(), 0);

    EffectTrailCB cb = {};
    cb.now = now;
    cb.trailCount = MAX_EFFECT_TRAILS;

    m_EffectTrailCS->WriteBuffer(context, 0, &cb);
    m_EffectTrailCS->Bind(context);

    m_EffectTrailCS->SetSRV(context, "anchors", m_EffectAnchorSRV.Get());
    m_EffectTrailCS->SetSRV(context, "trailStyles", m_TrailStyleSRV.Get());

    m_EffectTrailCS->SetUAV(context, "states", m_EffectStateUAV.Get());
    m_EffectTrailCS->SetUAV(context, "points", m_EffectPointUAV.Get());
    m_EffectTrailCS->SetUAV(context, "trailAlive", m_EffectAliveUAV.Get());
    m_EffectTrailCS->SetUAV(context, "g_Args", m_EffectArgsUAV.Get());
    m_EffectTrailCS->BindUAVs(context);

    context->Dispatch((MAX_EFFECT_TRAILS + 63) / 64, 1, 1);

    m_EffectTrailCS->UnbindSRVs(context);
    m_EffectTrailCS->UnbindUAVs(context);

    m_EffectTrailDrawn = true;
}

// ============================================
// 描画（RenderTrails の直後）
//   粒子の帯と同じく、特効の帯が使っている style ごとに 1 draw
//   （他の style の instance は VS が捨てる）
// ============================================
void GPUParticleSystem::RenderEffectTrails(ID3D11DeviceContext* context)
{
    if (!m_EffectTrailReady || !m_EffectTrailDrawn || !m_Camera) return;

    bool styleInUse[MAX_TRAIL_STYLES] = {};
    for (const auto& t : m_EffectTrails)
        if (t.state != EffectTrailSlot::State::Free && t.styleId >= 0 && t.styleId < MAX_TRAIL_STYLES)
            styleInUse[t.styleId] = true;

    ParticleRenderCB rcb = {};
    rcb.view = m_Camera->GetViewMatrix().Transpose();
    rcb.projection = m_Camera->GetProjectionMatrix().Transpose();
    rcb.cameraPosition = m_Camera->GetPosition();
    m_EffectTrailVS->WriteBuffer(context, 0, &rcb);

    m_EffectTrailVS->SetSRV(context, "trailAlive", m_EffectAliveSRV.Get());
    m_EffectTrailVS->SetSRV(context, "anchors", m_EffectAnchorSRV.Get());
    m_EffectTrailVS->SetSRV(context, "states", m_EffectStateSRV.Get());
    m_EffectTrailVS->SetSRV(context, "points", m_EffectPointSRV.Get());
    m_EffectTrailVS->SetSRV(context, "trailStyles", m_TrailStyleSRV.Get());

    // 長さ方向（U）は繰り返すので Wrap
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    context->PSSetSamplers(0, 1, &samp);

    m_EffectTrailVS->Bind(context);
    m_TrailPS->Bind(context);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context->IASetInputLayout(nullptr);
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    for (int i = 0; i < MAX_TRAIL_STYLES; ++i)
    {
        if (!styleInUse[i] || !m_TrailStyles[i].used) continue;
        const auto& slot = m_TrailStyles[i];

        const bool alpha = (slot.style.blend == 1);

        TrailDrawCB cb = {};
        cb.styleSlot = (uint32_t)(i + 1);
        cb.premultiply = alpha ? 1u : 0u;   // AlphaBlend() は ONE / INV_SRC_ALPHA（乗算済み alpha）
        cb.time = m_CachedGlobalCB.totalTime;
        m_EffectTrailVS->WriteBuffer(context, 1, &cb);
        m_TrailPS->WriteBuffer(context, 1, &cb);

        Texture* tex = slot.style.texture ? slot.style.texture.get() : m_WhiteTexture.get();
        if (tex) m_TrailPS->SetTexture(context, 0, tex);

        // どちらも深度は読むだけ・両面
        if (alpha) RenderStates::Get().ApplyAlphaBlend(context);
        else       RenderStates::Get().ApplyAdditiveBillboard(context);

        context->DrawInstancedIndirect(m_EffectArgsBuffer.Get(), 0);
    }

    RenderStates::Get().Restore(context);
    m_EffectTrailVS->UnbindSRVs(context);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}
