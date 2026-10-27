// ============================================================
// CollisionTestSceneDebug.cpp
// CollisionTestScene の ImGui パネル・デバッグ描画・TEMP-TEST の自動テスト。
// 本体（初期化・毎フレームの流れ・描画）は CollisionTestScene.cpp。
// 出来上がった機能のパネルは各部品が持つ（BattleCamera / RewardCrateSystem /
// FeedbackVFXSystem / SceneLighting / EliteSpawner / MobSpawner / StressTestTools）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Debug/AutoTest/BattleAutoTest.h"   // TEMP-TEST
#include "Core/DevUI.h"
#include "Audio/AudioSystem.h"
#include "Graphics/Renderer/TerrainSurface.h"
#include "Component/TransformComponent.h"
#include "Component/ClothChainComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Component/InteractableComponent.h"
#include "Component/ModelComponent.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "ECS/View.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Item/ItemTypes.h"
#include "Item/ItemInfo.h"
#include "UI/LevelUpSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/TerrainGenerator.h"
#include "VFX_Editor/VFXId.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Core/Application.h"
#include "imgui.h"


// ============================================================
// ImGui: 全体
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    if (!DevUI::Active()) return;   // Demo ビルド（ImGui が無い）：パネルも目印・ギズモも出さない
    if (!DebugManager::Get().GetShowPanels()) return;   // 既定は全部隠す。F10 で表示（2026-10-16）

    // 太陽の目印・シーン光源のギズモ（バックパック・レベルアップ・呪文書を開いている間はギズモを出さない）
    m_Lighting.DrawMarkers(PlayerPos(), m_Grid, GetCamera(), !m_GameUI.ShouldPauseGame());

    ImGui::Begin("Game Test");
    ImGui::SameLine();
    ImGui::Checkbox("Swarm Debug", &m_ShowSwarmDebug);
    ImGui::Checkbox("Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::SameLine();
    ImGui::Checkbox("Particle", &m_ShowParticle);
    ImGui::SameLine();
    ImGui::Checkbox("Collider", &m_ShowWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Wand Debug", &m_ShowWandDebug);
    ImGui::SameLine();
    ImGui::Checkbox("Grid", &m_ShowGridDebug);
    ImGui::SameLine();
    if (ImGui::Button("End Run -> Result")) EndRun();   // リザルト画面の確認用
    ImGui::Separator();

    DrawPlayerPanel();
    DrawWandPanel();
    m_GameUI.DrawDebugUI(m_Registry, m_Player, m_BackpackAggregate);
    m_Stress.DrawImGui(m_Swarm, m_CollisionSystem, m_ParticleSystem);
    if (!m_LabScene) m_SpellLab.DrawImGui(m_Registry, m_Player, m_Swarm, m_Mobs, m_Grid);   // 実験場では独立した窓（下）
    DrawSwarmPanel();
    DrawItemDatabasePanel();
    DrawTerrainPanel();
    m_Grass.DrawImGui();
    m_GrassFire.DrawImGui(PlayerPos());   // 草の延焼（点火ボタン・温度の絵）
    m_Debris.DrawImGui();                 // 地面の落ち葉
    m_Water.DrawImGui();                  // 水たまりの水面（2026-10-27）
    m_Dandelions.DrawImGui();             // タンポポ（2026-10-22）
    m_GrassShreds.DrawImGui();            // 風源の草屑（2026-10-23）
    m_Tumbleweeds.DrawImGui(m_Registry, m_Grid, PlayerPos() ? *PlayerPos() : Vector3::Zero);   // 転がり草
    DrawEnemiesPanel();
    if (m_Crates.DrawImGui(m_Interaction, m_LevelUpSystem)) RespawnCrates();
    if (const Vector3* pp = PlayerPos())
        m_Pickups.DrawImGui(m_Registry, m_Grid, *pp);
    m_Feedback.DrawImGui(m_Registry, m_Player);
    m_PlayerVFX.DrawImGui();          // プレイヤーの動きのエフェクト（詠唱の光・土煙・着地・尾・被弾の閃き）
    if (ImGui::Begin("Spell FX (CPU)"))   // 魔法の CPU 側の見た目（アイススパイクの氷のトゲ）
    {
        // Test cast はプレイヤーの足元から体の向きへ 8m
        Vector3 pos = Vector3::Zero, dir = Vector3::UnitZ;
        if (const Vector3* pp = PlayerPos()) pos = *pp;
        if (m_Registry.IsValid(m_Player) && m_Registry.Has<TransformComponent>(m_Player))
        {
            const float yaw = DirectX::XMConvertToRadians(m_Registry.Get<TransformComponent>(m_Player).rotation.y);
            dir = Vector3(std::sin(yaw), 0.0f, std::cos(yaw));
        }
        m_SpellFx.DrawImGui(pos, pos + dir * 8.0f, [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
    }
    ImGui::End();
    AudioSystem::Get().DrawImGui();   // 音量・BGM・cue の試聴
    m_Camera.DrawImGui();
    m_Lighting.DrawImGui(PlayerPos());   // 末尾にトゥーンの陰影（Toon Shading）
    m_Weather.DrawImGui();               // 時刻・天候の出来事（Drive Lighting が入っていると Lighting の手調整は毎フレーム戻る）
    if (ImGui::CollapsingHeader("Toon Outline"))   // トゥーンのアウトライン（2026-10-04）
        m_Outline.DrawImGui();
    m_Shadows.DrawImGui();
    FrameProfiler::Get().DrawImGui();
    DrawBloomPanel();
    ImGui::End();

    // 実験場（F7）：魔法の組み合わせの窓は常に開いている
    if (m_LabScene) m_SpellLab.DrawWindow(m_Registry, m_Player);
}

// ============================================================
// 衝突体のワイヤ・杖の可視化・格子（UpdateGameplay の最後）
// ============================================================
void CollisionTestScene::DrawGameplayDebug()
{
    if (m_ShowWireframe)
    {
        std::unordered_set<Entity> hitting;
        for (const auto& p : m_CollisionSystem.GetPairs())
        {
            hitting.insert(p.a);
            hitting.insert(p.b);
        }

        m_Registry.CreateView<TransformComponent, ColliderComponent>()
            .Each([&](Entity e, TransformComponent&, ColliderComponent&)
                {
                    Color col = hitting.count(e) ? Color(1.0f, 0.3f, 0.3f, 1.0f)
                        : Color(0.4f, 1.0f, 0.4f, 1.0f);
                    DrawColliderDebug(e, col);
                });
    }

    if (m_ShowWandDebug)
        DrawWandDebug();
    const Vector3* pp = PlayerPos();
    if (m_ShowGridDebug && pp)
        m_Grid.DrawDebug(*pp);
}

// ============================================================
// ImGui: 敵（雑魚の湧き・初期値・AI は MobSpawner、エリートは EliteSpawner）
// ============================================================
void CollisionTestScene::DrawEnemiesPanel()
{
    if (!ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    m_Mobs.DrawImGui(m_Swarm);
    ImGui::Separator();
    m_Stage.DrawImGui(m_Swarm, m_RunTime);
    m_BossAttacks.DrawImGui();
    {
        // 面の固定エリート（2026-10-25）：A / B を詞缀付きで呼ぶ・調整値
        int spawn = -1;
        uint32_t affixes = 0;
        m_StageElites.DrawImGui(spawn, affixes);
        if (spawn >= 0)
            if (const Vector3* pp = PlayerPos())
                m_StageElites.Spawn(m_Grid, m_Swarm, *pp, spawn == 1, affixes, m_Stage.eliteHp * m_Mobs.GetHpMul(), 10.0f, 14.0f);
    }
    ImGui::Separator();

    if (m_Elites.DrawImGui(m_Registry, m_MeshVFXSystem)) RespawnElites();
    ImGui::SameLine();
    // GPU 側を全消し。counter は残るので kills (total) は減らない
    if (ImGui::Button("Kill All (GPU)")) m_Swarm.KillAll();
}

// ============================================================
// 衝突体のワイヤ描画
// ============================================================
void CollisionTestScene::DrawColliderDebug(Entity e, const Color& color)
{
    auto& tf = m_Registry.Get<TransformComponent>(e);
    auto& col = m_Registry.Get<ColliderComponent>(e);
    Vector3 c = tf.position + col.offset;

    auto& dbg = DebugManager::Get();
    switch (col.shape)
    {
    case ColliderShape::Sphere:  dbg.DrawWireSphere(c, col.radius, color); break;
    case ColliderShape::Capsule: dbg.DrawWireCapsule(c, col.radius, col.height, color); break;
    case ColliderShape::AABB:    dbg.DrawWireAABB(c, col.halfExtents, color); break;
    case ColliderShape::Convex:  dbg.DrawWireAABB(c, col.halfExtents, color); break;   // 包囲箱だけ
    case ColliderShape::HeightField: break;   // フィールド全体の地面（箱を描いても邪魔なだけ）
    }
}

// ============================================================
// 杖の可視化: 射程 / 標的 / 発射方向 / 待機中の発射
// ============================================================
void CollisionTestScene::DrawWandDebug()
{
    if (!m_Registry.IsValid(m_Player)) return;
    if (!m_Registry.Has<WandComponent>(m_Player)) return;

    auto& dbg = DebugManager::Get();
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& aim = m_WeaponSystem.GetAimDebug();

    Vector3 muzzle = tf.position + wand.muzzleOffset;
    dbg.DrawWireSphere(muzzle, wand.range, Color(0.25f, 0.4f, 0.8f, 1.0f));

    if (!aim.hasTarget) return;

    // 標的の位置
    {
        const Vector3 tp = aim.targetPos;
        const float m = 0.6f;
        const Color mark = aim.targetIsGpu ? Color(0.4f, 0.8f, 1.0f, 1.0f)   // 雑魚 = 水色
            : Color(1.0f, 1.0f, 0.2f, 1.0f);  // エリート = 黄
        dbg.AddDebugLine(tp - Vector3(m, 0, 0), tp + Vector3(m, 0, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, m, 0), tp + Vector3(0, m, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, 0, m), tp + Vector3(0, 0, m), mark);
    }

    // 出力源ごとの発射方向（高さをずらして重ならないようにする）
    for (size_t i = 0; i < wand.spells.size(); ++i)
    {
        const auto& s = wand.spells[i];
        Vector3 origin = aim.muzzle + Vector3(0.0f, (float)i * 0.22f, 0.0f);

        Color col = (s.pendingCasts > 0) ? Color(1.0f, 0.6f, 0.2f, 1.0f)
            : Color(0.3f, 1.0f, 0.4f, 1.0f);

        int count = (s.projectileCount < 1) ? 1 : s.projectileCount;
        const float len = 3.0f;

        if (count == 1 || s.spreadAngle <= 0.0f)
        {
            dbg.AddDebugLine(origin, origin + aim.dir * len, col);
        }
        else
        {
            float step = s.spreadAngle / (float)(count - 1);
            float start = -s.spreadAngle * 0.5f;
            for (int k = 0; k < count; ++k)
            {
                float deg = start + step * (float)k;
                Matrix rot = Matrix::CreateRotationY(DirectX::XMConvertToRadians(deg));
                Vector3 d = Vector3::TransformNormal(aim.dir, rot);
                d.Normalize();
                dbg.AddDebugLine(origin, origin + d * len, col);
            }
        }

        // 待機中の二重詠唱を短い縦線で数える
        for (int k = 0; k < s.pendingCasts; ++k)
        {
            Vector3 p = origin + aim.dir * 0.4f + Vector3(0.15f * (float)k, 0.0f, 0.0f);
            dbg.AddDebugLine(p, p + Vector3(0.0f, 0.18f, 0.0f), Color(1.0f, 0.5f, 0.1f, 1.0f));
        }
    }
}

// ============================================================
// ImGui: Bloom（後処理）
// Graphics が持つ BloomParams をそのまま書き換える。
// 合成 PS は毎フレーム値を読むので、変えた瞬間に画面へ反映される。
// 粒子の加算が 1.0 を超えた分だけ光る（Threshold 以上）。
// ============================================================
void CollisionTestScene::DrawBloomPanel()
{
    if (!ImGui::CollapsingHeader("Bloom (Post Process)"))
        return;

    auto& bp = Application::Get().GetGraphics().GetBloomParams();

    // ---- 有効 / 無効 ----
    ImGui::Checkbox("Enabled", &bp.enabled);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
        bp = BloomParams{};   // ヘッダの既定値へ戻す

    // ---- 抽出（BloomCS の prefilter）----
    ImGui::SeparatorText("Extract");
    ImGui::DragFloat("Threshold", &bp.threshold, 0.01f, 0.0f, 4.0f, "%.2f");
    ImGui::SetItemTooltip("HDR 1.0 = white. Only brightness above this goes into bloom");
    ImGui::DragFloat("Knee", &bp.knee, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::SetItemTooltip("Soft range below the threshold. 0 = hard cut");

    // ---- 合成（CompositePS）----
    ImGui::SeparatorText("Composite");
    ImGui::DragFloat("Intensity", &bp.intensity, 0.01f, 0.0f, 5.0f, "%.2f");
    ImGui::SetItemTooltip("scene + bloom * Intensity. 0 looks the same as disabled");
    ImGui::DragFloat("Exposure", &bp.exposure, 0.01f, 0.1f, 8.0f, "%.2f");
    ImGui::SetItemTooltip("Whole-screen brightness multiplier (applied after bloom is added)");
    ImGui::Checkbox("Tonemap (ACES)", &bp.tonemap);
    ImGui::SameLine();
    ImGui::Checkbox("Gamma", &bp.gamma);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::DragFloat("Gamma Exp", &bp.gammaExp, 0.01f, 1.0f, 3.0f, "%.2f");   // 2.2 = sRGB 相当
    // 画面の歪み（2026-10-20、風の空気の揺らぎ。粒子の distort が書いたずれ × この倍率）
    auto& dp = Application::Get().GetGraphics().GetDistortion();
    ImGui::Separator();
    ImGui::Checkbox("Distortion (wind refraction)", &dp.enabled);
    ImGui::DragFloat("Distortion Strength", &dp.strength, 0.01f, 0.0f, 4.0f, "%.2f");

    // ---- 調整用の当たり値 ----
    ImGui::SeparatorText("Presets");
    if (ImGui::Button("Off"))
    {
        bp.enabled = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Soft"))
    {
        bp.enabled = true;
        bp.threshold = 1.2f; bp.knee = 0.6f; bp.intensity = 0.5f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Default"))
    {
        bp.enabled = true;
        bp.threshold = 1.0f; bp.knee = 0.5f; bp.intensity = 0.8f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Strong"))
    {
        bp.enabled = true;
        bp.threshold = 0.7f; bp.knee = 0.5f; bp.intensity = 1.6f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Isolate"))
    {
        // bloom だけを見る：閾値 0 で全部拾い、露出を落として bloom の形を確認する
        bp.enabled = true;
        bp.threshold = 0.0f; bp.knee = 0.0f; bp.intensity = 3.0f; bp.exposure = 0.3f;
    }
    ImGui::TextDisabled("Presets set Threshold / Knee / Intensity (Isolate also drops Exposure).");

    // ---- 現状の要約 ----
    ImGui::SeparatorText("State");
    ImGui::Text("Pipeline : resolve -> %s -> composite(%s%s)",
        bp.enabled ? "BloomCS x9 (prefilter + 4 down + 4 up)" : "(bloom skipped)",
        bp.tonemap ? "ACES" : "linear",
        bp.gamma ? ", gamma" : "");
    ImGui::Text("Effective bloom gain : %.2f", bp.enabled ? bp.intensity * bp.exposure : 0.0f);
}

// ============================================================
// 体格や色を変えた時に見た目を作り直す
// 数値は PlayerStatsComponent が持つので PlayerFactory に任せる
// ============================================================
void CollisionTestScene::RebuildPlayerMesh()
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    PlayerFactory::RebuildVisual(m_Registry, m_Player, device,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
}

// ============================================================
// TEMP-TEST: 戦闘の反応エフェクトの自動テスト（VFXL_BATTLE_AUTOTEST）
// ============================================================
void CollisionTestScene::AutoTestLog(const char* what)
{
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::ofstream f("autotest.log", std::ios::app);
    f << ms << " " << what << " (game " << m_AutoTime << " s)";
    const auto& cam = m_Camera.Camera();
    const Vector3 cp = cam.GetPosition();
    f << " cam dist " << cam.GetCurrentDistance() << (cam.IsOccluded() ? " occluded" : "") << " trauma " << cam.GetTrauma()
      << " pitch " << cam.GetPitch() << " camPos " << cp.x << "," << cp.y << "," << cp.z;
    if (m_Registry.IsValid(m_Player))
    {
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        f << " player " << pp.x << "," << pp.y << "," << pp.z;
    }
    f << std::endl;
    const std::string w = what;
    // TEMP-TEST: VFXL_SELF_SHOT=<フォルダ> の時、「... look <名>」の行でこのフレームの画面を <フォルダ>/<名前の空白を _ に>.png へ
    // （2026-10-23。外のスクリプトはゲームが前面でないと撮れない = ユーザーが PC を使っている間は撮れなかった）
    {
        static char shotDir[260] = {};
        static const bool shot = GetEnvironmentVariableA("VFXL_SELF_SHOT", shotDir, sizeof(shotDir)) > 0;
        if (shot && w.find(" look ") != std::string::npos)
        {
            std::string name = w;
            for (char& c : name) if (c == ' ' || c == ':' || c == '/' || c == '\\') c = '_';
            const std::string path = std::string(shotDir) + "\\" + name + ".png";
            Application::Get().GetGraphics().RequestScreenshot(std::wstring(path.begin(), path.end()));
        }
    }
    // 自動テストの終わり（"... done"）には音の統計も（どの cue が何回鳴った / 間引かれたか。2026-10-03）
    if (w.size() >= 4 && w.compare(w.size() - 4, 4, "done") == 0)
        f << ms << " audio " << AudioSystem::Get().DebugStats() << std::endl;
}

// 選ばれた自動テスト（Debug/AutoTest/）を gameplay の最後に回す。経過時間はここで進める
void CollisionTestScene::UpdateAutoTest(float dt)
{
    if (!m_AutoRunner || m_AutoRunner->SelfClock()) return;   // SelfClock の物は Update から回り、時計も自分で進める
    m_AutoTime += dt;
    if (!m_Registry.IsValid(m_Player)) return;
    m_AutoRunner->UpdateGameplay(dt);
}
