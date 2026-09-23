// ============================================================
// RenderSystem.cpp
// ============================================================
#include "ECS/System/RenderSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Component/DissolveComponent.h"
#include "Graphics/Transform.h"       // 既存の描画用 Transform（一時利用）
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Renderer/Renderer.h"
#include "ECS/View.h"

void RenderSystem::Render(Registry& reg, Renderer& renderer)
{
    reg.CreateView<TransformComponent, ModelComponent>()
        .Each([&](Entity e, TransformComponent& tf, ModelComponent& mc)
            {
                if (!mc.visible || !mc.model) return;

                // ECS の TransformComponent を既存 Transform に詰めて Model::Draw へ渡す。
                // これで描画管線を変えずに ECS 描画が可能になる。
                Transform temp;
                temp.SetPosition(tf.position);
                temp.SetRotation(tf.rotation);
                temp.SetScale(tf.scale);

                // 溶解中の実体は PS の溶解を掛ける（閾値 = progress）
                DissolveParams dp;
                const DissolveParams* pdp = nullptr;
                if (reg.Has<DissolveComponent>(e))
                {
                    const auto& d = reg.Get<DissolveComponent>(e);
                    if (d.noise)
                    {
                        dp.noise = d.noise->GetSRV();
                        dp.tiling = d.tiling;
                        dp.scroll = d.scroll;
                        dp.threshold = d.Threshold();
                        dp.edge = d.edge;
                        dp.edgeColor = d.edgeColor;
                        pdp = &dp;
                    }
                }
                renderer.SetDissolve(pdp);
                mc.model->Draw(renderer, &temp);
                renderer.SetDissolve(nullptr);
            });
}
