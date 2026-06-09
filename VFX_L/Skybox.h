#pragma once
#include <memory>
#include <string>
#include <d3d11.h>
#include <wrl/client.h>
#include "Model.h"
#include "Transform.h"
#include "VertexShader.h"
#include "PixelShader.h"
#include "Texture.h"

using Microsoft::WRL::ComPtr;
class CameraBase;
class Renderer;

class Skybox
{
public:
    bool Init(ID3D11Device* device,
        const std::string& modelPath,
        const std::wstring& texturePath);

    // カメラ追従 + 専用ステートで描画
    void Render(Renderer& renderer, CameraBase* camera);

private:
    std::shared_ptr<Model> m_SphereModel;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    std::shared_ptr<Texture> m_Texture;
    Transform m_Transform;

    // スカイボックス専用ステート
    ComPtr<ID3D11RasterizerState> m_FrontCullRS;   // 前面カリング（内面描画）
    ComPtr<ID3D11DepthStencilState> m_DepthState;  // 深度
    ComPtr<ID3D11SamplerState> m_Sampler;

    float m_Scale = 0.0005f;   // 球の大きさ（十分大きく）
};