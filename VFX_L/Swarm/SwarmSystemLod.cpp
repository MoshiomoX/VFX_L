// ============================================================
// SwarmSystemLod.cpp
// 遠くの雑魚の LOD（2026-11-02、晩の作業：作品資料に「部品アニメ → 遠くは 1 回の描画」の前後比較を載せる）。
//   ・普通の雑魚（描画リスト kDrawListMob）だけが対象。一番数が多いので
//   ・SwarmLodSplitCS：一覧（mobList）をカメラからの距離で近い方 / 遠い方の 2 本へ
//   ・近い方：従来どおり部品（submesh）毎に 1 回ずつ、部品アニメの表を引いて描く
//   ・遠い方：起動時に部品を 1 つの網に合わせておき、1 回で描く。g_AnimEnabled = 0 = 部品アニメを読まず、
//     VS の従来の揺れ（上下の弾みと左右の傾き）で歩く。焼いた歩きの姿勢のまま
//   HP バー・足元の影・砕け散りは今まで通り（一覧は別）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/ShaderPath.h"

#include <Windows.h>

using Microsoft::WRL::ComPtr;

namespace
{
    struct LodCB
    {
        DirectX::SimpleMath::Vector3 camPos;
        float dist2;
    };
    static_assert(sizeof(LodCB) == 16, "LodCB");

    // GPU のバッファを 1 回だけ読み戻す（起動時、部品の網を合わせる用）
    bool ReadBack(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11ShaderResourceView* srv,
        std::vector<uint8_t>& out)
    {
        if (!srv) return false;
        ComPtr<ID3D11Resource> res;
        srv->GetResource(&res);
        ComPtr<ID3D11Buffer> buf;
        if (FAILED(res.As(&buf))) return false;
        D3D11_BUFFER_DESC bd = {};
        buf->GetDesc(&bd);
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bd.ByteWidth;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        if (FAILED(device->CreateBuffer(&sd, nullptr, &staging))) return false;
        context->CopyResource(staging.Get(), buf.Get());
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
        out.assign(static_cast<const uint8_t*>(m.pData), static_cast<const uint8_t*>(m.pData) + bd.ByteWidth);
        context->Unmap(staging.Get(), 0);
        return true;
    }

    // 追加型の一覧（雑魚の一覧と同じ作り）
    bool MakeAppendList(ID3D11Device* device, ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11UnorderedAccessView>& uav,
        ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(uint32_t) * Swarm::kMaxEnemies;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(uint32_t);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = Swarm::kMaxEnemies;
        ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
        if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = Swarm::kMaxEnemies;
        return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sv, &srv));
    }
}

