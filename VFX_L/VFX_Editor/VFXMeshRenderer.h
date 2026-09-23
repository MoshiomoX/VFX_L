#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <vector>
#include <memory>
#include <cstdint>

class Model;
class Texture;
class CameraBase;
class VertexShader;
class PixelShader;
using Microsoft::WRL::ComPtr;

// PS b1。HLSL の VFXMeshCB と同じ並び（96B）
struct VFXMeshParams
{
    DirectX::SimpleMath::Vector4 tint = { 1, 1, 1, 1 };
    DirectX::SimpleMath::Vector2 mainTiling = { 1, 1 };
    DirectX::SimpleMath::Vector2 mainScroll = { 0, 0 };
    DirectX::SimpleMath::Vector2 noiseTiling = { 1, 1 };
    DirectX::SimpleMath::Vector2 noiseScroll = { 0, 0 };
    float distortion = 0.0f;
    float intensity = 1.0f;
    float dissolveThreshold = -1.0f;   // 負 = 溶解無し
    float dissolveEdge = 0.05f;
    DirectX::SimpleMath::Vector4 dissolveEdgeColor = { 1, 0.5f, 0.1f, 1 };
    uint32_t hasNoise = 0;
    uint32_t hasMask = 0;
    float _pad[2] = {};
};
static_assert(sizeof(VFXMeshParams) == 96, "VFXMeshCB layout mismatch");

// 1 回の描画
struct VFXMeshDrawItem
{
    Model* model = nullptr;
    DirectX::SimpleMath::Matrix world;
    Texture* mainTex = nullptr;    // null なら白
    Texture* noiseTex = nullptr;
    Texture* maskTex = nullptr;
    VFXMeshParams params;
    int  blend = 0;                // 0 additive / 1 alpha
    bool twoSided = true;
};

// ============================================================
// VFXMeshRenderer
// Mesh entry が毎フレーム Submit した物を、粒子の前にまとめて描く。
// 光は当てない。深度は読むだけ。
//   alpha    : 遠 → 近に並べて先に描く
//   additive : 順不同、後に描く
// ============================================================
class VFXMeshRenderer
{
public:
    bool Initialize(ID3D11Device* device);
    void Submit(const VFXMeshDrawItem& item) { m_Items.push_back(item); }
    void Render(ID3D11DeviceContext* ctx, CameraBase* camera);
    void Clear() { m_Items.clear(); }
    size_t GetItemCount() const { return m_Items.size(); }

private:
    void Draw(ID3D11DeviceContext* ctx, const VFXMeshDrawItem& item,
        const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);

    std::vector<VFXMeshDrawItem> m_Items;

    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    std::shared_ptr<Texture> m_White;   // 貼图無しの既定

    ComPtr<ID3D11BlendState> m_BlendAdditive;
    ComPtr<ID3D11BlendState> m_BlendAlpha;
    ComPtr<ID3D11DepthStencilState> m_DepthReadOnly;
    ComPtr<ID3D11RasterizerState> m_RasterCullBack;
    ComPtr<ID3D11RasterizerState> m_RasterCullNone;
};