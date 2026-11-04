// ============================================================
// CollisionTestSceneSand.cpp
// 砂漠の砂の面（World/SandField、2026-11-02）をシーンに繋ぐ所：
//   BuildSandField : 地形の後（Init・作り直し・ステージ選択の切替）。砂漠だけ動かす
//   PushSand       : PushWind の中（Flush の前）。風・プレイヤーの足・陽炎・舞う砂の入力（SwarmSystem::SetSandHaze）
//   StepSand       : 描画の最初（Flush の後、地形を描く前）。1 歩進めて地形の材質へ渡す
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/TransformComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Graphics/Renderer/TerrainSurface.h"
#include "Core/Application.h"

void CollisionTestScene::BuildSandField()
{
    m_Sand.Build(m_Grid);
    m_Sand.SetActive(m_TerrainConfig.biome == TerrainGenerator::Biome::Desert);
    m_Footprints.Clear();
}

void CollisionTestScene::UpdateFootprints(float dt)
{
    m_PlayerVFX.sandSteps = m_Sand.Active();   // 次の一歩から砂粒（今の一歩はもう出ている）
    if (!m_Sand.Active())
    {
        if (m_Footprints.Count() > 0) m_Footprints.Clear();
        return;
    }
    const int gw = m_Grid.Width();
    for (const auto& st : m_PlayerVFX.Steps())
    {
        const float ground = m_Grid.SampleHeight(st.pos.x, st.pos.z);
        if (st.pos.y - ground > 0.25f) continue;   // 岩・箱の上（砂ではない）
        int gx = 0, gz = 0;
        m_Grid.WorldToCell(st.pos, gx, gz);
        if (gx < 0 || gz < 0 || gx >= gw || gz >= m_Grid.Depth()) continue;
        if (m_Grid.IsWater(gx, gz)) continue;
        const size_t ci = (size_t)gz * gw + gx;
        if (ci < m_GrassMask.size() && m_GrassMask[ci]) continue;   // オアシスの草の上
        // 地面の傾き（高さ場の差。1m 離した 4 点）
        const float e = 0.5f;
        const float hx = m_Grid.SampleHeight(st.pos.x + e, st.pos.z) - m_Grid.SampleHeight(st.pos.x - e, st.pos.z);
        const float hz = m_Grid.SampleHeight(st.pos.x, st.pos.z + e) - m_Grid.SampleHeight(st.pos.x, st.pos.z - e);
        DirectX::SimpleMath::Vector3 n(-hx, 2.0f * e, -hz);
        n.Normalize();
        if (n.y < 0.6f) continue;   // 崖の面
        m_Footprints.Add(DirectX::SimpleMath::Vector3(st.pos.x, (std::max)(ground, st.pos.y - 0.05f), st.pos.z), st.yawDeg, st.side, n);
    }
    const auto& s = m_Sand.GetSettings();
    m_Footprints.Update(dt, &m_Swarm.GetLocalWind(), &m_Weather.Wind(), m_Grass.Time(),
        s.dustLiftWind, s.dustRange, s.dustWorldShare);
}

void CollisionTestScene::RenderFootprints(const DirectX::SimpleMath::Vector3& sunDir)
{
    CameraBase* cam = GetCamera();
    if (!cam || !m_Sand.Active() || m_Footprints.Count() == 0) return;
    m_Footprints.Render(*cam, m_Sand.GetStateSRV(), m_Sand.MapOrigin(), m_Sand.MapSize(), sunDir);
}

void CollisionTestScene::PushSand()
{
    if (!m_Sand.Active())
    {
        m_Swarm.SetSandHaze(nullptr);
        m_Swarm.SetSandDust(nullptr);
        return;
    }
    m_Sand.SetWind(m_Weather.Wind(), m_Grass.Time());
    if (m_Registry.IsValid(m_Player) && m_Registry.Has<TransformComponent>(m_Player))
    {
        const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
        bool grounded = true;
        if (m_Registry.Has<RigidbodyComponent>(m_Player))
            grounded = m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        m_Sand.SetPlayer(tf.position, grounded && !IsPlayerDead());
    }
    GrassFireEmitInput haze;
    CameraBase* cam = GetCamera();
    if (cam && m_Sand.FillEmitInput(haze, cam->GetPosition(), m_Grass.GetHeightSRV()))
        m_Swarm.SetSandHaze(&haze);
    else
        m_Swarm.SetSandHaze(nullptr);
    GrassFireEmitInput dust;
    if (cam && m_Sand.FillDustInput(dust, cam->GetPosition(), m_Grass.GetHeightSRV()))
        m_Swarm.SetSandDust(&dust);
    else
        m_Swarm.SetSandDust(nullptr);
}

void CollisionTestScene::StepSand()
{
    auto& surface = TerrainSurface::Get();
    if (!m_Sand.Active())
    {
        surface.SetSandField(nullptr, nullptr, {}, {}, 0.0f, 0.0f, 0.0f);
        return;
    }
    // 前のフレームの地形が t3 / t4 に張ったままだと、CS が同じテクスチャを UAV にする時に外される（デバッグ層の警告）。先に外す
    ID3D11DeviceContext* ctx = Application::Get().GetGraphics().GetContext();
    ID3D11ShaderResourceView* nulls[2] = { nullptr, nullptr };
    ctx->PSSetShaderResources(3, 2, nulls);

    m_Sand.Step(m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(),
        m_Swarm.GetEnemySRV(), m_Swarm.GetEnemyStateSRV(), m_Swarm.GetEnemyExtraSRV(),
        m_Grass.GetHeightSRV(), m_Swarm.GetAIParams().groundY, m_Swarm.GetWindSourceSRV());
    const auto& s = m_Sand.GetSettings();
    surface.SetSandField(m_Sand.GetStateSRV(), m_Sand.GetTrackSRV(), m_Sand.MapOrigin(), m_Sand.MapSize(),
        s.wetDark, s.heatTint, s.trackStrength);
}
