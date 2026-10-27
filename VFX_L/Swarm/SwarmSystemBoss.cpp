// ============================================================
// SwarmSystemBoss.cpp
// 描画リスト毎の皮と飾り（2026-10-24 に面の Boss 用に作り、10-25 に遺跡の敵の為に全リストへ一般化）。
// 面の Boss は自分の描画リスト（kDrawListBoss）に分かれ、面毎の皮（albedo）で体を描いた直後に飾りを描く。
// 遺跡の敵（呪いの鎧・ガーゴイル・死霊術師・骸骨・鎖の番人）も同じ仕組みで兜・翼・頭巾・鎖を付ける。
// 飾りの頂点は焼いた雑魚の網と同じ空間なので、体と同じ SwarmEnemyVS・同じ定数で描けば部品と一緒に動く
// （g_AnimPart = くっつける部品）。体の周りを回る飾りは g_AnimMode = 1、ガーゴイルの翼は 2（VS が回す）。
// 砕け散りの時は SwarmCorpseVS で、part に付いた飾りだけ部品と一緒に飛ぶ（回る物・翼は描かない）。
// 飾りの形は Enemy/BossLooks.cpp・Enemy/EnemyLooks.cpp が作る
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Material/Texture.h"

namespace
{
    // 飾り毎・submesh 毎の間接引数（IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance）
    std::vector<std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>> MakeDecoArgs(
        ID3D11Device* device, const std::vector<SwarmSystem::BossDecoration>& decos)
    {
        std::vector<std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>> out;
        for (const SwarmSystem::BossDecoration& d : decos)
        {
            std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> list;
            if (d.model)
                for (const auto& sub : d.model->GetSubMeshes())
                {
                    Microsoft::WRL::ComPtr<ID3D11Buffer> args;
                    if (sub.mesh)
                    {
                        D3D11_BUFFER_DESC desc = {};
                        desc.ByteWidth = sizeof(uint32_t) * 5;
                        desc.Usage = D3D11_USAGE_DEFAULT;
                        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                        const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
                        D3D11_SUBRESOURCE_DATA sd = {};
                        sd.pSysMem = init;
                        if (FAILED(device->CreateBuffer(&desc, &sd, &args))) args.Reset();
                    }
                    list.push_back(args);
                }
            out.push_back(std::move(list));
        }
        return out;
    }
}

void SwarmSystem::SetListLook(uint32_t list, std::shared_ptr<Texture> albedo, std::vector<BossDecoration> decorations, bool hideBody)
{
    if (list >= Swarm::kEnemyKinds) return;
    m_ListLookAlbedo[list] = std::move(albedo);
    m_ListDecos[list] = std::move(decorations);
    m_ListHideBody[list] = hideBody;
    m_ListDecoArgs[list].clear();
    m_ListDecoCorpseArgs[list].clear();
    if (!m_Device) return;
    if (!m_DecoWhite)
    {
        m_DecoWhite = std::make_shared<Texture>();
        m_DecoWhite->CreateSolid(m_Device, 255, 255, 255, 255);
    }
    m_ListDecoArgs[list] = MakeDecoArgs(m_Device, m_ListDecos[list]);
    m_ListDecoCorpseArgs[list] = MakeDecoArgs(m_Device, m_ListDecos[list]);
}

void SwarmSystem::CopyListDecoCounts()
{
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        for (auto& deco : m_ListDecoArgs[k])
            for (auto& args : deco)
                if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_KindListUAV[k].Get());
    // 遺跡の敵の予告の帯（鎖の番人の投げ・ガーゴイルの急降下。突撃兵の帯と同じ VS、2026-10-25）
    if (m_WardenLineArgs)
        m_Context->CopyStructureCount(m_WardenLineArgs.Get(), sizeof(uint32_t) * 1, m_KindListUAV[Swarm::kDrawListWarden].Get());
    if (m_GargoyleLineArgs)
        m_Context->CopyStructureCount(m_GargoyleLineArgs.Get(), sizeof(uint32_t) * 1, m_KindListUAV[Swarm::kDrawListGargoyle].Get());
}

void SwarmSystem::CopyCorpseDecoCounts()
{
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        for (auto& deco : m_ListDecoCorpseArgs[k])
            for (auto& args : deco)
                if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_CorpseListUAV[k].Get());
}