bool SwarmSystem::CreateLod(ID3D11Device* device)
{
    // TEMP-TEST: VFXL_SWARM_LOD=<m> で距離、=off で無効（比べる用）
    char env[32] = {};
    if (GetEnvironmentVariableA("VFXL_SWARM_LOD", env, sizeof(env)) > 0)
    {
        if (std::strcmp(env, "off") == 0) lod.enabled = false;
        else lod.distance = (float)std::atof(env);
    }
    if (!m_EnemyModel || m_EnemyDrawArgs[Swarm::kDrawListMob].empty()) return false;

    // ---- 部品の網を 1 つへ（頂点は焼いた時点で姿勢・拡縮・足元合わせ済み。並べて index をずらすだけ）----
    std::vector<VERTEX_3D> verts;
    std::vector<unsigned int> indices;
    uint32_t parts = 0;
    for (const auto& sub : m_EnemyModel->GetSubMeshes())
    {
        if (!sub.mesh) continue;
        std::vector<uint8_t> vb, ib;
        if (!ReadBack(device, m_Context, sub.mesh->GetVertexSRV(), vb)
            || !ReadBack(device, m_Context, sub.mesh->GetIndexSRV(), ib)) return false;
        const uint32_t base = (uint32_t)verts.size();
        const size_t nv = vb.size() / sizeof(VERTEX_3D);
        const size_t ni = ib.size() / sizeof(unsigned int);
        const auto* v = reinterpret_cast<const VERTEX_3D*>(vb.data());
        const auto* idx = reinterpret_cast<const unsigned int*>(ib.data());
        verts.insert(verts.end(), v, v + nv);
        for (size_t i = 0; i < ni; ++i) indices.push_back(base + idx[i]);
        ++parts;
    }
    if (verts.empty() || indices.empty()) return false;
    auto mesh = std::make_shared<Mesh>();
    if (!mesh->Create(device, verts, indices, false)) return false;

    m_LodSplitCS = std::make_shared<ComputeShader>();
    if (FAILED(ShaderPath::Load(m_LodSplitCS.get(), device, L"Shader/Swarm/SwarmLodSplitCS.hlsl")))
    {
        m_LodSplitCS = nullptr;
        return false;
    }
    if (!MakeAppendList(device, m_LodNearBuffer, m_LodNearUAV, m_LodNearSRV)
        || !MakeAppendList(device, m_LodFarBuffer, m_LodFarUAV, m_LodFarSRV)) return false;

    // 一覧の長さ（CopyStructureCount の先。CS が ByteAddressBuffer で読む）
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_LodCountBuffer))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_R32_TYPELESS;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        sv.BufferEx.NumElements = 4;
        sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        if (FAILED(device->CreateShaderResourceView(m_LodCountBuffer.Get(), &sv, &m_LodCountSRV))) return false;
    }
    // 遠い方の間接引数 { IndexCount, InstanceCount = 遠い方の一覧の長さ, 0, 0, 0 }
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(uint32_t) * 5;
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        const uint32_t init[5] = { (uint32_t)indices.size(), 0, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = init;
        if (FAILED(device->CreateBuffer(&bd, &sd, &m_LodFarArgs))) return false;
    }

    m_LodMesh = mesh;
    m_LodIndexCount = (uint32_t)indices.size();
    m_LodPartDraws = parts;
    std::cout << "[SwarmSystem] LOD: " << parts << " parts -> 1 mesh, " << verts.size() << " vertices, "
        << indices.size() / 3 << " triangles, distance " << lod.distance << (lod.enabled ? "" : " (off)") << std::endl;
    return true;
}

bool SwarmSystem::DispatchLodSplit(const DirectX::SimpleMath::Vector3& cameraPos)
{
    if (!m_LodMesh || !m_LodSplitCS || !lod.enabled) return false;

    m_Context->CopyStructureCount(m_LodCountBuffer.Get(), 0, m_KindListUAV[Swarm::kDrawListMob].Get());
    LodCB cb = { cameraPos, lod.distance * lod.distance };
    m_LodSplitCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_LodSplitCS->WriteBuffer(m_Context, 3, &cb);
    m_LodSplitCS->Bind(m_Context);
    m_LodSplitCS->SetSRV(m_Context, "mobList", m_KindListSRV[Swarm::kDrawListMob].Get());
    m_LodSplitCS->SetSRV(m_Context, "listCount", m_LodCountSRV.Get());
    m_LodSplitCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_LodSplitCS->SetUAV(m_Context, "nearList", m_LodNearUAV.Get(), 0);
    m_LodSplitCS->SetUAV(m_Context, "farList", m_LodFarUAV.Get(), 0);
    m_LodSplitCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_LodSplitCS->UnbindSRVs(m_Context);
    m_LodSplitCS->UnbindUAVs(m_Context);
    return true;
}

void SwarmSystem::CopyLodCounts()
{
    for (auto& args : m_EnemyDrawArgs[Swarm::kDrawListMob])
        if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_LodNearUAV.Get());
    m_Context->CopyStructureCount(m_LodFarArgs.Get(), sizeof(uint32_t) * 1, m_LodFarUAV.Get());
}

// RenderOpaque の雑魚（近い方）を描いた直後。VS・PS・テクスチャ・ステンシルはそのまま
void SwarmSystem::RenderLodFar()
{
    m_EnemyVS->SetSRV(m_Context, "aliveList", m_LodFarSRV.Get());
    const uint32_t prevEnabled = m_EnemyAnim.enabled;
    m_EnemyAnim.enabled = 0;   // 部品アニメの表を読まない（合わせた網に部品の区別は無い）
    m_EnemyAnim.part = 0;
    m_EnemyVS->WriteBuffer(m_Context, 4, &m_EnemyAnim);
    m_LodMesh->DrawIndexedInstancedIndirect(m_Context, m_LodFarArgs.Get(), 0);
    m_EnemyAnim.enabled = prevEnabled;
}
