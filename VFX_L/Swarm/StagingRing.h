// ============================================================
// StagingRing.h
// GPU の buffer を CPU へ待たずに読み戻す、staging 3 枚の輪転。
//
//   第 N   フレーム: Copy で GPU の buffer を staging[N % 3] へ写す（GPU 内の操作なので CPU は待たない）
//   第 N+1 フレーム: TryRead が次に写す 1 枚（= 一番古い写し）を DO_NOT_WAIT で Map する
//                    → 2 フレーム前の値が取れる。まだなら即失敗で戻り、前回の値のまま
//
// ブロッキングの Map（D3D11_MAP_READ だけ）は使わない。GPU の完了を待つのでパイプラインが切れる。
// stale = KillAll の前に写した 1 枚の印（MarkStale）。読めた時に読み手へ渡し、読んだら消す。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

class StagingRing
{
public:
    static constexpr int kCount = 3;

    // bytes の staging を 3 枚作る。写し済み・stale の印と書き込み位置も戻す
    bool Create(ID3D11Device* device, UINT bytes)
    {
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bytes;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kCount; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_Staging[i]))) return false;
            m_Filled[i] = false;
            m_Stale[i] = false;
        }
        m_Write = 0;
        return true;
    }

    void Release()
    {
        for (auto& s : m_Staging) s.Reset();
    }

    // src を次の 1 枚へ写す（待たない）。写した 1 枚は stale でなくなる
    void Copy(ID3D11DeviceContext* ctx, ID3D11Resource* src)
    {
        if (!src || !m_Staging[m_Write]) return;
        ctx->CopyResource(m_Staging[m_Write].Get(), src);
        m_Filled[m_Write] = true;
        m_Stale[m_Write] = false;
        m_Write = (m_Write + 1) % kCount;
    }

    // 一番古い写しを待たずに読む。読めたら read(const void* data, bool stale) を呼んで true。
    // consume = true なら読んだ 1 枚を未写しに戻す（次に写すまで同じ物を 2 回読まない）
    template <class F>
    bool TryRead(ID3D11DeviceContext* ctx, F&& read, bool consume = false)
    {
        const int i = m_Write;   // 次に写す = 一番古い
        if (!m_Filled[i] || !m_Staging[i]) return false;

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        const HRESULT hr = ctx->Map(m_Staging[i].Get(), 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return false;   // まだ。次のフレームで
        read(static_cast<const void*>(mapped.pData), m_Stale[i]);
        ctx->Unmap(m_Staging[i].Get(), 0);
        m_Stale[i] = false;
        if (consume) m_Filled[i] = false;
        return true;
    }

    // 写し済みの 1 枚を全部 stale にする（KillAll の前の出来事を捨てる用）
    void MarkStale()
    {
        for (int i = 0; i < kCount; ++i) m_Stale[i] = m_Filled[i];
    }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Staging[kCount];
    bool m_Filled[kCount] = {};   // その 1 枚に写し済みか
    bool m_Stale[kCount] = {};    // KillAll の前の写しか
    int  m_Write = 0;             // 次に写す 1 枚
};
