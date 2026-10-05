// ============================================================
// GPUParticleMesh.cpp
// GPUParticleSystem のうち「メッシュ粒子」（renderMode の下位 8bit != 0）の部分。
//
//   モデル表 : 0 番 = 組み込みの立方体、1.. = RegisterParticleMesh で登録したファイルのモデル。
//            包囲ボックスで「最長辺 = 1、中心 = 原点」に揃えて描く（粒子の size = 最長辺の m）
//   束     : 番号 × { 光を受ける, 発光 }。UpdateCS が束ごとに aliveMesh の区画へ振り分け、
//            meshCounts[束] に数を積む
//   描画   : 束の数を各 submesh の args（5 uint）の InstanceCount へ GPU 上で写し、
//            束 × submesh ごとに DrawIndexedInstancedIndirect。
//            光を受ける束 = PS.hlsl（Lambert、不透明・深度書き込み）
//            発光の束     = ParticleMeshGlowPS（加算・深度は読むだけ）
// ============================================================
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Manager/ResourceManager.h"
#include <algorithm>
#include <iostream>

using namespace DirectX::SimpleMath;

namespace
{
    // ParticleMeshVS の MeshDrawCB（b3）
    struct MeshDrawCB
    {
        uint32_t bucketBase;
        uint32_t instanceCap;
        float    invExtent;
        float    pad0;
        Vector3  center;
        float    pad1;
    };
    static_assert(sizeof(MeshDrawCB) == 32, "MeshDrawCB layout mismatch");

    // Shader/Common/Dissolve.hlsli の DissolveCB（b1）。PS.hlsl が読むので必ず書く（threshold < 0 = 溶解なし）
    struct DissolveOffCB
    {
        float tiling[2] = { 1, 1 };
        float scroll[2] = { 0, 0 };
        float threshold = -1.0f;
        float edge = 0.0f;
        float pad[2] = { 0, 0 };
        float edgeColor[4] = { 0, 0, 0, 0 };
    };
    static_assert(sizeof(DissolveOffCB) == 48, "DissolveCB layout mismatch");

    // 参照が 0 になってから別のモデルに使うまでの秒数（生き残った粒子が消えるのを待つ）
    constexpr float kMeshReuseDelay = 8.0f;

    // DrawIndexedInstancedIndirect の args 1 本（[0] = index 数。[1] は毎フレーム GPU が写す）
    ComPtr<ID3D11Buffer> MakeArgs(ID3D11Device* device, UINT indexCount)
    {
        const UINT init[5] = { indexCount, 0, 0, 0, 0 };
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(init);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = init;
        ComPtr<ID3D11Buffer> buf;
        if (FAILED(device->CreateBuffer(&desc, &data, &buf))) return nullptr;
        return buf;
    }
}

