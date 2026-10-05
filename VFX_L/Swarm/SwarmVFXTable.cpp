// ============================================================
// SwarmVFXTable.cpp
// ============================================================
#include "Swarm/SwarmVFXTable.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXParticleEntry.h"
#include "VFX_Editor/VFXPointLightEntry.h"
#include "VFX_Editor/VFXSpriteEntry.h"
#include "VFX_Editor/VFXLiquidEntry.h"
#include "VFX_Editor/SpriteSheets.h"
#include "Particle/GPUParticleSystem.h"
#include "Manager/ResourceManager.h"
#include <DirectXTex.h>
#include <algorithm>
#include <cstring>
#include <iostream>

using Microsoft::WRL::ComPtr;

bool SwarmVFXTable::UploadImmutable(ID3D11Device* device, const void* data,
    UINT stride, UINT count,
    ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv, const char* name)
{
    srv.Reset();
    buf.Reset();

    // 空の表でも1要素は確保する（SRV が null だと CS 側で読めない）
    // stride は最大で GPUEmitter の 336 bytes。1 要素分を読んでも溢れない大きさにしておく
    static const uint8_t dummy[1024] = {};
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
    std::vector<Swarm::VFXSpriteDef>  sprites;
    std::vector<const SpriteSheets::Info*> spriteSheetOf;   // sprites と同じ並び（cellUV を後で決める）
    std::vector<const SpriteSheets::Info*> slices;          // テクスチャ配列の 1 枚ずつ
    std::vector<VFXLiquidDef>         liquids;
    std::vector<uint32_t>             recipeLiquid;          // recipes と同じ並び。def の番号 + 1、0 = 無し

    m_Index.clear();
    m_Warnings = 0;

    // index 0 は「何も無い」レシピ。vfxType が引けない時の逃げ先
    recipes.push_back({});
    recipeLiquid.push_back(0u);
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

        // 全部の Particle entry（重ねて 1 発の見た目にする）と、全部の Light entry を拾う
        std::vector<VFXParticleEntry*> particleEntries;
        std::vector<const VFXPointLightEntry*> lightEntries;
        std::vector<const VFXSpriteEntry*> spriteEntries;
        const VFXLiquidEntry* liquidEntry = nullptr;   // 1 つの範囲に液溜まりは 1 つ（2 つ目以降は無視）
        for (int k = 0; ; ++k)
        {
            VFXEntry* e = tmpl->GetEntry(k);
            if (!e) break;
            if (e->GetType() == EntryType::Particle)
                particleEntries.push_back(static_cast<VFXParticleEntry*>(e));
            else if (e->GetType() == EntryType::Light)
                lightEntries.push_back(static_cast<const VFXPointLightEntry*>(e));
            else if (e->GetType() == EntryType::Sprite)
                spriteEntries.push_back(static_cast<const VFXSpriteEntry*>(e));
            else if (e->GetType() == EntryType::Liquid)
            {
                if (!liquidEntry) liquidEntry = static_cast<const VFXLiquidEntry*>(e);
                else std::cout << "[SwarmVFX] warning: " << path << " has more than one Liquid entry; the GPU uses the first" << std::endl;
            }
        }
        if (particleEntries.empty() && spriteEntries.empty() && !liquidEntry) continue;

        Swarm::VFXRecipe r;
        r.particleStart = (uint32_t)emitters.size();
        r.modelStart = (uint32_t)models.size();
        r.lightStart = (uint32_t)lights.size();

        // ---- 光源表（時間軸は無いので intensityStart だけ）----
        for (const auto* le : lightEntries)
        {
            Swarm::VFXLightEntry l;
            l.color[0] = le->color.x; l.color[1] = le->color.y; l.color[2] = le->color.z; l.color[3] = 1.0f;
            l.radius = le->radius;
            l.intensity = le->intensityStart;
            lights.push_back(l);
        }
        r.lightCount = (uint32_t)lightEntries.size();

        // ---- GPU 互換チェック ----
        // 一弾一スレッド・無状態なので時間軸は表現できない（全層が同時に出続ける）
        bool timelineIgnored = false;

        // ---- 発射器：entry の順に並べる。位置は弾からのずらし（SwarmEmitCS が足す）----
        for (VFXParticleEntry* pe : particleEntries)
        {
            if (pe->startTime != 0.0f || pe->duration >= 0.0f)
                timelineIgnored = true;

            GPUEmitter ge = pe->emitterData.ToGPU();
            // メッシュ粒子のモデルはここで登録する（表はシーンの間ずっと使うので解除しない）。
            // particles が無ければ ToGPU のまま（0 番 = 立方体）
            if (pe->emitterData.renderMode != 0 && particles)
                ge.renderMode = ParticleRenderMode::Pack(
                    particles->RegisterParticleMesh(pe->emitterData.meshPath),
                    pe->emitterData.meshGlow, pe->emitterData.meshFaceVelocity,
                    pe->emitterData.meshForwardAxis)
                    | (pe->emitterData.inheritVelocity ? ParticleRenderMode::kInheritSourceVelocity : 0);
            ge.isActive = 1.0f;
            ge.ownerID = 0;
            ge.colorKeyOffset = (int)keys.size();   // 静的区は offset 0 起点
            ge.colorKeyCount = pe->emitterData.colorKeyCount;
            for (int c = 0; c < pe->emitterData.colorKeyCount; ++c)
                keys.push_back(pe->emitterData.colorKeys[c]);

            emitters.push_back(ge);
        }
        r.particleCount = (uint32_t)particleEntries.size();

        // ---- Sprite entry（GPU の範囲だけが描く。弾の上では描かない）----
        // 範囲が生まれた瞬間に 1 回再生を始める（SwarmSpriteCS）。startTime は使わない
        r.spriteStart = (uint32_t)sprites.size();
        for (const VFXSpriteEntry* se : spriteEntries)
        {
            const SpriteSheets::Info* sheet = se->GetSheet();
            if (!sheet || !sheet->texture) continue;
            if (se->startTime != 0.0f) timelineIgnored = true;

            auto it = std::find(slices.begin(), slices.end(), sheet);
            if (it == slices.end())
            {
                if (slices.size() >= 255) continue;   // flags の 8bit に入る分まで
                slices.push_back(sheet);
                it = slices.end() - 1;
            }
            const uint32_t slice = (uint32_t)(it - slices.begin());

            const bool loop = se->IsLooping();
            const float frameTime = se->FrameTime();
            const float once = frameTime * (float)sheet->frameCount;
            const auto size = se->WorldSize();
            const auto pivot = se->Pivot();

            Swarm::VFXSpriteDef d;
            d.offset[0] = se->offset.x; d.offset[1] = se->offset.y; d.offset[2] = se->offset.z;
            d.height = size.y;
            d.color[0] = se->color.x; d.color[1] = se->color.y; d.color[2] = se->color.z; d.color[3] = se->color.w;
            d.pivot[0] = pivot.x; d.pivot[1] = pivot.y;
            d.cols = (uint32_t)sheet->cols;
            d.frameCount = (uint32_t)sheet->frameCount;
            d.frameTime = frameTime;
            // 繰り返す物は entry の長さだけ（無ければ 1 周）。範囲が消えても再生は続く
            d.life = (loop && se->duration > 0.0f) ? se->duration : once;
            d.facing = (uint32_t)std::clamp(se->facing, 0, 2);
            d.flags = (se->blend == 1 ? 1u : 0u) | (loop ? 2u : 0u) | (slice << 8);
            d.rotation = DirectX::XMConvertToRadians(se->rotationDeg);
            d.aspect = (size.y > 0.0f) ? size.x / size.y : 1.0f;
            sprites.push_back(d);
            spriteSheetOf.push_back(sheet);
        }
        r.spriteCount = (uint32_t)sprites.size() - r.spriteStart;

        if (timelineIgnored)
        {
            ++m_Warnings;
            tmpl->SetGPUTimelineIgnored(true);
            std::cout << "[SwarmVFX] warning: " << path
                << " has timed entries; GPU path ignores start/duration" << std::endl;
        }

        // ---- Liquid entry（GPU の範囲が生きている間、その下に描く。SwarmLiquidVS）----
        // 時間軸は使わない（範囲の寿命で出て、消える前の dryTime 秒で乾く）
        uint32_t liquidIndex = 0;
        if (liquidEntry)
        {
            liquids.push_back(liquidEntry->def);
            liquidIndex = (uint32_t)liquids.size();   // + 1 済み
        }

        m_Index.push_back({ id, (uint32_t)recipes.size() });
        recipes.push_back(r);
        recipeLiquid.push_back(liquidIndex);
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

    // ---- Sprite：テクスチャ配列（全部を一番大きい物の大きさに揃える）と 1 コマの uv ----
    m_SpriteDefCount = (int)sprites.size();
    if (!BuildSpriteArray(device, slices)) return false;
    for (size_t i = 0; i < sprites.size(); ++i)
    {
        sprites[i].cellUV[0] = (float)spriteSheetOf[i]->cellW / (float)(std::max)(1, m_SpriteSliceW);
        sprites[i].cellUV[1] = (float)spriteSheetOf[i]->cellH / (float)(std::max)(1, m_SpriteSliceH);
    }
    if (!UploadImmutable(device, sprites.data(), sizeof(Swarm::VFXSpriteDef),
        (UINT)sprites.size(), m_SpriteDefBuffer, m_SpriteDefSRV, "sprite")) return false;

    // ---- Liquid ----
    m_LiquidDefCount = (int)liquids.size();
    if (!UploadImmutable(device, liquids.data(), sizeof(VFXLiquidDef),
        (UINT)liquids.size(), m_LiquidDefBuffer, m_LiquidDefSRV, "liquid")) return false;
    if (!UploadImmutable(device, recipeLiquid.data(), sizeof(uint32_t),
        (UINT)recipeLiquid.size(), m_RecipeLiquidBuffer, m_RecipeLiquidSRV, "recipeLiquid")) return false;

    if (particles)
        particles->RegisterStaticColorKeys(keys);

    std::cout << "[SwarmVFX] " << recipes.size() - 1 << " recipes, "
        << emitters.size() << " emitters, " << lights.size() << " lights, "
        << sprites.size() << " sprites (" << slices.size() << " sheets), "
        << liquids.size() << " liquids, "
        << keys.size() << " color keys, "
        << m_Warnings << " warnings" << std::endl;
    return true;
}

