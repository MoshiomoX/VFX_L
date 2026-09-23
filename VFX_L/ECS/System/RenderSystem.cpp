// ============================================================
// RenderSystem.cpp
// ============================================================
#include "ECS/System/RenderSystem.h"
#include "ECS/System/SkinnedAnimSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Component/DissolveComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Graphics/Transform.h"       // 既存の描画用 Transform（一時利用）
#include "Graphics/Model/Model.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/SkinnedModelGPU.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Camera/CameraBase.h"
#include "Manager/ResourceManager.h"
#include "ECS/View.h"

using DirectX::SimpleMath::Matrix;
using DirectX::SimpleMath::Vector3;

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

    RenderSkinned(reg, renderer);
}

// ============================================================
// 骨付き: 層を混ぜたポーズ → SkinningCS → 描画
// 蒙皮結果は Entity 毎の SkinnedModelGPU に入る（複数体でも干渉しない）
// ============================================================
void RenderSystem::RenderSkinned(Registry& reg, Renderer& renderer)
{
    CameraBase* cam = renderer.GetCamera();
    ID3D11DeviceContext* ctx = renderer.GetContext();
    if (!cam || !ctx) return;

    if (!m_SkinningCS)
        m_SkinningCS = ResourceManager::Get().LoadCS(L"SkinningCS", L"Shader/Skinning/SkinningCS.hlsl");
    if (!m_SkinningCS) return;

    const Matrix view = cam->GetViewMatrix();
    const Matrix proj = cam->GetProjectionMatrix();
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cam->GetPosition();

    bool drewAny = false;
    reg.CreateView<TransformComponent, SkinnedAnimComponent>()
        .Each([&](Entity, TransformComponent& tf, SkinnedAnimComponent& a)
            {
                if (!a.visible || !a.model || !a.gpu) return;

                std::vector<Matrix> globals, palette;
                if (!SkinnedAnimSystem::BuildPose(a, globals)) return;

                const int subCount = (int)a.gpu->GetSubMeshes().size();
                for (int s = 0; s < subCount; ++s)
                {
                    if (!a.gpu->IsSubMeshVisible(s)) continue;
                    a.model->BuildSubmeshPalette(s, globals, palette);
                    a.gpu->SkinSubmesh(ctx, m_SkinningCS.get(), s, palette);
                }

                // モデル → Entity: 拡縮 → offset → Entity の回転 → 位置
                // （Transform.cpp と同じ Yaw/Pitch/Roll の規約）
                const Matrix world =
                    Matrix::CreateScale(a.scale)
                    * Matrix::CreateTranslation(a.offset)
                    * Matrix::CreateFromYawPitchRoll(
                        DirectX::XMConvertToRadians(tf.rotation.y + a.yawOffsetDeg),
                        DirectX::XMConvertToRadians(tf.rotation.x),
                        DirectX::XMConvertToRadians(tf.rotation.z))
                    * Matrix::CreateTranslation(tf.position);

                a.gpu->Render(ctx, *a.model, light, world, view, proj);
                drewAny = true;
            });

    // SkinnedModelGPU::Render は InputLayout / VB を外すので、後続の通常描画のために戻す
    if (drewAny)
        RenderStates::Get().Restore(ctx);
}
