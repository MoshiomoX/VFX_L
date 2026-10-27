// ============================================================
// CollisionTestSceneRender.cpp
// CollisionTestScene の描画の順番と松明の光（2026-10-25 に CollisionTestScene.cpp から分けた。中身はそのまま）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/TransformComponent.h"
#include "Graphics/Light/PointLightManager.h"
#include "UI/UIDeco.h"
#include "Debug/FrameProfiler.h"
#include "Core/Application.h"

// ============================================================
// Render
// ============================================================
void CollisionTestScene::Render(Renderer& renderer)
{
    m_Lighting.Apply(renderer);
    auto& gfx = Application::Get().GetGraphics();
    ID3D11DeviceContext* ctx = gfx.GetContext();

    // ---- 0) 太陽のシャドウマップ（段ごとに影を落とす物を深度だけで描く）→ シーンの HDR RT に戻す ----
    // 雑魚（GPU）はシャドウマップに入れない（足元の丸い影で代える）
    if (m_ShowMesh && GetCamera())
    {
        PROFILE_SCOPE_GPU("Shadow maps");
        m_Shadows.Render(ctx, renderer, *GetCamera(), m_Lighting.SunDirection(),
            [&](const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj, int cascade)
            {
                m_RenderSystem.RenderDepth(m_Registry, renderer, cascade == 0);
                m_StaticProps.RenderDepth(ctx, view, proj);
            });
        gfx.RestoreRenderTarget();
    }
    else
        m_Shadows.Disable(renderer);

    // 空（画面全体。深度を触らないので一番最初に）
    m_Lighting.DrawSky(ctx, GetCamera());

    SceneBase::Render(renderer);

    // ---- 1) モデル描画（CPU の実体 + GPU の雑魚）----
    if (m_ShowMesh)
    {
        { PROFILE_SCOPE_GPU("Models (ECS)"); m_RenderSystem.Render(m_Registry, renderer); }
        { PROFILE_SCOPE_GPU("Props (instanced)"); m_StaticProps.Render(renderer); }
        { PROFILE_SCOPE_GPU("Swarm draw"); m_Swarm.RenderOpaque(GetCamera(), renderer.GetLightData()); }
        // 光を受けるメッシュ粒子（石・矢。不透明で深度を書く）もアウトラインの前に（2026-10-04、線が付くように）
        if (m_ShowParticle)
        {
            PROFILE_SCOPE_GPU("Lit mesh particles");
            m_ParticleSystem.SetCamera(GetCamera());
            m_ParticleSystem.SetLight(renderer.GetLightData());
            m_ParticleSystem.RenderLitMeshes();
        }
        // トゥーンのアウトライン（2026-10-04）：不透明な物を描き終えた深度から。草（葉の一枚一枚に線が付く）と
        // 半透明・粒子・UI は後から描くので線が乗らない
        { PROFILE_SCOPE_GPU("Outline"); m_Outline.Render(ctx, Application::Get().GetGraphics(), GetCamera()); }
        {
            PROFILE_SCOPE_GPU("Grass");
            // 草の火を 1 歩進めてから草を描く（GPU の範囲が熱源。Flush の後なので範囲の池は読むだけ）
            m_GrassFire.Step(m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(), m_Grass.GetGroundSRV());
            m_Grass.SetFire(m_GrassFire.GetSRV(), m_GrassFire.GetSettings().ignite);
            // 爆発 / 光線 / 雑魚の足元も草を倒す（2026-10-13。池は Flush の後なので読むだけ）
            m_Grass.SetSwarmSources(m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(), m_Swarm.GetAreaEndSRV(),
                m_Swarm.GetEnemySRV(), m_Swarm.GetEnemyStateSRV(), m_Swarm.GetEnemyExtraSRV());
            m_Grass.SetWindSources(m_Swarm.GetWindSourceSRV());   // 砂旋の風源（草が輪になびく。2026-10-19）
            if (GetCamera()) m_Grass.Render(renderer, *GetCamera());   // 地形の後（深度で埋まる所を描かない）
            // 落ち葉（1 歩進めて描く。爆発の範囲と光線は読むだけ）
            if (GetCamera()) m_Debris.Render(renderer, *GetCamera(), m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(), m_Swarm.GetAreaEndSRV(),
                m_Swarm.GetWindSourceSRV());   // 砂旋の風源（落ち葉 / 砂屑が巻き上がる）
            // タンポポ（草の踏み跡は今フレームの物。風源で綿毛がほどけて飛ぶ。2026-10-22）
            if (GetCamera()) m_Dandelions.Render(renderer, *GetCamera(), m_Grass.GetTrampleSRV(), m_Swarm.GetWindSourceSRV(), PlayerPos());
        }
        {
            // 水たまりの水面（2026-10-27）：半透明。草・落ち葉の後（水の縁の草の上に被らない）、群れの重ね描き（警告の輪・体力バー）の前
            PROFILE_SCOPE_GPU("Water");
            if (GetCamera()) m_Water.Render(renderer, *GetCamera(), m_Swarm.GetAreaSRV(), m_Swarm.GetAreaStateSRV(), m_Swarm.GetWindSourceSRV());
        }
        { PROFILE_SCOPE_GPU("Swarm overlay"); m_Swarm.RenderOverlay(GetCamera(), renderer.GetLightData()); }
    }
    else
    {
        PROFILE_SCOPE_GPU("Outline");
        m_Outline.Render(ctx, Application::Get().GetGraphics(), GetCamera());
    }
    if (m_ShowSwarmDebug)
        m_Swarm.RenderDebug(GetCamera());

    {
        PROFILE_SCOPE_GPU("Billboards + sprites");
        // ---- 2a) CPU で出した液溜まり（地面なので連番画像より先。高さ場は群れの物を借りる）----
        m_LiquidRenderer.SetTerrain(m_Swarm.GetHeightSRV(), m_Swarm.GetFrameCB());
        m_LiquidRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera(), renderer.GetLightData());
        // ---- 2) Mesh entry（光球の球など。深度は読むだけ）→ 連番画像（CPU の Sprite entry と、GPU の範囲が出した物）。粒子の前 ----
        m_Feedback.SubmitMeshes(m_MeshRenderer, m_Registry, m_Player);   // シールドの護罩（一時停止中も見えるようにここで積む）
        m_PlayerVFX.SubmitMeshes(m_MeshRenderer);                        // 詠唱の魔法陣（足元・手の前）
        m_SpellFx.Submit(m_MeshRenderer);                                // アイススパイクの氷のトゲ
        m_SandSpikes.Submit(m_MeshRenderer);                             // 砂の守護者の砂の槍の石のトゲ（2026-10-24）
        m_MeshRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());
        m_SpriteRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());
        m_BeamRenderer.Render(Application::Get().GetGraphics().GetContext(), GetCamera());   // 光線（加算）
        m_Swarm.RenderSprites(GetCamera());
        m_Swarm.RenderProjTrails(GetCamera());   // GPU の弾が引く帯（Trail entry、2026-10-15）
    }

    // ---- 3) 粒子（VFX 本体）----
    if (m_ShowParticle)
    {
        PROFILE_SCOPE_GPU("Particles draw");
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.SetLight(renderer.GetLightData());   // 立方体粒子の Lambert 用
        m_ParticleSystem.Render(!m_ShowMesh);   // 光を受けるメッシュ粒子はアウトラインの前に描き済み（m_ShowMesh の時）
        // 画面の歪み（2026-10-20、風の空気の揺らぎ）：歪みの強さを持つ粒子だけを別の RT へ。合成がその分ずらす
        // 歪みを持つ粒子が生きていない時は resolve 2 回 + 全画面 1 回を省く（2026-10-25）
        if (m_ParticleSystem.HasDistortion() && gfx.BeginDistortion())
        {
            m_ParticleSystem.RenderDistortion();
            gfx.RestoreRenderTarget();
            gfx.ApplyDistortion();   // UI を描く前にシーンへ掛ける（UI は歪まない）
        }
    }
    m_Shadows.Unbind(ctx);   // 次のフレームでシャドウマップを DSV にするので外す

    // ---- リザルトの背景の写真（UI を描く前の画面。2026-10-09）----
    if (m_SnapshotThisFrame)
    {
        m_SnapshotThisFrame = false;
        SceneSnapshot& snap = SceneSnapshot::Get();
        if (snap.Capture(gfx))
        {
            snap.shotTime = m_RunTime;
            snap.highlight = m_SnapshotIsHighlight;
            if (m_AutoTest)
            {
                char line[64];
                snprintf(line, sizeof(line), "snapshot t %.2f highlight %d best %d", m_RunTime, m_SnapshotIsHighlight ? 1 : 0, m_Highlight.Best());
                AutoTestLog(line);   // TEMP-TEST
            }
        }
    }

    // ============================================================
    // 4) UI（一番手前。Begin/End の管理は GameUI の中）。タイトルは名前とメニューだけ
    // ============================================================
    PROFILE_SCOPE_GPU("Game UI");
    if (m_TitleScene)       m_TitleUI.Render(gfx);
    else if (m_SelectScene) m_SelectUI.Render(gfx);
    else                    m_GameUI.Render(m_Registry, m_Player);
}