// RenderOpaque のそのリストの体の直後。VS の aliveList はそのリストの一覧のまま、PS の t0 だけ白に替える
void SwarmSystem::RenderListDecorations(uint32_t list)
{
    if (list >= Swarm::kEnemyKinds || m_ListDecos[list].empty() || !m_DecoWhite) return;
    const auto& decos = m_ListDecos[list];
    const auto& argsList = m_ListDecoArgs[list];
    m_EnemyPS->SetTexture(m_Context, 0, m_DecoWhite.get());
    for (size_t d = 0; d < decos.size() && d < argsList.size(); ++d)
    {
        const BossDecoration& deco = decos[d];
        if (!deco.model) continue;
        const bool orbit = (deco.part == kDecoOrbit);
        const bool flap = (deco.flap.w != 0.0f);
        m_EnemyAnim.part = orbit ? 0u : deco.part;
        m_EnemyAnim.mode = orbit ? 1u : (deco.insectLeg ? 5u : (deco.leg ? 4u : (deco.tail ? 3u : (flap ? 2u : 0u))));   // 3 = 尾 / 首、4 = 四足の脚、5 = 虫の脚（2026-10-26）
        m_EnemyAnim.orbitSpeed = deco.orbitSpeed;
        m_EnemyAnim.flap[0] = deco.flap.x;
        m_EnemyAnim.flap[1] = deco.flap.y;
        m_EnemyAnim.flap[2] = deco.flap.z;
        m_EnemyAnim.flap[3] = deco.flap.w;
        m_EnemyAnim.hunch = deco.hunch;
        m_EnemyAnim.hinge[0] = deco.hinge.x;
        m_EnemyAnim.hinge[1] = deco.hinge.y;
        m_EnemyAnim.hinge[2] = deco.hinge.z;
        m_EnemyAnim.hinge[3] = deco.hinge.w;
        m_EnemyVS->WriteBuffer(m_Context, 4, &m_EnemyAnim);
        const auto& subs = deco.model->GetSubMeshes();
        for (size_t i = 0; i < subs.size() && i < argsList[d].size(); ++i)
            if (subs[i].mesh && argsList[d][i])
                subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, argsList[d][i].Get(), 0);
    }
    m_EnemyAnim.mode = 0u;
    m_EnemyAnim.orbitSpeed = 0.0f;
    m_EnemyAnim.flap[0] = m_EnemyAnim.flap[1] = m_EnemyAnim.flap[2] = m_EnemyAnim.flap[3] = 0.0f;
    m_EnemyAnim.hunch = 0.0f;
    m_EnemyAnim.hinge[0] = m_EnemyAnim.hinge[1] = m_EnemyAnim.hinge[2] = m_EnemyAnim.hinge[3] = 0.0f;
}

// RenderCorpses のそのリストの砕け散りの直後。SwarmCorpseVS は bind されたまま（corpseList もそのリスト）、
// CorpseCB.part を飾りの部品にして描く = 兜は頭と一緒に、盾は腕と一緒に飛ぶ
void SwarmSystem::RenderCorpseDecorations(uint32_t list, CorpseCB& ccb)
{
    if (list >= Swarm::kEnemyKinds || m_ListDecos[list].empty() || !m_DecoWhite || !m_CorpseVS) return;
    const auto& decos = m_ListDecos[list];
    const auto& argsList = m_ListDecoCorpseArgs[list];
    m_EnemyPS->SetTexture(m_Context, 0, m_DecoWhite.get());
    for (size_t d = 0; d < decos.size() && d < argsList.size(); ++d)
    {
        const BossDecoration& deco = decos[d];
        if (!deco.model || deco.part == kDecoOrbit || deco.flap.w != 0.0f || deco.tail || deco.leg || deco.insectLeg) continue;
        ccb.part = deco.part;
        m_CorpseVS->WriteBuffer(m_Context, 4, &ccb);
        const auto& subs = deco.model->GetSubMeshes();
        for (size_t i = 0; i < subs.size() && i < argsList[d].size(); ++i)
            if (subs[i].mesh && argsList[d][i])
                subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, argsList[d][i].Get(), 0);
    }
}
