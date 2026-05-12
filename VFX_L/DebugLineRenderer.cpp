#include "DebugLineRenderer.h"
#include "CameraBase.h"
#include <iostream>

bool DebugLineRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;

    // BasicEffect作成（頂点色のみ、ライトなし）
    m_Effect = std::make_unique<DirectX::BasicEffect>(device);
    m_Effect->SetVertexColorEnabled(true);
    m_Effect->SetLightingEnabled(false);

    // InputLayout作成
    const void* byteCode;
    size_t codeLength;
    m_Effect->GetVertexShaderBytecode(&byteCode, &codeLength);

    HRESULT hr = device->CreateInputLayout(
        DirectX::VertexPositionColor::InputElements,
        DirectX::VertexPositionColor::InputElementCount,
        byteCode, codeLength,
        m_InputLayout.ReleaseAndGetAddressOf());

    if (FAILED(hr))
    {
        std::cout << "[Error] DebugLineRenderer InputLayout failed" << std::endl;
        return false;
    }

    // 空のDynamicバッファを作成
    m_VertexBuffer.CreateEmpty(device, MAX_LINES * 2);

    m_LineVerts.reserve(MAX_LINES * 2);

    std::cout << "[OK] DebugLineRenderer initialized" << std::endl;
    return true;
}

void DebugLineRenderer::Shutdown()
{
    m_Effect.reset();
    m_InputLayout.Reset();
}

void DebugLineRenderer::AddLine(const Vector3& start, const Vector3& end, const Color& color)
{
    if (m_LineVerts.size() >= MAX_LINES * 2) return;

    m_LineVerts.push_back({ start, color });
    m_LineVerts.push_back({ end, color });
}

void DebugLineRenderer::Render()
{
    if (m_LineVerts.empty() || !m_Camera) return;

    // 行列設定
    m_Effect->SetWorld(Matrix::Identity);
    m_Effect->SetView(m_Camera->GetViewMatrix());
    m_Effect->SetProjection(m_Camera->GetProjectionMatrix());

    // パイプライン設定
    m_Effect->Apply(m_Context);
    m_Context->IASetInputLayout(m_InputLayout.Get());
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

    // 頂点データ更新・描画
    m_VertexBuffer.Modify(m_Context, m_LineVerts);
    m_VertexBuffer.SetGPU(m_Context);
    m_Context->Draw(static_cast<UINT>(m_LineVerts.size()), 0);

    // フレーム末にクリア
    m_LineVerts.clear();
}