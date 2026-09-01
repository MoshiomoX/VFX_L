// ============================================================
// ProjectileVFXSystem.cpp
// ============================================================
#include "ECS/System/ProjectileVFXSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/Projectile/ProjectileVFXComponent.h"
#include "VFX_Editor/VFXEffect.h"
#include "Particle/GPUParticleSystem.h"
#include "Manager/ResourceManager.h"
#include "ECS/View.h"
#include <iostream>

void ProjectileVFXSystem::RegisterVFX(ItemID id, const std::string& jsonPath)
{
    auto tmpl = ResourceManager::Get().LoadVFXTemplate(jsonPath);
    if (!tmpl)
    {
        std::cout << "[ProjectileVFX] template load failed: " << jsonPath << std::endl;
        return;
    }

    for (auto& p : m_Templates)
    {
        if (p.first == id) { p.second = tmpl; return; }
    }
    m_Templates.push_back({ id, tmpl });
}

std::shared_ptr<VFXEffect> ProjectileVFXSystem::GetTemplate(ItemID id) const
{
    for (const auto& p : m_Templates)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// ???? VFX ??????
// ============================================================
void ProjectileVFXSystem::AttachVFX(Registry& reg, Entity e, ItemID id, const VFXContext& ctx)
{
    auto tmpl = GetTemplate(id);
    if (!tmpl) return;   // ??:???????

    ProjectileVFXComponent comp;
    comp.effect = std::make_unique<VFXEffect>();
    comp.effect->CloneFrom(*tmpl);       // ??????????
    comp.effect->InitStateMachine(ctx);
    comp.effect->Play();

    reg.Add<ProjectileVFXComponent>(e, std::move(comp));
}

// ============================================================
// ?????:?? + Update(emitter ???)
// ============================================================
void ProjectileVFXSystem::Update(Registry& reg, float dt, const VFXContext& ctx)
{
    m_ActiveCount = 0;
    m_SkippedCount = 0;

    reg.CreateView<TransformComponent, ProjectileVFXComponent>()
        .Each([&](Entity e, TransformComponent& tf, ProjectileVFXComponent& vfx)
            {
                if (!vfx.effect) return;

                // ???????(????????????????)
                vfx.effect->SetWorldOffset(tf.position + vfx.offset);

                // ============================================
                // emitter ????? Update ??????
                //
                // ??? CollectAndDispatch ? emitter ????????
                // SubmitEmitters ??????????????????
                // ?????????????????????
                // ??:??? 4000 ? Dropped 2976 = ??? 3/4 ????
                //
                // ????:???????????
                //   timeInState ??????? Finishing ??????
                //   ???????????? VFX ????????
                //   ???? entity ??????????????????
                //   ??? VFXEffect ?????????????????
                // ============================================
                if (!ctx.particleSystem->HasEmitterSpace())
                {
                    ++m_SkippedCount;
                    return;
                }

                vfx.effect->Update(dt);
                ++m_ActiveCount;
            });
}