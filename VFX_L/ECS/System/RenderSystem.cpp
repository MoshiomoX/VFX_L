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

namespace
{
    // 骨付きのモデル → 世界（揺れ物の系と同じ行列を使うので SkinnedAnimSystem に置いた）
    Matrix SkinnedWorld(const TransformComponent& tf, const SkinnedAnimComponent& a)
    {
        return SkinnedAnimSystem::WorldMatrix(tf, a);
    }

    // 層を混ぜたポーズ → submesh 毎に SkinningCS
    bool Skin(ID3D11DeviceContext* ctx, ComputeShader* cs, SkinnedAnimComponent& a)
    {
        std::vector<Matrix> globals, palette;
        if (!SkinnedAnimSystem::BuildPose(a, globals)) return false;
        const int subCount = (int)a.gpu->GetSubMeshes().size();
        for (int s = 0; s < subCount; ++s)
        {
            if (!a.gpu->IsSubMeshVisible(s)) continue;
            a.model->BuildSubmeshPalette(s, globals, palette);
            a.gpu->SkinSubmesh(ctx, cs, s, palette);
        }
        return true;
    }
}

bool RenderSystem::EnsureSkinningCS()
{
    if (!m_SkinningCS)
        m_SkinningCS = ResourceManager::Get().LoadCS(L"SkinningCS", L"Shader/Skinning/SkinningCS.hlsl");
    return m_SkinningCS != nullptr;
}

// ============================================================
// シャドウマップへ深度だけ
// ============================================================
void RenderSystem::GatherDrawables(Registry& reg)
{
    m_Drawables.clear();
    reg.CreateView<TransformComponent, ModelComponent>()
        .EachFrom<ModelComponent>([&](Entity e, TransformComponent&, ModelComponent& mc)
            {
                if (mc.visible && mc.model && !mc.batched)   // batched は StaticPropRenderer
                    m_Drawables.push_back(e);
            });
    m_DrawablesFresh = true;
}

void RenderSystem::RenderDepth(Registry& reg, Renderer& renderer, bool skin)
{
    if (!m_DrawablesFresh) GatherDrawables(reg);   // 影の最初の段で集め、残りの段と Render で使い回す
    for (Entity e : m_Drawables)
    {
        if (!reg.IsValid(e) || !reg.Has<ModelComponent>(e) || !reg.Has<TransformComponent>(e)) continue;
        const auto& tf = reg.Get<TransformComponent>(e);
        auto& mc = reg.Get<ModelComponent>(e);
        Transform temp;
        temp.SetPosition(tf.position);
        temp.SetRotation(tf.rotation);
        temp.SetScale(tf.scale);
        mc.model->Draw(renderer, &temp);   // renderer が深度だけにする
    }

    // 深度だけの描画は混合・深度・ラスタライザの状態を触らない（影の深度バイアスを残す）
    ID3D11DeviceContext* ctx = renderer.GetContext();
    if (!ctx || !EnsureSkinningCS()) return;
    reg.CreateView<TransformComponent, SkinnedAnimComponent>()
        .EachFrom<SkinnedAnimComponent>([&](Entity, TransformComponent& tf, SkinnedAnimComponent& a)
            {
                if (!a.visible || !a.model || !a.gpu) return;
                if (skin && !Skin(ctx, m_SkinningCS.get(), a)) return;
                a.gpu->RenderDepth(ctx, *a.model, SkinnedWorld(tf, a), renderer.DepthView(), renderer.DepthProj());
            });
}

void RenderSystem::Render(Registry& reg, Renderer& renderer)
{
    if (!m_DrawablesFresh) GatherDrawables(reg);   // 影を描かないシーンはここで集める
    for (Entity e : m_Drawables)
    {
        if (!reg.IsValid(e) || !reg.Has<ModelComponent>(e) || !reg.Has<TransformComponent>(e)) continue;
        const auto& tf = reg.Get<TransformComponent>(e);
        auto& mc = reg.Get<ModelComponent>(e);

        // ECS の TransformComponent を既存 Transform に詰めて Model::Draw へ渡す。
        // これで描画パイプラインを変えずに ECS 描画が可能になる。
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
    }
    m_DrawablesFresh = false;   // 次のフレームで集め直す

    RenderSkinned(reg, renderer);
}

// ============================================================
// 骨付き: 層を混ぜたポーズ → SkinningCS → 描画
// スキニング結果は Entity 毎の SkinnedModelGPU に入る（複数体でも干渉しない）
// ============================================================
void RenderSystem::RenderSkinned(Registry& reg, Renderer& renderer)
{
    CameraBase* cam = renderer.GetCamera();
    ID3D11DeviceContext* ctx = renderer.GetContext();
    if (!cam || !ctx) return;

    if (!EnsureSkinningCS()) return;

    const Matrix view = cam->GetViewMatrix();
    const Matrix proj = cam->GetProjectionMatrix();
    LightBuffer light = renderer.GetLightData();
    light.cameraPosition = cam->GetPosition();

    bool drewAny = false;
    reg.CreateView<TransformComponent, SkinnedAnimComponent>()
        .EachFrom<SkinnedAnimComponent>([&](Entity, TransformComponent& tf, SkinnedAnimComponent& a)
            {
                if (!a.visible || !a.model || !a.gpu) return;
                if (!Skin(ctx, m_SkinningCS.get(), a)) return;
                const Matrix world = SkinnedWorld(tf, a);

                a.gpu->Render(ctx, *a.model, light, world, view, proj);
                drewAny = true;
            });

    // SkinnedModelGPU::Render は InputLayout / VB を外すので、後続の通常描画のために戻す
    if (drewAny)
        RenderStates::Get().Restore(ctx);
}
