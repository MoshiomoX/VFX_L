// ============================================================
// SwarmSystemCloseTarget.cpp
// 「本当に一番近い敵」（2026-10-27、ユーザー「突風・氷のトゲのような近距離の範囲は自分の周りの一番近い敵、遠距離は Boss を狙う」）。
//   杖の狙い（SwarmCounters の NEAREST_KEY）は 10-26 から Boss / エリートを優先するので、すぐ横の雑魚より 20m 先の Boss を向く。
//   近距離の範囲魔法のために、同じ競争を優先度なしの距離でもう 1 本やる：
//   ・SwarmEnemyMoveCS（u4）が key = 距離 | スロットを 32B の raw buffer の先頭へ InterlockedMin
//   ・SwarmAimResolveCS（u1）が key から位置と距離を +4 / +16 に書き、key を空に戻す（次の固定ステップ用）
//   ・3 枚の staging を輪転してリードバック（2〜3 フレーム遅れ。GetCloseEnemy）
//   SwarmCounters は 64B で満杯（CLAUDE.md の「既定で変えない」構造体）なので別の buffer にした
// ============================================================
#include "Swarm/SwarmSystem.h"

bool SwarmSystem::CreateCloseTargetReadback(ID3D11Device* device)
{
    // +0 key（最初は空 = 0xFFFFFFFF）、+4 位置、+16 距離（1e30 = 居ない）
    uint32_t init[8] = { 0xFFFFFFFFu, 0, 0, 0, 0, 0, 0, 0 };
    const float none = 1.0e30f;
    std::memcpy(&init[4], &none, sizeof(none));
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(init);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    D3D11_SUBRESOURCE_DATA sd0 = {};
    sd0.pSysMem = init;
    if (FAILED(device->CreateBuffer(&bd, &sd0, &m_CloseTargetBuffer))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ud.Buffer.NumElements = 8;
    if (FAILED(device->CreateUnorderedAccessView(m_CloseTargetBuffer.Get(), &ud, &m_CloseTargetUAV))) return false;

    D3D11_BUFFER_DESC sd = {};
    sd.ByteWidth = sizeof(init);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < kCloseTargetStaging; ++i)
    {
        if (FAILED(device->CreateBuffer(&sd, nullptr, &m_CloseTargetStaging[i]))) return false;
        m_CloseTargetStagingFilled[i] = false;
    }
    m_CloseTargetStagingWrite = 0;
    m_CloseTargetDist = 1.0e30f;
    return true;
}

// Flush の終わり（カウンタの写しと同じ頃）に staging へ
void SwarmSystem::CopyCloseTargetForCPU()
{
    if (!m_CloseTargetBuffer || !m_CloseTargetStaging[m_CloseTargetStagingWrite]) return;
    m_Context->CopyResource(m_CloseTargetStaging[m_CloseTargetStagingWrite].Get(), m_CloseTargetBuffer.Get());
    m_CloseTargetStagingFilled[m_CloseTargetStagingWrite] = true;
    m_CloseTargetStagingWrite = (m_CloseTargetStagingWrite + 1) % kCloseTargetStaging;
}

void SwarmSystem::ReadCloseTarget()
{
    const int readIndex = m_CloseTargetStagingWrite;   // 次に書く = 一番古い
    if (!m_CloseTargetStagingFilled[readIndex] || !m_CloseTargetStaging[readIndex]) return;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_CloseTargetStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    const float* f = static_cast<const float*>(mapped.pData);
    m_CloseTargetPos = { f[1], f[2], f[3] };
    m_CloseTargetDist = f[4];
    m_Context->Unmap(m_CloseTargetStaging[readIndex].Get(), 0);
}

bool SwarmSystem::GetCloseEnemy(DirectX::SimpleMath::Vector3& pos, float& dist) const
{
    if (!(m_CloseTargetDist < 1.0e29f)) return false;
    pos = m_CloseTargetPos;
    dist = m_CloseTargetDist;
    return true;
}
