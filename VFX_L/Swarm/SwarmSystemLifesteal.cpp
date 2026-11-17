// ============================================================
// SwarmSystemLifesteal.cpp
// 吸血のルーンの GPU → CPU：吸血の印が付いた敵（最後に当てたのが吸血の弾 / 範囲）が失った HP の累計。
//   ・SwarmDamageTextCS が毎フレーム、印の付いた敵の HP の減りを 16B の raw buffer の先頭へ InterlockedAdd（永久に累積）
//   ・誘発の環と同じく 3 枚の staging を輪転してリードバック、CPU は前回の値との差分を取る（読み損ねても取りこぼさない）
//   ・回復の量（割合・1 秒あたりの上限）は CPU（Player/LifestealHeal）
// ============================================================
#include "Swarm/SwarmSystem.h"

bool SwarmSystem::CreateLifestealReadback(ID3D11Device* device)
{
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = 16;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_LifestealBuffer))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ud.Buffer.NumElements = 4;
    if (FAILED(device->CreateUnorderedAccessView(m_LifestealBuffer.Get(), &ud, &m_LifestealUAV))) return false;

    if (!m_LifestealStaging.Create(device, 16)) return false;
    m_LifestealLast = 0;
    m_LifestealPending = 0.0f;
    m_LifestealSeen = false;
    return true;
}

// 範囲・誘発の環と同じ所（Flush の終わり）で staging へ
void SwarmSystem::CopyLifestealForCPU()
{
    m_LifestealStaging.Copy(m_Context, m_LifestealBuffer.Get());
}

void SwarmSystem::ReadLifesteal()
{
    uint32_t total = 0;
    bool stale = false;
    if (!m_LifestealStaging.TryRead(m_Context, [&](const void* data, bool st)
        {
            total = *static_cast<const uint32_t*>(data);
            stale = st;
        }))
        return;
    // KillAll の前の写しは前回値だけ進める（消した敵の分を後から回復させない）
    if (m_LifestealSeen && !stale)
        m_LifestealPending += Swarm::HpFromFixed(total - m_LifestealLast);   // 符号無しの差（溢れて一周しても正しい）
    m_LifestealLast = total;
    m_LifestealSeen = true;
}

float SwarmSystem::ConsumeLifesteal()
{
    const float d = m_LifestealPending;
    m_LifestealPending = 0.0f;
    return d;
}
