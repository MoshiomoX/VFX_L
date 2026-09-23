#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include "Camera/CameraBase.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Transform.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Light/LightTypes.h"
#include "Graphics/Renderer/RenderStates.h"
using Microsoft::WRL::ComPtr;
using namespace DirectX::SimpleMath;

struct MVPBuffer
{
    Matrix World;
    Matrix View;
    Matrix Projection;
};

// ============================================================
// 溶解（燃焼消滅）の描画パラメータ。
// RenderSystem が DissolveComponent から詰めて DrawMesh の前に SetDissolve する。
// PS 側は Shader/Common/Dissolve.hlsli（b1 + t5）
// ============================================================
struct DissolveParams
{
    ID3D11ShaderResourceView* noise = nullptr;   // null なら溶解無し
    Vector2 tiling = { 1, 1 };
    Vector2 scroll = { 0, 0 };
    float   threshold = -1.0f;                   // 負 = 無効
    float   edge = 0.05f;
    Vector4 edgeColor = { 1.0f, 0.5f, 0.1f, 1.0f };   // rgb = 縁の色, a = 強さ（HDR）
};

// HLSL の DissolveCB と同じ並び（48B）
struct DissolveCB
{
    Vector2 tiling;
    Vector2 scroll;
    float   threshold;
    float   edge;
    float   _pad[2];
    Vector4 edgeColor;
};
static_assert(sizeof(DissolveCB) == 48, "DissolveCB layout mismatch");

class Renderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    void SetCamera(CameraBase* camera) { m_Camera = camera; }

    void SetDirectionalLight(const Vector3& direction, const Vector3& color, float intensity);
    void SetAmbientColor(const Vector3& color);

    // 次の DrawMesh に効く溶解。null で無効（毎回書くので持ち越さない）
    void SetDissolve(const DissolveParams* p) { m_Dissolve = p; }

    void Begin();
    void DrawMesh(Mesh* mesh, Transform* transform, Material* material = nullptr);
    void End();

    ID3D11Device* GetDevice() const { return m_Device; }
    ID3D11DeviceContext* GetContext() const { return m_Context; }
    const LightBuffer& GetLightData() const { return m_LightData; }
private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    std::shared_ptr<VertexShader> m_DefaultVS;
    std::shared_ptr<PixelShader> m_DefaultPS;
    std::shared_ptr<Texture> m_DefaultTexture;
    CameraBase* m_Camera = nullptr;
    LightBuffer m_LightData;
    const DissolveParams* m_Dissolve = nullptr;
};