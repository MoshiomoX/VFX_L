// ============================================================
// ProjectileVFXSystem.cpp
// ============================================================
#include "ProjectileVFXSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ProjectileVFXComponent.h"
#include "VFXEffect.h"
#include "GPUParticleSystem.h"
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
void ProjectileVFXSystem::Update(Registry& reg, float dt, const VFXContext& ctx)
{
    m_ActiveCount = 0;
    m_SkippedCount = 0;

    reg.CreateView<TransformComponent, ProjectileVFXComponent>()
        .Each([&](Entity e, TransformComponent& tf, ProjectileVFXComponent& vfx)
            {
                if (!vfx.effect) return;

                // 追従は常に行う（軽い。位置がズレると復帰時に飛ぶ）
                vfx.effect->SetWorldOffset(tf.position + vfx.offset);

                // ============================================
                // emitter が満杯なら Update を打ち切る。
                //
                // 従来は CollectAndDispatch で emitter を全部作った後、
                // SubmitEmitters の中で上限超過を検出して捨てていた。
                // 捨てる分の計算を全部やってから捨てていた。
                // 計測：投射物 4000 で Dropped 2976 = 収集の 3/4 が無駄。
                //
                // ★副作用：状態機も進まなくなる。
                //   timeInState が増えないため Finishing の時間兜底が
                //   効かず、満杯が続く間その VFX は解放されない。
                //   投射物は entity ごと破棄されるので実害は出にくいが、
                //   本来は VFXEffect 側で「収集だけ止める」のが正しい。
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