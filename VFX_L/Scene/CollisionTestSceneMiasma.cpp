// ============================================================
// CollisionTestSceneMiasma.cpp
// 遺跡の瘴気 / 砂漠の砂塵（World/MiasmaField、2026-11-03）をシーンに繋ぐ所：
//   BuildMiasma  : 地形の後（Init・作り直し・ステージ選択の切替）。遺跡（瘴気）と砂漠（砂塵）で動かす（VFXL_MIASMA=1 で他の面でも）
//   PushMiasma   : PushWind の中。世界の風と箱の中心（プレイヤー）、遺跡の松明（周りを晴らして暖かく照らす。2026-11-05）
//   RenderMiasma : 描画の中、水の後・群れの重ね描きの前。1 歩進めて描く
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Core/Application.h"
#include "UI/UIDeco.h"

#include <Windows.h>

void CollisionTestScene::BuildMiasma()
{
    // 遺跡 = 瘴気、砂漠 = 砂塵（2026-11-03。同じ箱を種類で使い分ける）
    const auto biome = m_TerrainConfig.biome;
    m_Miasma.SetKind(biome == TerrainGenerator::Biome::Desert ? MiasmaField::Kind::Dust : MiasmaField::Kind::Miasma);
    m_Miasma.Build(m_Grid);
    bool on = biome == TerrainGenerator::Biome::Dungeon || biome == TerrainGenerator::Biome::Desert;
    char env[8] = {};
    if (GetEnvironmentVariableA("VFXL_MIASMA", env, sizeof(env)) > 0 && env[0] == '1')   // TEMP-TEST: どの面でも
        on = true;
    m_Miasma.SetActive(on);
}

void CollisionTestScene::PushMiasma()
{
    if (!m_Miasma.Active()) return;
    m_Miasma.SetWind(m_Weather.Wind(), m_Grass.Time());
    const Vector3* pp = PlayerPos();
    if (!pp) return;
    m_Miasma.SetCenter(*pp, m_Grid);
    // 松明は瘴気だけ（点光源と同じ時計で揺らぐ）。砂塵の時は渡さない
    static const std::vector<Vector3> kNoTorches;
    m_Miasma.SetTorches(m_Miasma.GetKind() == MiasmaField::Kind::Miasma ? m_Torches : kNoTorches, UIDeco::Clock());
}

void CollisionTestScene::RenderMiasma()
{
    if (!m_Miasma.Active()) return;
    m_Miasma.Step(m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(), m_GrassFire.GetHeatSRV(),
        m_Swarm.GetWindSourceSRV(), m_Grass.GetHeightSRV(),
        m_Miasma.GetKind() == MiasmaField::Kind::Dust ? m_Sand.GetStateSRV() : nullptr);   // 砂塵は砂の面の舞う強さで巻き上がる
    auto& gfx = Application::Get().GetGraphics();
    m_Miasma.Render(gfx.GetContext(), gfx, GetCamera(), m_Grass.GetHeightSRV());
}
