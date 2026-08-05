// ============================================================
// ProjectileVFXSystem.cpp
// ============================================================
#include "ProjectileVFXSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ProjectileVFXComponent.h"
#include "VFXEffect.h"
#include "ResourceManager.h"
#include "View.h"
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
// 投射物に VFX 実例を付ける
// ============================================================
void ProjectileVFXSystem::AttachVFX(Registry& reg, Entity e, ItemID id, const VFXContext& ctx)
{
    auto tmpl = GetTemplate(id);
    if (!tmpl) return;   // 降級：特効なしで飛ぶ

    ProjectileVFXComponent comp;
    comp.effect = std::make_unique<VFXEffect>();
    comp.effect->CloneFrom(*tmpl);       // テンプレートから複製
    comp.effect->InitStateMachine(ctx);
    comp.effect->Play();

    reg.Add<ProjectileVFXComponent>(e, std::move(comp));
}

// ============================================================
// 毎フレーム：追従 + Update（emitter を積む）
// ============================================================
void ProjectileVFXSystem::Update(Registry& reg, float dt)
{
    m_ActiveCount = 0;

    reg.CreateView<TransformComponent, ProjectileVFXComponent>()
        .Each([&](Entity e, TransformComponent& tf, ProjectileVFXComponent& vfx)
            {
                if (!vfx.effect) return;

                // 投射物の現在位置を全 emitter のオフセットとして渡す
                vfx.effect->SetWorldOffset(tf.position + vfx.offset);
                vfx.effect->Update(dt);

                ++m_ActiveCount;
            });
}