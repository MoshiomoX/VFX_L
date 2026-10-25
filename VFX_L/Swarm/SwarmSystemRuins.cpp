// ============================================================
// SwarmSystemRuins.cpp
// 遺跡の敵（2026-10-25：呪いの鎧・石像ガーゴイル・死霊術師 + 骸骨・鎖の番人）の GPU 側の準備。
//   ・生きている一覧：SwarmEnemyCompactCS / SwarmEnemyKindListCS は UAV が 8 本ずつ埋まっているので、
//     SwarmEnemyKindList2CS が KindListCS の直後に作る（CompactCS は mobList に入れない）
//   ・砕け中の一覧：SwarmCorpseListCS も 8 本埋まっているので SwarmCorpseList2CS。ついでに Boss・砂嵐の術師・
//     突撃爆弾兵もここへ移した（以前はゾンビ / 赤いロボットの皮で砕けていた）
//   ・予告の帯：鎖の番人の投げとガーゴイルの急降下は突撃兵の帯（SwarmChargeLineVS）を一覧を替えて描く
// 動き（眠り・目覚め・飛行・急降下・詠唱・投げ）は SwarmEnemyAICS、当たりは SwarmContactCS、皮と飾りは Enemy/EnemyLooks.cpp、
// 鎧の復活・骸骨の湧き・鎖は CPU（Enemy/MobSpawnerRuins.cpp・Scene/CollisionTestSceneRuins.cpp）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr UINT kChargeLineSegs = 24;   // SwarmChargeLineVS の LINE_SEGS

    std::shared_ptr<ComputeShader> LoadCS(ID3D11Device* device, const wchar_t* path, const char* name)
    {
        auto s = std::make_shared<ComputeShader>();
        const HRESULT hr = ShaderPath::Load(s.get(), device, path);
        std::cout << "[SwarmSystem] " << name << ": " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
        return SUCCEEDED(hr) ? s : nullptr;
    }

    ComPtr<ID3D11Buffer> MakeLineArgs(ID3D11Device* device)
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        const uint32_t init[4] = { kChargeLineSegs * 6, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = init;
        ComPtr<ID3D11Buffer> args;
        if (FAILED(device->CreateBuffer(&desc, &sd, &args))) return nullptr;
        return args;
    }
}

bool SwarmSystem::LoadRuinsAssets(ID3D11Device* device)
{
    m_KindList2CS = LoadCS(device, L"Shader/Swarm/SwarmEnemyKindList2CS.hlsl", "EnemyKindList2CS");
    m_CorpseList2CS = LoadCS(device, L"Shader/Swarm/SwarmCorpseList2CS.hlsl", "CorpseList2CS");
    m_WardenLineArgs = MakeLineArgs(device);
    m_GargoyleLineArgs = MakeLineArgs(device);
    return m_KindList2CS && m_CorpseList2CS;
}

// SwarmEnemyKindListCS の直後（RenderOpaque の前準備）。FrameCB は KindListCS と同じ物
void SwarmSystem::DispatchKindLists2()
{
    if (!m_KindList2CS) return;
    m_KindList2CS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_KindList2CS->Bind(m_Context);
    m_KindList2CS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_KindList2CS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_KindList2CS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());          // 固定エリートの様子（2026-10-25）
    m_KindList2CS->SetSRV(m_Context, "enemyMaxHp", m_EnemyMaxHpSRV.Get());
    m_KindList2CS->SetUAV(m_Context, "armorList", m_KindListUAV[Swarm::kDrawListArmor].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "gargoyleList", m_KindListUAV[Swarm::kDrawListGargoyle].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "necroList", m_KindListUAV[Swarm::kDrawListNecro].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "skeletonList", m_KindListUAV[Swarm::kDrawListSkeleton].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "wardenList", m_KindListUAV[Swarm::kDrawListWarden].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "eliteAList", m_KindListUAV[Swarm::kDrawListEliteA].Get(), 0);
    m_KindList2CS->SetUAV(m_Context, "eliteBList", m_KindListUAV[Swarm::kDrawListEliteB].Get(), 0);
    if (m_EliteInfoUAV) m_KindList2CS->SetUAV(m_Context, "eliteInfo", m_EliteInfoUAV.Get());
    m_KindList2CS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_KindList2CS->UnbindSRVs(m_Context);
    m_KindList2CS->UnbindUAVs(m_Context);
}

// SwarmCorpseListCS の直後。CB（時計・砕け散りの秒）は同じ物
void SwarmSystem::DispatchCorpseList2(const void* corpseListCB)
{
    if (!m_CorpseList2CS) return;
    m_CorpseList2CS->WriteBuffer(m_Context, 4, const_cast<void*>(corpseListCB));
    m_CorpseList2CS->Bind(m_Context);
    m_CorpseList2CS->SetSRV(m_Context, "corpses", m_CorpseSRV.Get());
    m_CorpseList2CS->SetUAV(m_Context, "bossCorpses", m_CorpseListUAV[Swarm::kDrawListBoss].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "sandMageCorpses", m_CorpseListUAV[Swarm::kDrawListSandMage].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "dashBomberCorpses", m_CorpseListUAV[Swarm::kDrawListDashBomber].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "armorCorpses", m_CorpseListUAV[Swarm::kDrawListArmor].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "gargoyleCorpses", m_CorpseListUAV[Swarm::kDrawListGargoyle].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "necroCorpses", m_CorpseListUAV[Swarm::kDrawListNecro].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "skeletonCorpses", m_CorpseListUAV[Swarm::kDrawListSkeleton].Get(), 0);
    m_CorpseList2CS->SetUAV(m_Context, "wardenCorpses", m_CorpseListUAV[Swarm::kDrawListWarden].Get(), 0);
    m_CorpseList2CS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxCorpses + 63) / 64, 1, 1);
    m_CorpseList2CS->UnbindSRVs(m_Context);
    m_CorpseList2CS->UnbindUAVs(m_Context);
}
