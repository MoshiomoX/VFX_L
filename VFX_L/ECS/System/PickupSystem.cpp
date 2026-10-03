// ============================================================
// PickupSystem.cpp
// ============================================================
#include "ECS/System/PickupSystem.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Light/PointLightManager.h"
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"
#include "Audio/AudioSystem.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <iostream>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr float kFloatY = 0.6f;   // 地面からの浮き（蹄鉄の底）

    void BoxVerts(Vector3 v[8], const Vector3& lo, const Vector3& hi)
    {
        v[0] = { lo.x, lo.y, lo.z }; v[1] = { hi.x, lo.y, lo.z };
        v[2] = { hi.x, lo.y, hi.z }; v[3] = { lo.x, lo.y, hi.z };
        v[4] = { lo.x, hi.y, lo.z }; v[5] = { hi.x, hi.y, lo.z };
        v[6] = { hi.x, hi.y, hi.z }; v[7] = { lo.x, hi.y, hi.z };
    }

    // 蹄鉄形の磁石（底が原点、幅 1m・高さ 1m 弱）。赤い U 字 + 銀の先。頂点色は線形
    std::shared_ptr<Model> BuildMagnetModel(ID3D11Device* device)
    {
        const Vector4 red = { 0.75f, 0.02f, 0.02f, 1.0f };
        const Vector4 silver = { 0.75f, 0.75f, 0.80f, 1.0f };
        PrimitiveBuilder::HexahedronBatch batch;
        Vector3 v[8];
        auto box = [&](const Vector3& lo, const Vector3& hi, const Vector4& c)
            {
                BoxVerts(v, lo, hi);
                batch.Append(v, c, c);
            };
        box({ -0.50f, 0.00f, -0.12f }, { 0.50f, 0.25f, 0.12f }, red);     // 底の横棒
        box({ -0.50f, 0.25f, -0.12f }, { -0.25f, 0.72f, 0.12f }, red);    // 左の腕
        box({ 0.25f, 0.25f, -0.12f }, { 0.50f, 0.72f, 0.12f }, red);      // 右の腕
        box({ -0.50f, 0.72f, -0.12f }, { -0.25f, 0.95f, 0.12f }, silver); // 左の先
        box({ 0.25f, 0.72f, -0.12f }, { 0.50f, 0.95f, 0.12f }, silver);   // 右の先
        return batch.Build(device);
    }
}

void PickupSystem::Init(ID3D11Device* device)
{
    m_Model = BuildMagnetModel(device);
    m_Magnets.clear();
}

bool PickupSystem::Place(Registry& reg, const GridWorld& grid, const Vector3& center, float rMin, float rMax)
{
    if (!m_Model) return false;
    std::uniform_real_distribution<float> angleDist(0.0f, DirectX::XM_2PI);
    std::uniform_real_distribution<float> radiusDist(rMin, (std::max)(rMin, rMax));

    for (int attempt = 0; attempt < 64; ++attempt)
    {
        const float a = angleDist(m_Rng);
        const float r = radiusDist(m_Rng);
        const Vector3 probe = center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);

        int gx = 0, gz = 0;
        grid.WorldToCell(probe, gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;
        Vector3 pos = grid.CellToWorld(gx, gz);
        pos.y = grid.SampleHeight(pos.x, pos.z);

        Entity e = reg.Create();
        TransformComponent tf;
        tf.position = pos + Vector3(0.0f, kFloatY, 0.0f);
        reg.Add<TransformComponent>(e, tf);
        ModelComponent mc;
        mc.model = m_Model;
        reg.Add<ModelComponent>(e, mc);

        m_Magnets.push_back({ e, pos, a * 3.0f });
        return true;
    }
    return false;
}

void PickupSystem::Reset(Registry& reg, const GridWorld& grid, const Vector3& center, uint32_t seed)
{
    for (const auto& m : m_Magnets)
        if (reg.IsValid(m.e)) reg.Destroy(m.e);
    m_Magnets.clear();
    m_Rng.seed(seed * 2654435761u + 7u);
    m_NextSpawn = respawnInterval;
    for (int i = 0; i < initialCount; ++i)
        Place(reg, grid, center, startRingMin, startRingMax);
}

