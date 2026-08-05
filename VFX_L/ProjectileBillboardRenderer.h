// ============================================================
// ProjectileBillboardRenderer.h
// 投射物の芯をビルボードで一括描画する（インスタンシング）。
// 位置は CPU 権威（衝突判定と同じ TransformComponent を読む）ので、
// 見た目と当たり判定が絶対にズレない。
// 描画ステートは RenderStates に一元化。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <SimpleMath.h>

class Registry;
class CameraBase;
class Texture;
class VertexShader;
class PixelShader;

class ProjectileBillboardRenderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
        UINT maxProjectiles = 4096);
    void Shutdown();

    void SetTexture(std::shared_ptr<Texture> tex) { m_Texture = tex; }

    // 不透明描画の後に呼ぶ（加算合成なので順序は問わない）
    void Render(Registry& reg, CameraBase* camera);

    UINT GetLastDrawCount() const { return m_LastDrawCount; }

private:
    // HLSL の ProjectileInstance と一致させる（48 bytes）
    struct ProjectileInstance
    {
        DirectX::SimpleMath::Vector3 position;
        float                        size;
        DirectX::SimpleMath::Vector4 color;
        DirectX::SimpleMath::Vector3 velocity;
        float                        stretch;
    };

    struct RenderCB
    {
        DirectX::SimpleMath::Matrix  view;
        DirectX::SimpleMath::Matrix  projection;
        DirectX::SimpleMath::Vector3 cameraPosition;
        float                        pad0;
    };

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_InstanceBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_InstanceSRV;

    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
    std::shared_ptr<Texture>      m_Texture;

    std::vector<ProjectileInstance> m_Instances;   // CPU 側の積み上げ
    UINT m_MaxProjectiles = 0;
    UINT m_LastDrawCount = 0;
};