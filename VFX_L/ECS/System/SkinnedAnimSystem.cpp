// ============================================================
// SkinnedAnimSystem.cpp
// ============================================================
#include "ECS/System/SkinnedAnimSystem.h"
#include "ECS/Registry.h"
#include "ECS/View.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/TransformComponent.h"
#include "Graphics/Model/SkinnedModel.h"

using DirectX::SimpleMath::Matrix;

namespace
{
    int s_PoseHits = 0, s_PoseMisses = 0;

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

    // 素のポーズを決める層の状態（同じなら BuildPose の結果は同じ）
    SkinnedAnimComponent::PoseKey MakePoseKey(const SkinnedAnimComponent& a)
    {
        SkinnedAnimComponent::PoseKey k{};
        size_t n = 0;
        for (const SkinnedAnimLayer* L : { &a.base, &a.upper, &a.over })
        {
            k.v[n++] = (float)L->clip;
            k.v[n++] = L->time;
            k.v[n++] = (float)L->prevClip;
            k.v[n++] = L->prevTime;
            k.v[n++] = L->fade;
            k.v[n++] = L->weight;
        }
        k.model = a.model.get();
        k.mask = a.upperMask.empty() ? nullptr : a.upperMask.data();
        k.maskSize = a.upperMask.size();
        return k;
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

Matrix SkinnedAnimSystem::WorldMatrix(const TransformComponent& tf, const SkinnedAnimComponent& a)
{
    // Transform.cpp と同じ Yaw/Pitch/Roll の規約。
    // KayKit のモデル空間の正面は -Z なので、X 軸回りの正の角度で頭が後ろへ倒れる
    return Matrix::CreateScale(a.scale)
        * Matrix::CreateRotationX(DirectX::XMConvertToRadians(a.leanDeg))
        * Matrix::CreateTranslation(a.offset)
        * Matrix::CreateFromYawPitchRoll(
            DirectX::XMConvertToRadians(tf.rotation.y + a.yawOffsetDeg),
            DirectX::XMConvertToRadians(tf.rotation.x),
            DirectX::XMConvertToRadians(tf.rotation.z))
        * Matrix::CreateTranslation(tf.position);
}

void SkinnedAnimSystem::PoseCacheStats(int& hits, int& misses)
{
    hits = s_PoseHits;
    misses = s_PoseMisses;
}

bool SkinnedAnimSystem::BuildPose(const SkinnedAnimComponent& a, std::vector<Matrix>& outGlobal, bool applyOverrides)
{
    if (!a.model) return false;
    const SkinnedModel& model = *a.model;

    // 同じフレームの 2 回目以降（マント → 描画 → 影 → 尾の根元）は層の状態が同じなので、素のポーズを使い回す
    // （2026-10-23。Debug で 1 回 0.7ms。マントを 5 列にして骨が 151 → 183 本に増えた分を取り返す）
    const SkinnedAnimComponent::PoseKey key = MakePoseKey(a);
    if (a.poseCacheValid && a.poseCacheKey == key && a.poseCache.size() == (size_t)model.GetSkeleton().GetBoneCount())
    {
        ++s_PoseHits;
        outGlobal = a.poseCache;
        if (applyOverrides)
            for (const auto& [bone, m] : a.boneOverrides)
                if (bone >= 0 && bone < (int)outGlobal.size()) outGlobal[bone] = m;
        return true;
    }

    ++s_PoseMisses;
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
    a.poseCache = outGlobal;
    a.poseCacheKey = key;
    a.poseCacheValid = true;

    // 揺れ物（マント等）の結果で差し替え
    if (applyOverrides)
        for (const auto& [bone, m] : a.boneOverrides)
            if (bone >= 0 && bone < (int)outGlobal.size()) outGlobal[bone] = m;
    return true;
}
