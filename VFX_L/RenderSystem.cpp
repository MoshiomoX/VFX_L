// ============================================================
// RenderSystem.cpp
// ============================================================
#include "RenderSystem.h"
#include "TransformComponent.h"
#include "ModelComponent.h"
#include "Transform.h"       // 既存の描画用 Transform（一時利用）
#include "Model.h"
#include "View.h"

void RenderSystem::Render(Registry& reg, Renderer& renderer)
{
    reg.CreateView<TransformComponent, ModelComponent>()
        .Each([&renderer](Entity e, TransformComponent& tf, ModelComponent& mc)
            {
                if (!mc.visible || !mc.model) return;

                // ECS の TransformComponent を既存 Transform に詰めて Model::Draw へ渡す。
                // これで描画管線を変えずに ECS 描画が可能になる。
                Transform temp;
                temp.SetPosition(tf.position);
                temp.SetRotation(tf.rotation);
                temp.SetScale(tf.scale);

                mc.model->Draw(renderer, &temp);
            });
}