// ============================================================
// LocalWind.h
// 局所の風源（Swarm::WindSource の表）を CPU で引く（2026-10-22、ユーザー「風に影響される物を増やしたい」→
// マントと転がり草が竜巻・渦・砂旋・突風に巻かれる）。
//   表は GPU（SwarmWindSourceCS）が毎フレーム作り、SwarmSystem がリードバックして持っている（2〜3 フレーム遅れ。
//   SwarmSystem::GetLocalWind）。Air() は Shader/Common/WindSources.hlsli の WindSourceAir と同じ式
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include <SimpleMath.h>
#include <array>
#include <algorithm>
#include <cmath>

struct LocalWind
{
    std::array<Swarm::WindSource, Swarm::kMaxWindSources> sources = {};

    // 地面の点 (x, z) での空気の速さ（m/s）。lift = 一番強い風源の効き 0〜1（芯ほど 1）
    DirectX::SimpleMath::Vector3 Air(float x, float z, float* lift = nullptr) const
    {
        DirectX::SimpleMath::Vector3 air(0.0f, 0.0f, 0.0f);
        float maxLift = 0.0f;
        for (const Swarm::WindSource& w : sources)
        {
            if (w.radius <= 0.0f) continue;
            const float dx = x - w.pos[0], dz = z - w.pos[2];
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= w.radius) continue;
            const float r = dist / w.radius;
            const float ox = dist > 1e-3f ? dx / dist : 0.0f;
            const float oz = dist > 1e-3f ? dz / dist : 1.0f;
            const float tx = -oz, tz = ox;
            const float sw = std::clamp(r / 0.3f, 0.0f, 1.0f) * std::clamp((1.0f - r) / 0.7f, 0.0f, 1.0f);
            const float up = std::clamp((1.0f - r) / 0.6f, 0.0f, 1.0f);
            const float inw = std::clamp((r - 0.25f) / 0.75f, 0.0f, 1.0f) * (1.0f - r);
            const float ps = 1.0f - r * r;
            const float swirl = w.swirl * sw * w.fade;
            const float inflow = w.inflow * inw * w.fade;
            const float push = w.push * ps * w.fade;
            air.x += tx * swirl - ox * inflow + w.pushDir[0] * push;
            air.z += tz * swirl - oz * inflow + w.pushDir[1] * push;
            air.y += w.updraft * up * w.fade;
            maxLift = (std::max)(maxLift, (1.0f - r) * w.fade);
        }
        if (lift) *lift = maxLift;
        return air;
    }

    // 風源の芯へ向かう向き × 強さ（2026-10-22、転がり草が竜巻・渦に吸い込まれる用）。
    // 強さ = inflow（4 m/s で頭打ち）× fade × (1 − r)。芯のすぐ近く（r < 0.15）は 0 へ落とす（芯で震えない）
    DirectX::SimpleMath::Vector3 Inward(float x, float z) const
    {
        DirectX::SimpleMath::Vector3 v(0.0f, 0.0f, 0.0f);
        for (const Swarm::WindSource& w : sources)
        {
            if (w.radius <= 0.0f || w.inflow <= 0.0f) continue;
            const float dx = w.pos[0] - x, dz = w.pos[2] - z;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= w.radius || dist < 1e-3f) continue;
            const float r = dist / w.radius;
            const float s = (std::min)(w.inflow, 4.0f) * w.fade * (1.0f - r) * std::clamp(r / 0.15f, 0.0f, 1.0f);
            v.x += dx / dist * s;
            v.z += dz / dist * s;
        }
        return v;
    }
};
