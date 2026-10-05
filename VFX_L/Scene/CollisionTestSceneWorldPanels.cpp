    // ============================================================
// CollisionTestSceneWorldPanels.cpp
// CollisionTestScene: ImGui panels (Swarm (GPU) / Terrain)
// ============================================================
#include "Scene/CollisionTestScene.h"
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
#include <filesystem>
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
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <fstream>   // TEMP-TEST: autotest.log
#include <random>
#include <unordered_set>

void CollisionTestScene::DrawSwarmPanel()
{
    // ============================================================
    // GPU 側 gameplay（Swarm）
    // 位置や HP は GPU 上にしか無い。読めるのは counter だけ
    // ============================================================
    if (ImGui::CollapsingHeader("Swarm (GPU)", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto& c = m_Swarm.GetCounters();

        ImGui::Text("nearest  : dist %.2f  pos %.1f %.1f %.1f  key %08X",
            c.nearestDist, c.nearestPos[0], c.nearestPos[1], c.nearestPos[2], c.nearestKey);
        const auto& aim = m_WeaponSystem.GetAimDebug();
        ImGui::Text("aim      : %s  %s  range %.1f",
            aim.hasTarget ? "TARGET" : "none", aim.targetIsGpu ? "gpu" : "cpu", aim.range);

        ImGui::Text("alive proj    : %u", c.aliveProjectiles);
        ImGui::Text("alive enemy   : %u", c.aliveEnemies);
        ImGui::Text("kills (total) : %u", c.killCount);
        ImGui::Text("pending spawn : proj %d   enemy %d",
            m_Swarm.GetPendingProjSpawns(), m_Swarm.GetPendingEnemySpawns());
        ImGui::Text("sub steps     : %d   flush %.4f ms",
            m_Swarm.GetLastSubSteps(), m_Swarm.GetFlushMs());

        // ---- 雑魚の頭上の HP バー（見た目の調整）----
        if (ImGui::TreeNode("HP Bars"))
        {
            auto& bar = m_Swarm.hpBar;
            ImGui::Checkbox("Show", &bar.enabled);
            ImGui::DragFloat("Width (m)", &bar.width, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Height (m)", &bar.height, 0.005f, 0.02f, 1.0f);
            ImGui::DragFloat("Offset (m)", &bar.offset, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Border (m)", &bar.border, 0.001f, 0.0f, 0.1f);
            ImGui::ColorEdit4("Fill", &bar.fill.x);
            ImGui::ColorEdit4("Back", &bar.back.x);
            ImGui::ColorEdit4("Edge", &bar.edge.x);
            ImGui::TreePop();
        }

        // ---- 雑魚の足元の丸い影（太陽のシャドウマップには入れない代わり）----
        if (ImGui::TreeNode("Blob Shadows"))
        {
            auto& blob = m_Swarm.blobShadow;
            ImGui::Checkbox("Enabled##blob", &blob.enabled);
            ImGui::DragFloat("Radius (m)##blob", &blob.radius, 0.01f, 0.1f, 3.0f);
            ImGui::SliderFloat("Strength##blob", &blob.strength, 0.0f, 1.0f);
            ImGui::SliderFloat("Softness##blob", &blob.softness, 0.0f, 1.0f);
            ImGui::DragFloat("Lift (m)##blob", &blob.lift, 0.005f, 0.0f, 0.3f);
            ImGui::DragFloat("Height Clamp (m)##blob", &blob.clamp, 0.01f, 0.0f, 2.0f);
            ImGui::TreePop();
        }

        // ---- 死んだ敵の砕け散り（2026-10-02）----
        if (ImGui::TreeNode("Death Shatter"))
        {
            auto& cs = m_Swarm.corpse;
            ImGui::Checkbox("Enabled##corpse", &cs.enabled);
            ImGui::DragFloat("Life (s)##corpse", &cs.life, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Fling (m/s)##corpse", &cs.fling, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat("Up (m/s)##corpse", &cs.up, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat("Gravity##corpse", &cs.gravity, 0.1f, 0.1f, 60.0f);
            ImGui::DragFloat("Spin (rad/s)##corpse", &cs.spin, 0.1f, 0.0f, 40.0f);
            ImGui::SliderFloat("Bounce##corpse", &cs.bounce, 0.0f, 1.0f);
            ImGui::DragFloat("Flash##corpse", &cs.flash, 0.05f, 1.0f, 10.0f);
            ImGui::DragFloat("Flash Time (s)##corpse", &cs.flashTime, 0.005f, 0.0f, 0.5f);
            ImGui::SliderFloat("Shrink Start##corpse", &cs.shrinkStart, 0.0f, 1.0f);
            ImGui::TextDisabled("dust: area #%u (AreaData/MobDeath.json, VFXData/MobDeath.json)", cs.deathArea);
            ImGui::TreePop();
        }

        // ---- 液溜まり（Liquid entry。見た目は VFX の json、ここは描くかどうかだけ）----
        ImGui::Checkbox("Liquids (GPU areas)", &m_Swarm.liquids);
        ImGui::SameLine();
        ImGui::TextDisabled("look: Liquid entry in the area's VFX json (F2)");

        // ---- 隕石（DROP の弾）の着弾点の警告の輪 ----
        if (ImGui::TreeNode("Meteor Ring"))
        {
            auto& ring = m_Swarm.dropRing;
            ImGui::Checkbox("Enabled##dropRing", &ring.enabled);
            ImGui::DragFloat("Edge Width##dropRing", &ring.edgeWidth, 0.005f, 0.0f, 0.5f);
            ImGui::DragFloat("Lift##dropRing", &ring.lift, 0.005f, 0.0f, 0.3f);
            ImGui::ColorEdit4("Fill##dropRing", &ring.fill.x);
            ImGui::ColorEdit4("Edge##dropRing", &ring.edge.x);
            ImGui::ColorEdit4("Back##dropRing", &ring.back.x);
            ImGui::TreePop();
        }
        // ---- Boss のスラムの警告の輪（BossAttacks が置く）----
        if (ImGui::TreeNode("Boss Slam Ring"))
        {
            auto& ring = m_Swarm.warnRing;
            ImGui::Checkbox("Enabled##warnRing", &ring.enabled);
            ImGui::DragFloat("Edge Width##warnRing", &ring.edgeWidth, 0.005f, 0.0f, 0.5f);
            ImGui::DragFloat("Lift##warnRing", &ring.lift, 0.005f, 0.0f, 0.3f);
            ImGui::ColorEdit4("Fill##warnRing", &ring.fill.x);
            ImGui::ColorEdit4("Edge##warnRing", &ring.edge.x);
            ImGui::ColorEdit4("Back##warnRing", &ring.back.x);
            ImGui::TreePop();
        }

        // ---- 経験値オーブの見た目（宝石の動き・光り方・吸い寄せ中の尾）----
        if (ImGui::TreeNode("Exp Orbs"))
        {
            auto& ol = m_Swarm.orbLook;
            ImGui::DragFloat("Scale##orb", &ol.scale, 0.01f, 0.1f, 5.0f);
            ImGui::DragFloat("Bob height (m)", &ol.bobHeight, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Bob speed", &ol.bobSpeed, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat("Spin speed", &ol.spinSpeed, 0.05f, -20.0f, 20.0f);
            ImGui::DragFloat("Pulse amount", &ol.pulseAmount, 0.005f, 0.0f, 0.5f);
            ImGui::DragFloat("Pulse speed", &ol.pulseSpeed, 0.05f, 0.0f, 30.0f);
            ImGui::ColorEdit3("Idle color", &ol.idleColor.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit3("Pull color", &ol.pullColor.x, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Emissive", &ol.emissive, 0.02f, 0.0f, 10.0f);
            ImGui::SliderFloat("Facet", &ol.facet, 0.0f, 1.0f);
            ImGui::DragFloat("Rim gain", &ol.rimGain, 0.02f, 0.0f, 10.0f);
            ImGui::DragFloat("Rim power", &ol.rimPower, 0.05f, 0.1f, 16.0f);
            ImGui::ColorEdit3("Rim color", &ol.rimColor.x, ImGuiColorEditFlags_Float);
            ImGui::DragFloat("Glint gain", &ol.glintGain, 0.02f, 0.0f, 10.0f);
            ImGui::DragFloat("Glint power", &ol.glintPower, 0.5f, 1.0f, 256.0f);
            ImGui::SeparatorText("Pulled");
            ImGui::DragFloat("Full pull speed", &ol.fullPullSpeed, 0.1f, 0.1f, 50.0f);
            ImGui::DragFloat("Tilt max (rad)", &ol.tiltMax, 0.02f, 0.0f, 1.6f);
            ImGui::DragFloat("Stretch / speed", &ol.stretchPerSpeed, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Stretch max", &ol.stretchMax, 0.02f, 0.0f, 4.0f);
            ImGui::DragFloat("Pull glow", &ol.pullGlow, 0.02f, 0.0f, 10.0f);
            ImGui::Checkbox("Trail (ExpOrbTrail.json)", &ol.trail);
            ImGui::DragFloat("Trail min speed", &ol.trailMinSpeed, 0.1f, 0.0f, 50.0f);
            ImGui::TreePop();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Test Fire: alive proj should rise to ~100");
        ImGui::TextDisabled("then fall back to 0 within 3 seconds.");
        ImGui::Text("requested %u   dispatched %u   steps %u",
            m_Swarm.GetTotalRequested(), m_Swarm.GetTotalDispatched(), m_Swarm.GetTotalSteps());
        // ---- 範囲攻撃の確認：アイテムを持っていなくても、エディタのプロファイルを直接出せる ----
        ImGui::Text("alive areas %u   ticking %u   area vfx %zu",
            c.aliveAreas, c.tickingAreas, m_AreaVFX.GetActiveCount());
        if (AreaProfileDB::Count() > 1)
        {
            m_AreaTestProfile = (std::max)(1, (std::min)(m_AreaTestProfile, AreaProfileDB::Count() - 1));
            if (ImGui::BeginCombo("Area Profile", AreaProfileDB::At(m_AreaTestProfile).name.c_str()))
            {
                for (int i = 1; i < AreaProfileDB::Count(); ++i)
                    if (ImGui::Selectable(AreaProfileDB::At(i).name.c_str(), i == m_AreaTestProfile))
                        m_AreaTestProfile = i;
                ImGui::EndCombo();
            }

            const bool hasPlayer = m_Registry.IsValid(m_Player);
            auto castAt = [&](const Vector3& pos, bool atCaster)
                {
                    const AreaProfile& ap = AreaProfileDB::At(m_AreaTestProfile);
                    const Swarm::Area ar = ap.MakeArea(pos, atCaster);
                    m_Swarm.SpawnArea(ar);
                    m_AreaVFX.Play(ap.vfxFile, pos, ar.timeLeft,
                        (ar.flags & Swarm::kAreaFollowPlayer) != 0, m_VFXContext);
                };

            if (ImGui::Button("Cast at Nearest Enemy"))
            {
                Vector3 np, nv; float nd;
                if (m_Swarm.GetNearestEnemy(np, nv, nd)) castAt(np, false);
            }
            ImGui::SameLine();
            if (ImGui::Button("Cast at Player") && hasPlayer)
                castAt(m_Registry.Get<TransformComponent>(m_Player).position, true);
        }
        else
        {
            ImGui::TextDisabled("no area profile yet (make one in the Projectile Editor, F4)");
        }

        // 生成経路が通っているかの最短確認。プレイヤーの周りへ放射状に撃つ
        if (ImGui::Button("Test Fire 100"))
        {
            if (m_Registry.IsValid(m_Player))
            {
                const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position
                    + Vector3(0.0f, 1.0f, 0.0f);
                for (int i = 0; i < 100; ++i)
                {
                    const float a = 6.2831853f * (float)i / 100.0f;
                    Vector3 dir(std::cos(a), 0.0f, std::sin(a));
                    m_Swarm.SpawnProjectile(VFXId::Fireball, pp, dir * 20.0f, 10.0f, 0.25f, 3.0f);
                }
            }
        }
    }
}

void CollisionTestScene::DrawTerrainPanel()
{
    // ---------- 地形 ----------
    if (ImGui::CollapsingHeader("Terrain"))
    {
        ImGui::Text("Grid : %d x %d  (%.0fm x %.0fm)",
            m_Grid.Width(), m_Grid.Depth(),
            m_Grid.WorldWidth(), m_Grid.WorldDepth());
        TerrainSurface::Get().DrawImGui();   // 地面のテクスチャ（2026-10-03）

        auto& tc = m_TerrainConfig;
        int seed = (int)tc.seed;
        if (ImGui::InputInt("Seed", &seed)) tc.seed = (uint32_t)(seed < 0 ? 0 : seed);
        ImGui::SameLine();
        if (ImGui::Button("Random")) tc.seed = std::random_device{}() % 100000u;

        // 三層（山頂・平原・洞窟）。隅の組は seed で決まる
        ImGui::SeparatorText("Layers (summit / plain / mine)");
        ImGui::Checkbox("Layers", &tc.layers);
        ImGui::Text("summit cells %d   mine cells %d   mine deep %s",
            (int)m_TerrainLayout.summitCells.size(), (int)m_TerrainLayout.mineCells.size(),
            m_TerrainLayout.hasMineDeep ? "yes" : "no");
        ImGui::DragInt("Summit Size", &tc.summitSize, 0.2f, 10, 70);
        ImGui::DragFloat("Summit Height", &tc.summitHeight, 0.1f, 2.0f, 40.0f, "%.1f m");
        ImGui::DragInt("Summit Ramps", &tc.summitRamps, 0.1f, 0, 6);
        ImGui::DragInt("Summit Ramp Width", &tc.summitRampWidth, 0.1f, 1, 12);
        ImGui::DragFloatRange2("Summit Ramp Slope", &tc.summitRampSlopeMin, &tc.summitRampSlopeMax, 0.1f, 5.0f, 35.0f, "%.0f deg");
        ImGui::DragInt("Mine Size", &tc.mineSize, 0.2f, 10, 70);
        ImGui::DragFloat("Mine Depth", &tc.mineDepth, 0.1f, 2.0f, 30.0f, "%.1f m");
        ImGui::DragInt("Mine Ramps", &tc.mineRamps, 0.1f, 0, 6);
        ImGui::DragInt("Mine Ramp Width", &tc.mineRampWidth, 0.1f, 1, 8);
        ImGui::SliderFloat("Mine Ramp Slope", &tc.mineRampSlope, 10.0f, 35.0f, "%.0f deg");
        ImGui::DragInt("Mine Rocks", &tc.mineRockCount, 0.5f, 0, 200);
        // 洞窟の屋根（坑を岩の塊で覆う。口は下り坂の上端）
        ImGui::Checkbox("Mine Roof (cave)", &tc.mineRoof);
        ImGui::DragFloat("Roof Bottom", &tc.roofBottom, 0.1f, 2.5f, 20.0f, "%.1f m");
        ImGui::SetItemTooltip("Above the plain. Cave height = this + mine depth, mouth height = this");
        ImGui::DragFloat("Roof Top", &tc.roofTop, 0.1f, 3.0f, 30.0f, "%.1f m");
        ImGui::DragFloat("Roof Collision Top", &tc.roofCollisionTop, 0.5f, 5.0f, 100.0f, "%.0f m");
        ImGui::DragFloatRange2("Roof Rocks", &tc.roofRockMin, &tc.roofRockMax, 0.1f, 0.0f, 40.0f, "%.1f m");
        ImGui::DragInt("Cave Torch Spacing", &tc.caveTorchSpacing, 0.1f, 1, 20);
        // フローフィールドの探索の打ち切り（経路長のマス数。0 = 全域）。フィールドが広いので遠くは直線追跡で構わない
        if (ImGui::DragInt("Flow Range (cells)", &m_Swarm.GetFlowField().maxRangeCells, 0.5f, 0, 300))
            m_Swarm.RequestFlowRebuild();
        ImGui::SetItemTooltip("0 = whole map. Enemies farther than this (path length) chase in a straight line");
        ImGui::SeparatorText("Plain");
        ImGui::DragInt("Plateaus", &tc.plateauCount, 0.2f, 0, 40);
        ImGui::DragIntRange2("Plateau Size", &tc.plateauMin, &tc.plateauMax, 0.2f, 3, 20);
        ImGui::DragFloatRange2("Plateau Height", &tc.heightMin, &tc.heightMax, 0.05f, 1.0f, 8.0f);
        ImGui::DragInt("Plateau Gap", &tc.gap, 0.1f, 1, 10);
        ImGui::SliderFloat("2nd Tier Chance", &tc.tier2Chance, 0.0f, 1.0f);
        ImGui::DragFloatRange2("2nd Tier Height", &tc.tier2HeightMin, &tc.tier2HeightMax, 0.05f, 1.0f, 6.0f);
        ImGui::DragInt("Ramp Width", &tc.rampWidth, 0.1f, 1, 5);
        ImGui::SliderFloat("Ramp Slope", &tc.rampSlopeDeg, 10.0f, 35.0f, "%.0f deg");
        ImGui::DragInt("Terraces", &tc.terraceCount, 0.1f, 0, 10);
        ImGui::DragFloatRange2("Terrace Height", &tc.terraceHeightMin, &tc.terraceHeightMax, 0.05f, 1.0f, 12.0f);
        ImGui::DragFloatRange2("Terrace Slope", &tc.terraceSlopeMin, &tc.terraceSlopeMax, 0.1f, 5.0f, 35.0f, "%.0f deg");
        ImGui::DragIntRange2("Terrace Top", &tc.terraceTopMin, &tc.terraceTopMax, 0.1f, 2, 16);
        ImGui::SliderFloat("Terrace 2nd Tier", &tc.terraceTier2Chance, 0.0f, 1.0f);
        ImGui::DragFloatRange2("Terrace 2nd Height", &tc.terraceTier2HeightMin, &tc.terraceTier2HeightMax, 0.05f, 1.0f, 8.0f);
        ImGui::DragInt("Terrace Clear", &tc.terraceClear, 0.1f, 1, 12);
        ImGui::DragInt("Spawn Clear", &tc.spawnClearRadius, 0.1f, 2, 20);
        ImGui::DragInt("Trees", &tc.treeCount, 1.0f, 0, 400);
        ImGui::DragInt("Rocks", &tc.rockCount, 1.0f, 0, 200);
        ImGui::DragInt("Bushes", &tc.bushCount, 1.0f, 0, 600);
        // これより小さい木・岩は見た目だけ（格子も衝突も無し）
        ImGui::DragFloat("Tree blocks if height >=", &tc.treeBlockMinHeight, 0.05f, 0.0f, 20.0f, "%.2f m");
        ImGui::DragFloat("Rock blocks if size >=", &tc.rockBlockMinSize, 0.05f, 0.0f, 10.0f, "%.2f m");
        ImGui::Checkbox("Rock Mountains (outer wall)", &tc.rockMountains);
        ImGui::DragFloat("Mountain Scale", &tc.mountainScale, 0.02f, 0.2f, 3.0f);
        // 一番手前の列の岩：縁から内に入る量（負 = 外）。内に入る岩だけ衝突が付く
        ImGui::DragFloatRange2("Edge Rock Intrude", &tc.edgeRockIntrudeMin, &tc.edgeRockIntrudeMax, 0.05f, -3.0f, 4.0f, "%.2f m");
        ImGui::DragFloat("Edge Rock Collider x", &tc.edgeRockShrink, 0.01f, 0.5f, 1.2f);
        ImGui::DragFloat("Ruin Column Intrude", &tc.ruinColumnIntrude, 0.01f, 0.0f, 1.0f);

        // 起伏（2026-10-04）：緩い丘 + 小さい土饅頭。台地・坂の足元は台座で均す
        ImGui::SeparatorText("Relief (rolling ground)");
        ImGui::Checkbox("Relief", &tc.relief);
        ImGui::DragFloat("Hill Height", &tc.hillHeight, 0.05f, 0.0f, 8.0f, "%.2f m");
        ImGui::DragFloat("Hill Scale", &tc.hillScale, 0.5f, 8.0f, 200.0f, "%.0f m");
        ImGui::DragFloat("Detail Height", &tc.hillDetailHeight, 0.02f, 0.0f, 3.0f, "%.2f m");
        ImGui::DragFloat("Detail Scale", &tc.hillDetailScale, 0.2f, 4.0f, 60.0f, "%.0f m");
        ImGui::DragFloat("Bumps / 200m^2", &tc.bumpCount, 1.0f, 0.0f, 400.0f, "%.0f");
        ImGui::DragFloatRange2("Bump Radius", &tc.bumpRadiusMin, &tc.bumpRadiusMax, 0.05f, 0.5f, 12.0f, "%.1f m");
        ImGui::DragFloatRange2("Bump Height", &tc.bumpHeightMin, &tc.bumpHeightMax, 0.02f, 0.0f, 4.0f, "%.2f m");
        ImGui::DragFloat("Bump Max Slope", &tc.bumpMaxSlope, 0.01f, 0.05f, 0.8f);
        ImGui::SetItemTooltip("Bump height <= radius x this (steepest part ~ 1.54 x height / radius)");
        ImGui::DragFloat("Summit Relief x", &tc.summitReliefMul, 0.02f, 0.0f, 2.0f);
        ImGui::SliderInt("Pad Margin (cells)", &tc.padMargin, 1, 10);
        ImGui::DragFloat("Max Slope", &tc.reliefMaxSlopeDeg, 0.5f, 10.0f, 45.0f, "%.1f deg");
        ImGui::SetItemTooltip("Per-axis limit between neighbour nodes (diagonal up to 1.41x). Mobs walk up to 40 deg");
        ImGui::SetItemTooltip("How far the flattened ground under plateaus / ramps blends back into the hills");
        ImGui::SliderInt("Mesh Subdiv", &tc.reliefSubdiv, 1, 4);

        if (ImGui::Button("Regenerate"))
        {
            // seed と上の設定から作り直す（読み込んだ地図は外す）。中身は CollisionTestSceneTerrain.cpp
            m_MapFile.clear();
            MapData::PlayOverride().clear();
            RebuildTerrain();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("same seed = same map");

        // ---- 地図のデータ（2026-10-05、World/MapData）----
        // 今の地形（生成した物でも読んだ物でも）をそのまま保存し、読み込めば乱数なしで同じ地図になる
        ImGui::SeparatorText("Map Data (Assets/Data/MapData/<name>.vmap)");
        ImGui::Text("Current : %s", m_MapLoaded ? m_MapFile.c_str() : "(generated from seed)");
        ImGui::Text("%zu boxes, %zu hulls, %zu visuals, %zu props, %zu blocks",
            m_TerrainMap.boxes.size(), m_TerrainMap.hulls.size(), m_TerrainMap.visuals.size(),
            m_TerrainMap.props.size(), m_TerrainMap.blocks.size());
        ImGui::InputText("Map Name", m_MapNameBuf, sizeof(m_MapNameBuf));
        if (ImGui::Button("Save Map") && m_MapNameBuf[0])
        {
            BakePlacements();   // 地図にまだ無ければ、今の箱と Boss の門の位置を入れる（F6 で動かせる）
            MapData::Save(m_MapNameBuf, m_TerrainMap);
        }
        ImGui::TextDisabled("Edit props / crates / boss gate in F6 -> Battle Map");
        ImGui::SameLine();
        if (ImGui::Button("Load Map") && m_MapNameBuf[0])
        {
            m_MapFile = m_MapNameBuf;
            RebuildTerrain();   // 読めなければ seed から生成する（Current の表示で分かる）
        }
        ImGui::SetItemTooltip("Per-stage maps: StageDef::mapFile (World/StageConfig). VFXL_MAP=<name> overrides at start");

        // 木・岩・茂み・草の描画（モデル毎の instanced + 視錐台 / 距離の間引き）
        ImGui::Separator();
        m_StaticProps.DrawImGui();
    }
}
