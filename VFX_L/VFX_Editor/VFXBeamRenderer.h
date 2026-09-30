// ============================================================
// VFXBeamRenderer.h
// Beam entry が毎フレーム Submit した光線を、粒子の前にまとめて描く（2026-09-30）。
//   1 本 = 起点 → 終点の帯。VS が SV_VertexID から帯を組み立て（32 分割、カメラへ向ける）、
//   1 本につき 3 層（辉光 / 主色 / 白芯）を instance で描く。PS は幅方向の断面 + 長さ方向に
//   流れる値ノイズで「流れる光」を作る。貼图は使わない。
//   乗算済み alpha の混合で alpha 0 を書く = 加算（粒子の加算と同じ見え方）。深度は読むだけ。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class CameraBase;
class VertexShader;
class PixelShader;

// HLSL: BeamItem（96 bytes）
struct VFXBeamItem
{
    DirectX::SimpleMath::Vector3 start;
    float width = 0.8f;                     // 主色の全幅（m）
    DirectX::SimpleMath::Vector3 end;
    float coreRatio = 0.35f;                // 白芯の幅（主色比）
    DirectX::SimpleMath::Vector4 color = { 0.3f, 0.8f, 1.0f, 1.0f };       // 主色（HDR 可）
    DirectX::SimpleMath::Vector4 coreColor = { 1.0f, 1.0f, 1.0f, 1.0f };   // 白芯
    float glowRatio = 2.2f;                 // 辉光の幅（主色比）
    float glowAlpha = 0.35f;                // 辉光の濃さ
    float scroll = 0.0f;                    // ノイズの流れの位相（m）
    float noiseScale = 1.5f;                // ノイズの細かさ（1/m）
    float noiseStrength = 0.35f;            // 幅の揺れ・明暗の強さ（0〜1）
    float tipFade = 1.0f;                   // 先端を薄くする長さ（m）
    float rootFade = 0.3f;                  // 根元を薄くする長さ（m）
    float seed = 0.0f;                      // 本ごとのノイズのずれ
};
static_assert(sizeof(VFXBeamItem) == 96, "BeamItem layout mismatch");

class VFXBeamRenderer
{
public:
    bool Initialize(ID3D11Device* device);
    void Submit(const VFXBeamItem& item) { m_Items.push_back(item); }
    // 不透明物の後、粒子の前に呼ぶ。描いたら積んだ物は消える
    void Render(ID3D11DeviceContext* ctx, CameraBase* camera);
    size_t GetItemCount() const { return m_Items.size(); }

private:
    struct BeamCB
    {
        DirectX::SimpleMath::Matrix  viewProj;
        DirectX::SimpleMath::Vector3 camPos;
        uint32_t first = 0;
        float time = 0.0f;
        float _pad[3] = {};
    };
    static_assert(sizeof(BeamCB) == 96, "BeamCB layout mismatch");

    bool EnsureCapacity(ID3D11Device* device, size_t count);

    std::vector<VFXBeamItem> m_Items;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ItemBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ItemSRV;
    size_t m_Capacity = 0;
};
