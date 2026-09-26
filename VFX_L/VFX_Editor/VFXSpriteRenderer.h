// ============================================================
// VFXSpriteRenderer.h
// Sprite entry が毎フレーム Submit した連番絵の 1 コマを、粒子の前にまとめて描く。
//   1 枚 = SpriteQuad（HLSL の SpriteQuad.hlsli と同じ並び）を dynamic な
//   structured buffer に詰め、同じ貼图ごとに DrawInstanced。板の向きは VS が決める。
//   混合は乗算済み alpha の 1 種類（加算の物は PS が alpha 0 を書く）。深度は読むだけ。
//   並べ替えはしない（重なった半透明の前後は崩れることがある）
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class Texture;
class CameraBase;
class VertexShader;
class PixelShader;

// HLSL: SpriteQuad（80 bytes）
struct VFXSpriteQuad
{
    DirectX::SimpleMath::Vector3 position;   // 基準点の世界座標
    float    rotation = 0.0f;                // ラジアン（板の面内）
    DirectX::SimpleMath::Vector2 size;       // 幅・高さ（m）
    DirectX::SimpleMath::Vector2 pivot = { 0.5f, 0.5f };   // コマ内の基準点（0..1、左上原点）
    DirectX::SimpleMath::Vector4 uvRect;     // コマの左上 uv と大きさ
    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };
    uint32_t facing = 0;                     // 0 カメラを向く / 1 立てる（Y 軸だけ回る）/ 2 地面に寝かせる
    uint32_t flags = 0;                      // bit0 = 加算
    float    _pad[2] = {};
};
static_assert(sizeof(VFXSpriteQuad) == 80, "SpriteQuad layout mismatch");

// VFXSpriteVS / SwarmSpriteVS の b0（112 bytes）。カメラの向きは揺れ込みのビュー行列から
struct VFXSpriteCameraCB
{
    DirectX::SimpleMath::Matrix  viewProj;
    DirectX::SimpleMath::Vector3 camRight;
    uint32_t first = 0;          // VFXSpriteVS：この組の最初の 1 枚（SV_InstanceID は描画ごとに 0 から）
    DirectX::SimpleMath::Vector3 camUp;
    float    _pad0 = 0.0f;
    DirectX::SimpleMath::Vector3 camForward;
    float    _pad1 = 0.0f;

    static VFXSpriteCameraCB From(CameraBase* camera);
};
static_assert(sizeof(VFXSpriteCameraCB) == 112, "SpriteCB layout mismatch");

struct VFXSpriteDrawItem
{
    Texture* texture = nullptr;
    bool point = true;        // 最近傍で読む
    VFXSpriteQuad quad;
};

class VFXSpriteRenderer
{
public:
    bool Initialize(ID3D11Device* device);
    void Submit(const VFXSpriteDrawItem& item) { if (item.texture) m_Items.push_back(item); }
    // 不透明物の後、粒子の前に呼ぶ。描いたら積んだ物は消える
    void Render(ID3D11DeviceContext* ctx, CameraBase* camera);
    size_t GetItemCount() const { return m_Items.size(); }

private:
    bool EnsureCapacity(ID3D11Device* device, size_t count);

    std::vector<VFXSpriteDrawItem> m_Items;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_QuadBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_QuadSRV;
    size_t m_Capacity = 0;
};
