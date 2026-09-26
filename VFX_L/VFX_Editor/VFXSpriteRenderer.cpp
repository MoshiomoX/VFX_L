// ============================================================
// VFXSpriteRenderer.cpp
// ============================================================
#include "VFX_Editor/VFXSpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Manager/ResourceManager.h"
#include "Camera/CameraBase.h"
#include <algorithm>
#include <cstring>
#include <iostream>

using namespace DirectX::SimpleMath;

// カメラの向き（揺れ込みのビュー行列から。粒子のビルボードと同じ取り方）
VFXSpriteCameraCB VFXSpriteCameraCB::From(CameraBase* camera)
{
    VFXSpriteCameraCB cb;
    const Matrix view = camera->GetViewMatrix();
    cb.viewProj = view * camera->GetProjectionMatrix();
    cb.camRight = Vector3(view._11, view._21, view._31);
    cb.camUp = Vector3(view._12, view._22, view._32);
    cb.camForward = -Vector3(view._13, view._23, view._33);   // 右手系：ビューの +Z は後ろ
    return cb;
}

bool VFXSpriteRenderer::Initialize(ID3D11Device* device)
{
    m_VS = ResourceManager::Get().LoadVS(L"VFXSpriteVS", L"Shader/VFX/VFXSpriteVS.hlsl");
    m_PS = ResourceManager::Get().LoadPS(L"VFXSpritePS", L"Shader/VFX/VFXSpritePS.hlsl");
    if (!m_VS || !m_PS) return false;
    if (!EnsureCapacity(device, 256)) return false;
    std::cout << "[OK] VFXSpriteRenderer initialized" << std::endl;
    return true;
}

bool VFXSpriteRenderer::EnsureCapacity(ID3D11Device* device, size_t count)
{
    if (count <= m_Capacity && m_QuadBuffer) return true;
    const size_t cap = (std::max)({ count, m_Capacity * 2, (size_t)256 });

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(VFXSpriteQuad) * cap);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(VFXSpriteQuad);

    Microsoft::WRL::ComPtr<ID3D11Buffer> buf;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)cap;
    if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv))) return false;

    m_QuadBuffer = buf;
    m_QuadSRV = srv;
    m_Capacity = cap;
    return true;
}

// ============================================================
// 描画。呼ぶ側は不透明物の後、粒子の前
// ============================================================
void VFXSpriteRenderer::Render(ID3D11DeviceContext* ctx, CameraBase* camera)
{
    if (m_Items.empty()) return;
    if (!ctx || !camera || !m_VS || !m_PS) { m_Items.clear(); return; }

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    ctx->GetDevice(&device);
    if (!EnsureCapacity(device.Get(), m_Items.size())) { m_Items.clear(); return; }

    // 同じ貼图・同じ採样をまとめる
    std::stable_sort(m_Items.begin(), m_Items.end(),
        [](const VFXSpriteDrawItem& a, const VFXSpriteDrawItem& b)
        {
            if (a.texture != b.texture) return a.texture < b.texture;
            return a.point < b.point;
        });

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(ctx->Map(m_QuadBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) { m_Items.clear(); return; }
    auto* dst = static_cast<VFXSpriteQuad*>(mapped.pData);
    for (size_t i = 0; i < m_Items.size(); ++i)
        dst[i] = m_Items[i].quad;
    ctx->Unmap(m_QuadBuffer.Get(), 0);

    VFXSpriteCameraCB cb = VFXSpriteCameraCB::From(camera);

    m_VS->Bind(ctx);
    m_PS->Bind(ctx);
    ID3D11ShaderResourceView* quads = m_QuadSRV.Get();
    ctx->VSSetShaderResources(0, 1, &quads);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    RenderStates::Get().ApplyAlphaBlend(ctx);   // 乗算済み alpha・深度は読むだけ・両面

    size_t start = 0;
    while (start < m_Items.size())
    {
        size_t end = start + 1;
        while (end < m_Items.size() && m_Items[end].texture == m_Items[start].texture
            && m_Items[end].point == m_Items[start].point)
            ++end;

        cb.first = (uint32_t)start;
        m_VS->WriteBuffer(ctx, 0, &cb);
        ID3D11ShaderResourceView* tex = m_Items[start].texture->GetSRV();
        ctx->PSSetShaderResources(0, 1, &tex);
        ID3D11SamplerState* samp = m_Items[start].point
            ? RenderStates::Get().PointClamp() : RenderStates::Get().LinearClamp();
        ctx->PSSetSamplers(0, 1, &samp);

        ctx->DrawInstanced(6, (UINT)(end - start), 0, 0);
        start = end;
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
    ctx->PSSetShaderResources(0, 1, &nullSRV);
    RenderStates::Get().Restore(ctx);
    m_Items.clear();
}
