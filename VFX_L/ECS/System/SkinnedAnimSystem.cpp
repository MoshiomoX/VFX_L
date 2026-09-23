// ============================================================
// SkinnedAnimSystem.cpp
// ============================================================
#include "ECS/System/SkinnedAnimSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/SkinnedAnimComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include <algorithm>
#include <cmath>

using DirectX::SimpleMath::Matrix;

namespace
{
    // 1 層分の時計を進める
    void TickLayer(SkinnedAnimLayer& L, const SkinnedModel& model, float dt)
    {
        // ---- 本クリップ ----
        if (L.clip >= 0)
        {
            const float dur = model.GetClipDurationSec(L.clip);
            L.time += dt * L.speed;
            if (dur > 0.0f)
            {
                if (L.loop)
                {
                    L.time = std::fmod(L.time, dur);
                }
                else if (L.time >= dur)
                {
                    // 末尾で止める。fmod で折り返すと最終ポーズが 0 フレーム目に戻るので
                    // ぎりぎり手前に置く
                    L.time = dur - 1e-4f;
                    L.finished = true;
                }
            }
        }

        // ---- クロスフェード元 ----
        if (L.prevClip >= 0)
        {
            L.prevTime += dt * L.speed;
            const float dur = model.GetClipDurationSec(L.prevClip);
            if (dur > 0.0f) L.prevTime = std::fmod(L.prevTime, dur);

            L.fade += (L.fadeDuration > 0.0f) ? dt / L.fadeDuration : 1.0f;
            if (L.fade >= 1.0f)
            {
                L.fade = 1.0f;
                L.prevClip = -1;
            }
        }

        // ---- 層 weight の追従 ----
        if (L.weight != L.targetWeight)
        {
            const float step = (L.fadeDuration > 0.0f) ? dt / L.fadeDuration : 1.0f;
            if (L.weight < L.targetWeight) L.weight = (std::min)(L.weight + step, L.targetWeight);
            else                           L.weight = (std::max)(L.weight - step, L.targetWeight);
        }
    }

    // 1 層のポーズ（前クリップとのクロスフェード込み）
    void SampleLayer(const SkinnedAnimLayer& L, const SkinnedModel& model,
        std::vector<BoneLocal>& out, std::vector<BoneLocal>& scratch)
    {
        model.SampleLocal(L.clip, L.time, out);
        if (L.prevClip >= 0 && L.fade < 1.0f)
        {
            // prev → cur を fade で混ぜる = prev をベースに cur を fade で乗せる
            model.SampleLocal(L.prevClip, L.prevTime, scratch);
            SkinnedModel::BlendLocals(scratch, out, L.fade);
            out.swap(scratch);
        }
    }
}

void SkinnedAnimSystem::Update(Registry& reg, float dt)
{
    reg.CreateView<SkinnedAnimComponent>()
        .Each([&](Entity, SkinnedAnimComponent& a)
            {
                if (!a.model) return;
                TickLayer(a.base, *a.model, dt);
                TickLayer(a.upper, *a.model, dt);
                TickLayer(a.over, *a.model, dt);
            });
}

bool SkinnedAnimSystem::BuildPose(const SkinnedAnimComponent& a, std::vector<Matrix>& outGlobal)
{
    if (!a.model) return false;
    const SkinnedModel& model = *a.model;

    std::vector<BoneLocal> pose, layer, scratch;

    // base（クリップ無しなら bind）
    SampleLayer(a.base, model, pose, scratch);

    // upper: マスクの骨だけ weight で差し替え
    if (a.upper.clip >= 0 && a.upper.weight > 0.0f && !a.upperMask.empty())
    {
        SampleLayer(a.upper, model, layer, scratch);
        SkinnedModel::BlendLocals(pose, layer, a.upper.weight, &a.upperMask);
    }

    // over: 全身を weight で上書き
    if (a.over.clip >= 0 && a.over.weight > 0.0f)
    {
        SampleLayer(a.over, model, layer, scratch);
        SkinnedModel::BlendLocals(pose, layer, a.over.weight);
    }

    model.BuildGlobals(pose, outGlobal);
    return true;
}
