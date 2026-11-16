// ============================================================
// SceneDepthRead.h
// 描画中にシーンの深度を SRV で読むための組（ソフトパーティクル・シールドの交わりの光）。
// シーンが毎フレーム Set する。Begin で今の RT はそのまま、DSV だけ読むだけの物に替え、
// End で元の RT / DSV に戻す。深度は MSAA（Texture2DMS）の時だけ読む
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>

class SceneDepthRead
{
public:
    void Set(ID3D11ShaderResourceView* depth, ID3D11DepthStencilView* readOnlyDsv)
    {
        m_Depth = depth;
        m_ReadOnlyDsv = readOnlyDsv;
    }
    void Clear() { m_Depth.Reset(); m_ReadOnlyDsv.Reset(); }
    bool Valid() const { return m_Depth && m_ReadOnlyDsv; }
    ID3D11ShaderResourceView* Depth() const { return m_Depth.Get(); }

    // 深度 → 目からの距離：eye = B / (d - A)、A = _33 × _34、B = _43
    static DirectX::SimpleMath::Vector2 EyeDepthParams(const DirectX::SimpleMath::Matrix& proj)
    {
        return { proj._33 * proj._34, proj._43 };
    }

    // 読むだけの DSV に替え、PS の slot に深度を張る。Valid でなければ何もしない（false）
    bool Begin(ID3D11DeviceContext* ctx, UINT slot)
    {
        if (!Valid() || m_Active) return false;
        ctx->OMGetRenderTargets(1, m_PrevRtv.ReleaseAndGetAddressOf(), m_PrevDsv.ReleaseAndGetAddressOf());
        ID3D11RenderTargetView* rtv = m_PrevRtv.Get();
        ctx->OMSetRenderTargets(1, &rtv, m_ReadOnlyDsv.Get());
        ID3D11ShaderResourceView* srv = m_Depth.Get();
        ctx->PSSetShaderResources(slot, 1, &srv);
        m_Slot = slot;
        m_Active = true;
        return true;
    }
    void End(ID3D11DeviceContext* ctx)
    {
        if (!m_Active) return;
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(m_Slot, 1, &none);
        ID3D11RenderTargetView* rtv = m_PrevRtv.Get();
        ctx->OMSetRenderTargets(1, &rtv, m_PrevDsv.Get());
        m_PrevRtv.Reset();
        m_PrevDsv.Reset();
        m_Active = false;
    }

    // 深度を書きながら読む描画用：今のシーンの深度を自前のテクスチャへ写して PS の slot に張る（RT / DSV はそのまま）。
    // EndCopy で外す。写し先は大きさ・サンプル数が変わった時だけ作り直す
    bool BeginCopy(ID3D11DeviceContext* ctx, UINT slot)
    {
        if (!m_Depth || m_CopyActive) return false;
        Microsoft::WRL::ComPtr<ID3D11Resource> res;
        m_Depth->GetResource(res.GetAddressOf());
        Microsoft::WRL::ComPtr<ID3D11Texture2D> src;
        if (!res || FAILED(res.As(&src))) return false;
        D3D11_TEXTURE2D_DESC d = {};
        src->GetDesc(&d);
        if (!m_Copy || m_CopyW != d.Width || m_CopyH != d.Height || m_CopySamples != d.SampleDesc.Count)
        {
            m_Copy.Reset();
            m_CopySrv.Reset();
            Microsoft::WRL::ComPtr<ID3D11Device> dev;
            ctx->GetDevice(dev.GetAddressOf());
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            m_Depth->GetDesc(&sd);
            if (FAILED(dev->CreateTexture2D(&d, nullptr, m_Copy.GetAddressOf())) ||
                FAILED(dev->CreateShaderResourceView(m_Copy.Get(), &sd, m_CopySrv.GetAddressOf())))
            {
                m_Copy.Reset();
                m_CopySrv.Reset();
                return false;
            }
            m_CopyW = d.Width;
            m_CopyH = d.Height;
            m_CopySamples = d.SampleDesc.Count;
        }
        ctx->CopyResource(m_Copy.Get(), src.Get());
        ID3D11ShaderResourceView* srv = m_CopySrv.Get();
        ctx->PSSetShaderResources(slot, 1, &srv);
        m_CopySlot = slot;
        m_CopyActive = true;
        return true;
    }
    void EndCopy(ID3D11DeviceContext* ctx)
    {
        if (!m_CopyActive) return;
        ID3D11ShaderResourceView* none = nullptr;
        ctx->PSSetShaderResources(m_CopySlot, 1, &none);
        m_CopyActive = false;
    }

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Copy;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CopySrv;
    UINT m_CopyW = 0, m_CopyH = 0, m_CopySamples = 0, m_CopySlot = 0;
    bool m_CopyActive = false;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_Depth;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_ReadOnlyDsv;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_PrevRtv;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_PrevDsv;
    UINT m_Slot = 0;
    bool m_Active = false;
};
