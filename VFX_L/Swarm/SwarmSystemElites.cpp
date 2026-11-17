// ============================================================
// SwarmSystemElites.cpp
// 面の固定エリート（各面 2 体、どれも「予告を避ける」型）の GPU 側の準備。
//   ・生きている一覧：SwarmEnemyKindList2CS の u5 / u6（CompactCS は mobList に入れない）
//   ・様子のリードバック：同じ CS の u7 に EliteInfoBlock（数 + 槽・種類・HP・位置）。Boss の BossInfo と同じく 3 枚の staging を輪転
//   ・砕け中の一覧：SwarmCorpseListCS / List2CS は 8 本埋まっているので SwarmCorpseList3CS（自分の皮で砕ける）
// 技・アフィックス・予告・当たりは CPU（Enemy/EliteAttacks）。GPU への指示は KindCB::eliteCmd / eliteMove
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"

bool SwarmSystem::LoadEliteAssets(ID3D11Device* device)
{
    auto cs = std::make_shared<ComputeShader>();
    const HRESULT hr = ShaderPath::Load(cs.get(), device, L"Shader/Swarm/SwarmCorpseList3CS.hlsl");
    std::cout << "[SwarmSystem] CorpseList3CS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    m_CorpseList3CS = SUCCEEDED(hr) ? cs : nullptr;

    // 様子の置き場（raw。先頭 16B = 数）
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = sizeof(Swarm::EliteInfoBlock);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    if (FAILED(device->CreateBuffer(&bd, nullptr, &m_EliteInfoBuffer))) return false;
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ud.Buffer.NumElements = sizeof(Swarm::EliteInfoBlock) / 4;
    if (FAILED(device->CreateUnorderedAccessView(m_EliteInfoBuffer.Get(), &ud, &m_EliteInfoUAV))) return false;

    if (!m_EliteStaging.Create(device, sizeof(Swarm::EliteInfoBlock))) return false;
    m_EliteInfo = {};
    return m_CorpseList3CS != nullptr;
}

// SwarmEnemyKindList2CS の前：数を 0 に戻す（CS が InterlockedAdd で数える）
void SwarmSystem::ClearEliteInfo()
{
    if (!m_EliteInfoUAV) return;
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_EliteInfoUAV.Get(), zero);
}

// SwarmEnemyKindList2CS の後：staging へ写す（読むのは 2 フレーム後の Flush）
void SwarmSystem::CopyEliteInfoForCPU()
{
    m_EliteStaging.Copy(m_Context, m_EliteInfoBuffer.Get());
}

void SwarmSystem::ReadEliteInfo()
{
    // 読んだ 1 枚は未写しに戻す（同じ物を 2 回読まない。新しい値かどうかを通し番号で見せる）。読めなければ前回の値のまま
    const bool read = m_EliteStaging.TryRead(m_Context,
        [&](const void* data, bool) { memcpy(&m_EliteInfo, data, sizeof(Swarm::EliteInfoBlock)); }, true);
    if (!read) return;
    m_EliteInfo.count = (std::min)(m_EliteInfo.count, Swarm::kMaxEliteInfo);
    ++m_EliteInfoSerial;
}

// SwarmCorpseList2CS の直後。CB（時計・砕け散りの秒）は同じ物
void SwarmSystem::DispatchCorpseList3(const void* corpseListCB)
{
    if (!m_CorpseList3CS) return;
    m_CorpseList3CS->WriteBuffer(m_Context, 4, const_cast<void*>(corpseListCB));
    m_CorpseList3CS->Bind(m_Context);
    m_CorpseList3CS->SetSRV(m_Context, "corpses", m_CorpseSRV.Get());
    m_CorpseList3CS->SetUAV(m_Context, "eliteACorpses", m_CorpseListUAV[Swarm::kDrawListEliteA].Get(), 0);
    m_CorpseList3CS->SetUAV(m_Context, "eliteBCorpses", m_CorpseListUAV[Swarm::kDrawListEliteB].Get(), 0);
    m_CorpseList3CS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxCorpses + 63) / 64, 1, 1);
    m_CorpseList3CS->UnbindSRVs(m_Context);
    m_CorpseList3CS->UnbindUAVs(m_Context);
}
