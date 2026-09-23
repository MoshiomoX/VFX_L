// ============================================================
// MeshVFXSystem.cpp
// ============================================================
#include "ECS/System/MeshVFXSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Component/MeshVFXComponent.h"
#include "Component/DissolveComponent.h"
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/VFXParticleEntry.h"
#include "VFX_Editor/VFXTextureRef.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include "Graphics/Material/Texture.h"
#include "Manager/ResourceManager.h"
#include <iostream>

using namespace DirectX::SimpleMath;

void MeshVFXSystem::RegisterVFX(VFXId id, const std::string& jsonPath)
{
    auto tmpl = ResourceManager::Get().LoadVFXTemplate(jsonPath);
    if (!tmpl)
    {
        std::cout << "[MeshVFX] template load failed: " << jsonPath << std::endl;
        return;
    }
    for (auto& p : m_Templates)
        if (p.first == id) { p.second = tmpl; return; }
    m_Templates.push_back({ id, tmpl });
}

std::shared_ptr<VFXEffect> MeshVFXSystem::GetTemplate(VFXId id) const
{
    for (const auto& p : m_Templates)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// 発射源のキャッシュ（Model ごとに 1 枠）
// ============================================================
int MeshVFXSystem::AcquireSource(const std::shared_ptr<Model>& model, GPUParticleSystem* ps, int& vertexCount)
{
    vertexCount = 0;
    if (!model || !ps || model->GetSubMeshes().empty() || !model->GetSubMeshes()[0].mesh)
        return -1;

    for (const auto& s : m_Sources)
        if (s.model == model) { vertexCount = s.vertexCount; return s.sourceId; }

    const auto& mesh = model->GetSubMeshes()[0].mesh;
    SourceCache s;
    s.model = model;
    s.vertexCount = (int)mesh->GetVertexCount();
    s.sourceId = ps->RegisterEmitSource(
        mesh->GetVertexSRV(), mesh->GetVertexCount(), GPUParticleSystem::kLayoutStatic,
        mesh->GetIndexSRV(), mesh->GetIndexCount(), mesh->GetIndexBytes());
    if (s.sourceId < 0)
    {
        std::cout << "[MeshVFX] RegisterEmitSource failed" << std::endl;
        return -1;
    }
    m_Sources.push_back(s);
    vertexCount = s.vertexCount;
    return s.sourceId;
}

// ============================================================
// 効果の中の Mesh 発射 entry を宿主の源へ束ねる。
// 規約：Shape = Mesh かつ source（ファイル）未指定 = 宿主のモデルから出す
// ============================================================
void MeshVFXSystem::BindEntries(MeshVFXComponent& comp)
{
    if (!comp.effect) return;
    for (int i = 0; i < comp.effect->GetEntryCount(); ++i)
    {
        auto* entry = comp.effect->GetEntry(i);
        if (!entry || entry->GetType() != EntryType::Particle) continue;
        auto* pe = static_cast<VFXParticleEntry*>(entry);
        if (pe->emitterData.emitType != EmitType::Mesh) continue;
        if (!pe->sourceModelPath.empty()) continue;   // ファイル指定の物はそのまま

        pe->SetExternalSource(comp.sourceId, comp.vertexCount, comp.world.get());
    }
}

// ============================================================
// 付ける / 外す
// ============================================================
bool MeshVFXSystem::Attach(Registry& reg, Entity e, VFXId id, const VFXContext& ctx)
{
    if (!reg.IsValid(e) || !reg.Has<ModelComponent>(e) || !ctx.particleSystem) return false;
    auto tmpl = GetTemplate(id);
    if (!tmpl) return false;

    if (reg.Has<MeshVFXComponent>(e))
        Detach(reg, e);

    MeshVFXComponent comp;
    comp.effect = std::make_shared<VFXEffect>();
    comp.effect->CloneFrom(*tmpl);
    comp.effect->InitStateMachine(ctx);
    comp.world = std::make_shared<Matrix>(Matrix::Identity);

    auto model = reg.Get<ModelComponent>(e).model;
    comp.sourceId = AcquireSource(model, ctx.particleSystem, comp.vertexCount);
    comp.boundModel = model.get();
    if (comp.sourceId < 0) return false;

    BindEntries(comp);
    comp.effect->Play();

    reg.Add<MeshVFXComponent>(e, comp);
    return true;
}

void MeshVFXSystem::Detach(Registry& reg, Entity e)
{
    if (!reg.IsValid(e) || !reg.Has<MeshVFXComponent>(e)) return;
    auto& comp = reg.Get<MeshVFXComponent>(e);
    if (comp.effect) comp.effect->Stop();
    // 発射源はキャッシュが持ち続ける（Model ごとに 1 枠、精英同士で共有）
    reg.Remove<MeshVFXComponent>(e);
}

bool MeshVFXSystem::StartBurn(Registry& reg, Entity e, VFXId id, const VFXContext& ctx, float duration)
{
    if (!reg.IsValid(e)) return false;

    if (!m_DefaultNoise)
    {
        NoiseRecipe recipe;   // 既定 = Perlin 256 / 周期 4
        m_DefaultNoise = ResourceManager::Get().LoadNoiseTexture(recipe);
    }

    if (!reg.Has<DissolveComponent>(e))
    {
        DissolveComponent d;
        d.noise = m_DefaultNoise;
        d.duration = (duration > 0.0f) ? duration : 1.5f;
        reg.Add<DissolveComponent>(e, d);
    }
    return Attach(reg, e, id, ctx);
}

// ============================================================
// 毎フレーム
// ============================================================
void MeshVFXSystem::Update(Registry& reg, float dt, const VFXContext& ctx)
{
    m_ActiveCount = 0;
    std::vector<Entity> finished;

    // ---- 1) 溶解の進行（VFX が無い実体でも進む）----
    reg.CreateView<DissolveComponent>()
        .Each([&](Entity e, DissolveComponent& d)
            {
                if (d.IsDone()) return;
                d.progress += (d.duration > 0.0f) ? dt / d.duration : 1.0f;
                if (d.progress >= 1.0f)
                {
                    d.progress = 1.0f;
                    if (d.destroyWhenDone) finished.push_back(e);
                }
            });

    // ---- 2) VFX の追従と emitter の積み上げ ----
    reg.CreateView<TransformComponent, ModelComponent, MeshVFXComponent>()
        .Each([&](Entity e, TransformComponent& tf, ModelComponent& mc, MeshVFXComponent& comp)
            {
                if (!comp.effect || !comp.world) return;

                // RebuildVisual 等でモデルが差し替わったら束ね直す
                if (mc.model.get() != comp.boundModel)
                {
                    comp.sourceId = AcquireSource(mc.model, ctx.particleSystem, comp.vertexCount);
                    comp.boundModel = mc.model.get();
                    if (comp.sourceId < 0) return;
                    BindEntries(comp);
                }

                // Transform::UpdateWorldMatrix と同じ式
                *comp.world =
                    Matrix::CreateScale(tf.scale) *
                    Matrix::CreateFromYawPitchRoll(
                        DirectX::XMConvertToRadians(tf.rotation.y),
                        DirectX::XMConvertToRadians(tf.rotation.x),
                        DirectX::XMConvertToRadians(tf.rotation.z)) *
                    Matrix::CreateTranslation(tf.position);

                // 溶解中なら、縁モードの entry のために頂点表を依頼する
                if (ctx.particleSystem && reg.Has<DissolveComponent>(e))
                {
                    const auto& d = reg.Get<DissolveComponent>(e);
                    if (d.noise)
                    {
                        EdgeFilterParams ep;
                        ep.noiseSRV = d.noise->GetSRV();
                        ep.noiseTiling = d.tiling;
                        ep.noiseScroll = d.scroll;
                        ep.threshold = d.Threshold();
                        ep.edge = d.edge;

                        bool anyEdge = false;
                        for (int i = 0; i < comp.effect->GetEntryCount() && !anyEdge; ++i)
                        {
                            auto* entry = comp.effect->GetEntry(i);
                            if (entry && entry->GetType() == EntryType::Particle)
                            {
                                auto* pe = static_cast<VFXParticleEntry*>(entry);
                                anyEdge = (pe->emitterData.emitType == EmitType::Mesh
                                    && pe->emitterData.shape.edgeMode == 1
                                    && pe->emitterData.shape.sourceId == comp.sourceId);
                            }
                        }
                        if (anyEdge)
                            ctx.particleSystem->SetSourceEdgeParams(comp.sourceId, ep);
                    }
                }

                // emitter の枠が無いなら積まない（ProjectileVFXSystem と同じ理由）
                if (ctx.particleSystem && !ctx.particleSystem->HasEmitterSpace())
                    return;

                comp.effect->Update(dt);
                ++m_ActiveCount;
            });

    // ---- 3) 消滅し終えた実体を片付ける（Each の外で）----
    for (Entity e : finished)
    {
        if (!reg.IsValid(e)) continue;
        Detach(reg, e);
        reg.Destroy(e);
    }
}