// ============================================================
// 遺跡の松明・洞窟の壁の松明：プレイヤーに近い順に StageDef::torchLights 個だけ点光源を付ける
// （松明は外周に 60 本ほど。当時の点光源の上限 64 を使い切らないため。2026-10-23 の Forward+ で上限は 256）。
// 50m より遠い物は付けない（草原・砂漠は洞窟の中にしか無く、遠くで光っても見えない）
// ============================================================
void CollisionTestScene::SubmitTorchLights()
{
    if (m_Torches.empty() || !m_Registry.IsValid(m_Player)) return;
    const StageDef& def = StageConfig::Get(m_StageIndex);
    const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;

    // 近い順に並べる（松明の数は少ないので毎フレーム並べ替えても軽い）
    std::vector<std::pair<float, size_t>> order;
    order.reserve(m_Torches.size());
    for (size_t i = 0; i < m_Torches.size(); ++i)
        order.push_back({ (m_Torches[i] - pp).LengthSquared(), i });
    std::sort(order.begin(), order.end());

    auto& lights = PointLightManager::Get();
    const float t = UIDeco::Clock();
    const int n = (std::min)((int)order.size(), def.torchLights);
    constexpr float kMaxDistSq = 50.0f * 50.0f;
    for (int k = 0; k < n && order[k].first < kMaxDistSq; ++k)
    {
        const Vector3& p = m_Torches[order[k].second];
        // 松明ごとに位相をずらした揺らぎ
        const float flicker = 0.85f + 0.15f * std::sin(t * 9.0f + (float)order[k].second * 1.7f)
            * std::cos(t * 5.3f + (float)order[k].second * 0.9f);
        // 洞窟の中（平原より低い）は小さく：点光源は壁を抜けるので、大きいと洞の外の壁・草まで照らす
        const bool inCave = p.y < -1.0f;
        lights.Add(p, { 1.0f, 0.62f, 0.25f }, inCave ? 10.0f : 18.0f, (inCave ? 2.2f : 2.6f) * flicker);
    }
}
