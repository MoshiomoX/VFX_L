// ============================================================
// RenderStates.h
// 描画ステートの一元管理。
// D3D11 のステートオブジェクトは不変・共有可能なので、
// 全体で1組だけ作って使い回す（各モジュールで作らない）。
//
// 基本は DirectXTK の CommonStates を利用し、
// 足りないもの（Skybox 用 depth 等）と「用途別プリセット」だけ自前で持つ。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <CommonStates.h>

class RenderStates
{
public:
    static RenderStates& Get()
    {
        static RenderStates instance;
        return instance;
    }

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // ---- 個別ステート（CommonStates 経由）----
    ID3D11BlendState* Opaque()      const { return m_Common->Opaque(); }
    ID3D11BlendState* Additive()    const { return m_Common->Additive(); }
    ID3D11BlendState* AlphaBlend()  const { return m_Common->AlphaBlend(); }

    ID3D11DepthStencilState* DepthDefault()  const { return m_Common->DepthDefault(); }
    ID3D11DepthStencilState* DepthReadOnly() const { return m_Common->DepthRead(); }
    ID3D11DepthStencilState* DepthNone()     const { return m_Common->DepthNone(); }
    ID3D11DepthStencilState* DepthLessEqual() const { return m_DepthLessEqual.Get(); }  // Skybox 用

    ID3D11RasterizerState* CullBack()  const { return m_CullBack.Get(); }
    ID3D11RasterizerState* CullFront() const { return m_CullFront.Get(); }
    ID3D11RasterizerState* CullNone()  const { return m_Common->CullNone(); }
    ID3D11RasterizerState* Wireframe() const { return m_Common->Wireframe(); }

    ID3D11SamplerState* LinearWrap()  const { return m_Common->LinearWrap(); }
    ID3D11SamplerState* LinearClamp() const { return m_Common->LinearClamp(); }
    ID3D11SamplerState* PointClamp()  const { return m_Common->PointClamp(); }

    // ---- 用途別プリセット（3つのステートをまとめて設定）----
    // 不透明メッシュ：深度書き込み、背面カリング
    void ApplyOpaque(ID3D11DeviceContext* ctx) const;

    // 加算ビルボード（パーティクル・投射物の芯）：深度読むだけ、カリング無し
    void ApplyAdditiveBillboard(ID3D11DeviceContext* ctx) const;

    // 半透明（UI・フェード等）：アルファ合成、深度読むだけ
    void ApplyAlphaBlend(ID3D11DeviceContext* ctx) const;

    // Skybox：前面カリング、深度 LESS_EQUAL
    void ApplySkybox(ID3D11DeviceContext* ctx) const;

    //描画後に必ず呼ぶ。次のパスへステートを持ち越さないため
    void Restore(ID3D11DeviceContext* ctx) const;

private:
    RenderStates() = default;
    ~RenderStates() = default;
    RenderStates(const RenderStates&) = delete;
    RenderStates& operator=(const RenderStates&) = delete;

    std::unique_ptr<DirectX::CommonStates> m_Common;

    // CommonStates に無いもの
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_DepthLessEqual;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_CullBack;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState>   m_CullFront;

    bool m_Initialized = false;
};