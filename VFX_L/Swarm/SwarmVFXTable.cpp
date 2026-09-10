// ============================================================
// SwarmVFXTable.cpp
// ============================================================
#include "Swarm/SwarmVFXTable.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXParticleEntry.h"
#include "Particle/GPUParticleSystem.h"
#include "Manager/ResourceManager.h"
#include <iostream>

using Microsoft::WRL::ComPtr;

bool SwarmVFXTable::UploadImmutable(ID3D11Device* device, const void* data,
    UINT stride, UINT count,
    ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv, const char* name)
{
    srv.Reset();
    buf.Reset();

    // 空の表でも1要素は確保する（SRV が null だと CS 側で読めない）
    static const uint8_t dummy[64] = {};
    const UINT n = (count > 0) ? count : 1;
    const void* src = (count > 0) ? data : dummy;

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = stride * n;
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = stride;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = src;

    if (FAILED(device->CreateBuffer(&bd, &init, &buf)))
    {
        std::cout << "[Error] SwarmVFXTable: " << name << " buffer failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = n;
    if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
    {
        std::cout << "[Error] SwarmVFXTable: " << name << " SRV failed" << std::endl;
        return false;
    }
    return true;
}

// ============================================================
// 表の構築
// ============================================================
bool SwarmVFXTable::Build(ID3D11Device* device, GPUParticleSystem* particles)
{
    std::vector<Swarm::VFXRecipe>     recipes;
    std::vector<GPUEmitter>           emitters;
    std::vector<ColorKey>             keys;
    std::vector<Swarm::VFXModelEntry> models;
    std::vector<Swarm::VFXLightEntry> lights;

    m_Index.clear();
    m_Warnings = 0;

    // index 0 は「何も無い」配方。vfxType が引けない時の逃げ先
    recipes.push_back({});
    m_Index.push_back({ VFXId::None, 0 });

    for (int i = 0; i < VFXDatabase::Count(); ++i)
    {
        const VFXId id = VFXDatabase::At(i);
        const char* path = VFXDatabase::GetPath(id);
        if (!path) continue;

        auto tmpl = ResourceManager::Get().LoadVFXTemplate(path);
        if (!tmpl)
        {
            std::cout << "[SwarmVFX] load failed: " << path << std::endl;
            continue;
        }

        // 最初の Particle entry を探す
        VFXParticleEntry* pe = nullptr;
        for (int k = 0; ; ++k)
        {
            VFXEntry* e = tmpl->GetEntry(k);
            if (!e) break;
            if (e->GetType() == EntryType::Particle)
            {
                pe = static_cast<VFXParticleEntry*>(e);
                break;
            }
        }
        if (!pe) continue;

        Swarm::VFXRecipe r;
        r.particleStart = (uint32_t)emitters.size();
        r.modelStart = (uint32_t)models.size();
        r.lightStart = (uint32_t)lights.size();

        // ---- GPU 互換チェック ----
        // 一弾一スレッド・無状態なので時間軸は表現できない
        bool timelineIgnored = (pe->startTime != 0.0f || pe->duration >= 0.0f);

        GPUEmitter ge = pe->emitterData.ToGPU();
        ge.isActive = 1.0f;
        ge.ownerID = 0;
        ge.colorKeyOffset = (int)keys.size();   // 静的区は offset 0 起点
        ge.colorKeyCount = pe->emitterData.colorKeyCount;
        for (int c = 0; c < pe->emitterData.colorKeyCount; ++c)
            keys.push_back(pe->emitterData.colorKeys[c]);

        emitters.push_back(ge);
        r.particleCount = 1;

        if (timelineIgnored)
        {
            ++m_Warnings;
            tmpl->SetGPUTimelineIgnored(true);
            std::cout << "[SwarmVFX] warning: " << path
                << " has timed entries; GPU path ignores start/duration" << std::endl;
        }

        m_Index.push_back({ id, (uint32_t)recipes.size() });
        recipes.push_back(r);
    }

    m_EmitterCount = (int)emitters.size();

    // ---- GPU へ ----
    if (!UploadImmutable(device, recipes.data(), sizeof(Swarm::VFXRecipe),
        (UINT)recipes.size(), m_RecipeBuffer, m_RecipeSRV, "recipe")) return false;
    if (!UploadImmutable(device, emitters.data(), sizeof(GPUEmitter),
        (UINT)emitters.size(), m_EmitterBuffer, m_EmitterSRV, "emitter")) return false;
    if (!UploadImmutable(device, models.data(), sizeof(Swarm::VFXModelEntry),
        (UINT)models.size(), m_ModelBuffer, m_ModelSRV, "model")) return false;
    if (!UploadImmutable(device, lights.data(), sizeof(Swarm::VFXLightEntry),
        (UINT)lights.size(), m_LightBuffer, m_LightSRV, "light")) return false;

    if (particles)
        particles->RegisterStaticColorKeys(keys);

    std::cout << "[SwarmVFX] " << recipes.size() - 1 << " recipes, "
        << emitters.size() << " emitters, " << keys.size() << " color keys, "
        << m_Warnings << " warnings" << std::endl;
    return true;
}

uint32_t SwarmVFXTable::IndexOf(VFXId id) const
{
    for (const auto& p : m_Index)
        if (p.first == id) return p.second;
    return 0;
}