// ============================================
// 資源：aliveMesh（束 × 上限）+ meshCounts（束の数）+ 0 番の立方体
// ============================================
bool GPUParticleSystem::CreateMeshResources(ID3D11Device* device)
{
    m_WhiteTexture = std::make_shared<Texture>();
    m_WhiteTexture->CreateSolid(device, 255, 255, 255, 255);

    // ---- aliveMesh（区画 = 束 × kParticleMeshBucketCap）----
    {
        const UINT count = kParticleMeshBuckets * kParticleMeshBucketCap;
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * count;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        desc.StructureByteStride = sizeof(uint32_t);
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_AliveMeshBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_UNKNOWN;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = count;
        if (FAILED(device->CreateUnorderedAccessView(m_AliveMeshBuffer.Get(), &ud, &m_AliveMeshUAV))) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = count;
        if (FAILED(device->CreateShaderResourceView(m_AliveMeshBuffer.Get(), &sd, &m_AliveMeshSRV))) return false;
    }

    // ---- meshCounts（束ごとの数。UpdateCS が InterlockedAdd、描画前に args へ写す）----
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * kParticleMeshBuckets;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        if (FAILED(device->CreateBuffer(&desc, nullptr, &m_MeshCountBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_R32_UINT;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.NumElements = kParticleMeshBuckets;
        if (FAILED(device->CreateUnorderedAccessView(m_MeshCountBuffer.Get(), &ud, &m_MeshCountUAV))) return false;
    }

    // ---- 0 番：組み込みの立方体（辺 1。旧 renderMode = 1 の見た目のまま）----
    auto cube = PrimitiveBuilder::CreateBox(device, { 0.5f, 0.5f, 0.5f }, { 1, 1, 1, 1 });
    if (!BuildMeshSlot(0, cube, ""))
    {
        std::cout << "[Error] mesh particle: cube creation failed" << std::endl;
        return false;
    }

    std::cout << "[OK] Mesh particle resources created (" << kParticleMeshSlots << " meshes x 2 passes, "
        << kParticleMeshBucketCap << " per bucket)" << std::endl;
    return true;
}

// ============================================
// 番号 1 つ分を組む：大きさの揃え方と、submesh ごとの args
// ============================================
bool GPUParticleSystem::BuildMeshSlot(int slot, std::shared_ptr<Model> model, const std::string& path)
{
    if (slot < 0 || slot >= (int)kParticleMeshSlots) return false;
    if (!model || model->GetSubMeshes().empty() || !m_Device) return false;

    MeshSlot s;
    s.used = true;
    s.path = path;
    s.model = model;

    // 包囲ボックスの最長辺を 1、中心を原点へ（ファイルの単位や置き位置に左右されない）
    const Vector3 mn = model->GetBoundsMin();
    const Vector3 mx = model->GetBoundsMax();
    const Vector3 ext = mx - mn;
    const float longest = (std::max)({ ext.x, ext.y, ext.z });
    s.center = (mn + mx) * 0.5f;
    s.invExtent = (longest > 1.0e-6f) ? 1.0f / longest : 1.0f;

    for (int pass = 0; pass < 2; ++pass)
    {
        for (const auto& sm : model->GetSubMeshes())
        {
            auto args = MakeArgs(m_Device, sm.mesh ? sm.mesh->GetIndexCount() : 0);
            if (!args) return false;
            s.args[pass].push_back(args);
        }
    }

    m_MeshSlots[slot] = std::move(s);
    return true;
}

// ============================================
// モデルの登録 / 解除
// ============================================
int GPUParticleSystem::RegisterParticleMesh(const std::string& path)
{
    if (path.empty()) return 0;

    // 同じ path はそのまま（参照 0 で空きを待っている物も拾い直せる）
    for (int i = 1; i < (int)kParticleMeshSlots; ++i)
    {
        MeshSlot& s = m_MeshSlots[i];
        if (s.used && s.path == path)
        {
            ++s.refs;
            return i;
        }
    }

    // 空き：未使用 → 参照 0 で猶予を過ぎた物
    const float now = m_CachedGlobalCB.totalTime;
    int slot = -1;
    for (int i = 1; i < (int)kParticleMeshSlots && slot < 0; ++i)
        if (!m_MeshSlots[i].used) slot = i;
    for (int i = 1; i < (int)kParticleMeshSlots && slot < 0; ++i)
        if (m_MeshSlots[i].refs <= 0 && now >= m_MeshSlots[i].freeAt) slot = i;
    if (slot < 0)
    {
        std::cout << "[Warn] mesh particle: no free mesh slot for " << path << " (drawn as cube)" << std::endl;
        return 0;
    }

    auto model = ResourceManager::Get().LoadModel(path);
    if (!model || !BuildMeshSlot(slot, model, path))
    {
        std::cout << "[Warn] mesh particle: cannot load " << path << " (drawn as cube)" << std::endl;
        m_MeshSlots[slot] = MeshSlot{};
        return 0;
    }

    m_MeshSlots[slot].refs = 1;
    std::cout << "[MeshParticle] slot " << slot << " = " << path << std::endl;
    return slot;
}

void GPUParticleSystem::ReleaseParticleMesh(int slot)
{
    if (slot <= 0 || slot >= (int)kParticleMeshSlots) return;   // 0 番（立方体）は数えない
    MeshSlot& s = m_MeshSlots[slot];
    if (!s.used || s.refs <= 0) return;
    if (--s.refs == 0)
        s.freeAt = m_CachedGlobalCB.totalTime + kMeshReuseDelay;
}

int GPUParticleSystem::GetParticleMeshCount() const
{
    int n = 0;
    for (const auto& s : m_MeshSlots)
        if (s.used) ++n;
    return n;
}

// ============================================
// 描画（ビルボード・帯の後）
// ============================================
void GPUParticleSystem::RenderMeshes(ID3D11DeviceContext* context, int firstPass, int lastPass)
{
    if (!m_Camera || !m_MeshVS || !m_MeshVS->IsValid()) return;
    if (!m_MeshLitPS || !m_MeshLitPS->IsValid() || !m_MeshGlowPS || !m_MeshGlowPS->IsValid()) return;

    // ---- 束の数を、その束を描く submesh の args の InstanceCount（+4 byte）へ写す ----
    for (int i = 0; i < (int)kParticleMeshSlots; ++i)
    {
        const MeshSlot& s = m_MeshSlots[i];
        if (!s.used) continue;
        for (int pass = 0; pass < 2; ++pass)
        {
            const UINT bucket = (UINT)i * 2u + (UINT)pass;
            const D3D11_BOX box = { bucket * 4u, 0, 0, bucket * 4u + 4u, 1, 1 };
            for (const auto& args : s.args[pass])
                context->CopySubresourceRegion(args.Get(), 0, 4, 0, 0, m_MeshCountBuffer.Get(), 0, &box);
        }
    }

    // ModelCommon.hlsli の MVPBuffer と同じ並び（row_major なので転置しない）。World は VS が粒子ごとに組む
    struct { Matrix W, V, P; } mvp{ Matrix::Identity,
        m_Camera->GetViewMatrix(), m_Camera->GetProjectionMatrix() };
    static_assert(sizeof(mvp) == 192, "MVPBuffer layout mismatch");

    LightBuffer light = m_Light;
    light.cameraPosition = m_Camera->GetPosition();
    DissolveOffCB dissolve;

    m_MeshVS->Bind(context);
    m_MeshVS->WriteBuffer(context, 2, &mvp);
    m_MeshVS->SetSRV(context, "particles", m_ParticleSRV.Get());
    m_MeshVS->SetSRV(context, "aliveMesh", m_AliveMeshSRV.Get());

    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();

    // pass 0 = 光を受ける（不透明・深度書き込み）/ 1 = 発光（加算・深度は読むだけ）
    for (int pass = (std::max)(firstPass, 0); pass <= (std::min)(lastPass, 1); ++pass)
    {
        PixelShader* ps = (pass == 0) ? m_MeshLitPS.get() : m_MeshGlowPS.get();
        if (pass == 0) RenderStates::Get().ApplyOpaque(context);
        else           RenderStates::Get().ApplyAdditiveBillboard(context);

        ps->Bind(context);
        if (pass == 0)
        {
            ps->WriteBuffer(context, 0, &light);
            ps->WriteBuffer(context, 1, &dissolve);
        }
        context->PSSetSamplers(0, 1, &samp);

        for (int i = 0; i < (int)kParticleMeshSlots; ++i)
        {
            const MeshSlot& s = m_MeshSlots[i];
            if (!s.used || !s.model) continue;

            MeshDrawCB cb = {};
            cb.bucketBase = ((UINT)i * 2u + (UINT)pass) * kParticleMeshBucketCap;
            cb.instanceCap = kParticleMeshBucketCap;
            cb.invExtent = s.invExtent;
            cb.center = s.center;
            m_MeshVS->WriteBuffer(context, 3, &cb);

            const auto& subs = s.model->GetSubMeshes();
            for (size_t k = 0; k < subs.size() && k < s.args[pass].size(); ++k)
            {
                if (!subs[k].mesh) continue;
                Material* mat = s.model->GetMaterial(subs[k].materialIndex);
                Texture* tex = mat ? mat->GetAlbedoTexture() : nullptr;
                ps->SetTexture(context, 0, tex ? tex : m_WhiteTexture.get());
                subs[k].mesh->DrawIndexedInstancedIndirect(context, s.args[pass][k].Get(), 0);
            }
        }
        ps->UnbindSRVs(context);
    }

    RenderStates::Get().Restore(context);
    m_MeshVS->UnbindSRVs(context);
}