int PickupSystem::Update(Registry& reg, const GridWorld& grid, const Vector3& player,
    float dt, float runTime, SwarmSystem& swarm, Vector3* outPicked)
{
    m_Time += dt;

    // ---- 時間で足す（場に maxOnMap 個まで。満杯の時は次の区切りまで待つ）----
    if (runTime >= m_NextSpawn)
    {
        if ((int)m_Magnets.size() < maxOnMap)
            Place(reg, grid, player, ringMin, ringMax);
        m_NextSpawn = runTime + (std::max)(respawnInterval, 1.0f);
    }

    // ---- 浮遊・回転、触れたら使う ----
    int picked = 0;
    for (size_t i = 0; i < m_Magnets.size();)
    {
        Magnet& m = m_Magnets[i];
        if (!reg.IsValid(m.e)) { m_Magnets.erase(m_Magnets.begin() + (ptrdiff_t)i); continue; }

        auto& tf = reg.Get<TransformComponent>(m.e);
        const float bob = (std::sin(m_Time * 2.0f + m.phase) * 0.5f + 0.5f) * bobHeight;
        tf.position = m.base + Vector3(0.0f, kFloatY + bob, 0.0f);
        tf.rotation.y = std::fmod(tf.rotation.y + spinSpeed * dt, 360.0f);

        const float dx = m.base.x - player.x, dz = m.base.z - player.z;
        if (dx * dx + dz * dz <= pickupRadius * pickupRadius)
        {
            swarm.MagnetAllOrbs(magnetSeconds);
            AudioSystem::Get().Play("magnet");
            if (outPicked) *outPicked = m.base;
            reg.Destroy(m.e);
            m_Magnets.erase(m_Magnets.begin() + (ptrdiff_t)i);
            ++picked;
            ++m_Picked;
            std::cout << "[Pickup] magnet picked (" << m_Picked << " so far)" << std::endl;
            continue;
        }
        ++i;
    }
    return picked;
}

void PickupSystem::SubmitLights() const
{
    auto& lights = PointLightManager::Get();
    for (const auto& m : m_Magnets)
        lights.Add(m.base + Vector3(0.0f, kFloatY + 0.6f, 0.0f), { 1.0f, 0.25f, 0.2f }, 3.0f, 1.2f);
}

std::vector<Vector3> PickupSystem::GetPositions() const
{
    std::vector<Vector3> out;
    out.reserve(m_Magnets.size());
    for (const auto& m : m_Magnets) out.push_back(m.base + Vector3(0.0f, kFloatY + 0.5f, 0.0f));
    return out;
}

void PickupSystem::DrawImGui(Registry& reg, const GridWorld& grid, const Vector3& player)
{
    if (!ImGui::CollapsingHeader("Pickups (Magnet)"))
        return;
    ImGui::Text("on map %d   picked %d   next in %.0f s", (int)m_Magnets.size(), m_Picked, m_NextSpawn);
    ImGui::DragInt("Initial Count", &initialCount, 0.1f, 0, 20);
    ImGui::DragInt("Max On Map", &maxOnMap, 0.1f, 0, 20);
    ImGui::DragFloat("Respawn Interval (s)", &respawnInterval, 1.0f, 1.0f, 600.0f);
    ImGui::DragFloatRange2("Start Ring", &startRingMin, &startRingMax, 0.5f, 2.0f, 100.0f);
    ImGui::DragFloatRange2("Ring", &ringMin, &ringMax, 0.5f, 2.0f, 100.0f);
    ImGui::DragFloat("Pickup Radius", &pickupRadius, 0.05f, 0.2f, 5.0f);
    ImGui::DragFloat("Magnet Seconds", &magnetSeconds, 0.05f, 0.05f, 5.0f);
    if (ImGui::Button("Place Magnet Nearby")) Place(reg, grid, player, 4.0f, 6.0f);
}
