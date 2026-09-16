#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

using Microsoft::WRL::ComPtr;
class ComputeShader;

// ============================================================
// Bloom（全部 ComputeShader、シェーダーは BloomCS.hlsl 1本）
//   prefilter（閾値 + 半分に）→ dual-filter 降採様 ×4 → 昇採様 ×4
// 結果は半解像度。合成 PS が linear で拾って全画面に伸ばす
// ============================================================
struct BloomParams
{
    bool  enabled = true;
    float threshold = 1.0f;   // HDR で 1.0 = 白。これを超えた分が光る
    float knee = 0.5f;        // 閾値の手前からなだらかに効かせる幅
    float intensity = 0.8f;   // 合成時の倍率
    float exposure = 1.0f;    // 全体の明るさ倍率
    bool  tonemap = false;    // ACES。1.0 超えを潰す。既定 off
    bool  gamma = true;       // pow(1/2.2)。線形 → 表示用
};

class Bloom
{
public:
    static constexpr int kMipCount = 5;

    bool Initialize(ID3D11Device* device, int width, int height);
    bool Resize(ID3D11Device* device, int width, int height);
    void Shutdown();

    // sceneSRV: resolve 済みの HDR 場面（非 MSAA）
    void Execute(ID3D11DeviceContext* context, ID3D11ShaderResourceView* sceneSRV);

    ID3D11ShaderResourceView* GetResultSRV() const;
    BloomParams& Params() { return m_Params; }

private:
    struct Mip
    {
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        UINT w = 0, h = 0;
    };

    // HLSL の BloomCB と同じ 32B
    struct CB
    {
        float dstTexelX, dstTexelY;
        float srcTexelX, srcTexelY;
        float threshold;
        float knee;
        uint32_t mode;
        float _pad;
    };
    static_assert(sizeof(CB) == 32, "BloomCB layout mismatch");

    enum Mode : uint32_t { Prefilter = 0, Downsample = 1, Upsample = 2 };

    bool CreateMips(ID3D11Device* device, int width, int height);
    void Run(ID3D11DeviceContext* context, Mode mode,
        ID3D11ShaderResourceView* src, const Mip* srcMip,
        const Mip* baseMip, const Mip& dst);

    std::vector<Mip> m_Down;   // [0] = 半解像度 … [N-1] = 一番小さい
    std::vector<Mip> m_Up;     // [0..N-2]。[N-2] は m_Down[N-1] を「小さい方」にして作る
    std::shared_ptr<ComputeShader> m_CS;
    BloomParams m_Params;
};