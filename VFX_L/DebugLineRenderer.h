#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include <Effects.h>
#include <VertexTypes.h>
#include <memory>
#include <wrl/client.h>
#include "VertexBuffer.h"

using namespace DirectX::SimpleMath;

class CameraBase;

class DebugLineRenderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    void AddLine(const Vector3& start, const Vector3& end, const Color& color);
    void Render();

private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    std::unique_ptr<DirectX::BasicEffect> m_Effect;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> m_InputLayout;
    VertexBuffer<DirectX::VertexPositionColor> m_VertexBuffer;

    // ライン蓄積
    static const int MAX_LINES = 10000;
    std::vector<DirectX::VertexPositionColor> m_LineVerts;

    CameraBase* m_Camera = nullptr;
};