uint32_t SwarmVFXTable::IndexOf(VFXId id) const
{
    for (const auto& p : m_Index)
        if (p.first == id) return p.second;
    return 0;
}
// ============================================================
// Sprite のテクスチャ配列
// 全部を一番大きいテクスチャの大きさに揃え、各 1 枚の左上に詰める（余りは透明）。
// uv は SwarmVFXTable::Build が「コマの画素 / 1 枚の大きさ」で入れる。
// ピクセルアートなので mipmap は作らない（最近傍で読む）。
// 使う物が無くても 1x1 の透明を 1 枚作る（SRV を null にしない）
// ============================================================
bool SwarmVFXTable::BuildSpriteArray(ID3D11Device* device, const std::vector<const SpriteSheets::Info*>& sheets)
{
    m_SpriteArray.Reset();
    m_SpriteArraySRV.Reset();

    int w = 1, h = 1;
    for (const auto* s : sheets)
    {
        w = (std::max)(w, s->texW);
        h = (std::max)(h, s->texH);
    }
    m_SpriteSliceW = w;
    m_SpriteSliceH = h;

    const size_t count = (std::max)((size_t)1, sheets.size());
    std::vector<std::vector<uint8_t>> pixels(count, std::vector<uint8_t>((size_t)w * h * 4, 0));

    for (size_t i = 0; i < sheets.size(); ++i)
    {
        const std::wstring path(sheets[i]->path.begin(), sheets[i]->path.end());
        DirectX::ScratchImage img;
        if (FAILED(DirectX::LoadFromWICFile(path.c_str(), DirectX::WIC_FLAGS_FORCE_RGB, nullptr, img)))
        {
            std::cout << "[SwarmVFX] sprite sheet load failed: " << sheets[i]->path << std::endl;
            continue;   // その枚は透明のまま
        }
        if (img.GetMetadata().format != DXGI_FORMAT_R8G8B8A8_UNORM)
        {
            DirectX::ScratchImage conv;
            if (FAILED(DirectX::Convert(img.GetImages(), img.GetImageCount(), img.GetMetadata(),
                DXGI_FORMAT_R8G8B8A8_UNORM, DirectX::TEX_FILTER_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, conv)))
                continue;
            img = std::move(conv);
        }
        const DirectX::Image* src = img.GetImage(0, 0, 0);
        const size_t rows = (std::min)((size_t)h, src->height);
        const size_t bytes = (std::min)((size_t)w, src->width) * 4;
        for (size_t y = 0; y < rows; ++y)
            memcpy(&pixels[i][y * (size_t)w * 4], src->pixels + y * src->rowPitch, bytes);
    }

    D3D11_TEXTURE2D_DESC td = {};
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = 1;
    td.ArraySize = (UINT)count;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    std::vector<D3D11_SUBRESOURCE_DATA> init(count);
    for (size_t i = 0; i < count; ++i)
    {
        init[i].pSysMem = pixels[i].data();
        init[i].SysMemPitch = (UINT)w * 4;
    }
    if (FAILED(device->CreateTexture2D(&td, init.data(), &m_SpriteArray)))
    {
        std::cout << "[Error] SwarmVFXTable: sprite array failed" << std::endl;
        return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = td.Format;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
    sd.Texture2DArray.MipLevels = 1;
    sd.Texture2DArray.ArraySize = (UINT)count;
    if (FAILED(device->CreateShaderResourceView(m_SpriteArray.Get(), &sd, &m_SpriteArraySRV)))
    {
        std::cout << "[Error] SwarmVFXTable: sprite array SRV failed" << std::endl;
        return false;
    }
    return true;
}
