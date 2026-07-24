// ============================================================
// CollisionTestScene.cpp
// ============================================================
#include "CollisionTestScene.h"

#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "RigidbodyComponent.h"
#include "ModelComponent.h"
#include "PlayerTag.h"
#include "WandComponent.h"
#include "HealthComponent.h"
#include "View.h"

#include "TestSpawner.h"
#include "PrimitiveBuilder.h"
#include "DebugManager.h"
#include "InputManager.h"
#include "Application.h"
#include "Model.h"
#include "imgui.h"
#include <iostream>

// ============================================================
// Init
// ============================================================
void CollisionTestScene::Init()
{
    std::cout << "[CollisionTestScene] Init" << std::endl;

    auto* device = Application::Get().GetGraphics().GetDevice();

    // ---------- Camera ----------
    m_Camera.Init(45.0f, 1600.0f / 900.0f, 0.1f, 10000.0f);
    SetCamera(&m_Camera);

    // ---------- 共有モデル ----------
    m_EnemyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 1.0f, 0.35f, 0.35f, 1 });
    m_ProjectileModel = PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.9f, 0.3f, 1 });
    m_WeaponSystem.SetProjectileModel(m_ProjectileModel);

    // ---------- 地形 ----------
    struct TerrainDef { Vector3 pos; Vector3 half; Vector4 color; };
    TerrainDef defs[] = {
        { { 0.0f,  0.0f, 0.0f }, { 10.0f, 0.5f, 10.0f }, { 0.45f, 0.45f, 0.50f, 1 } }, // 床
        { { 5.0f,  1.0f, 0.0f }, {  1.0f, 1.0f,  1.0f }, { 0.70f, 0.45f, 0.30f, 1 } }, // 高い段差
        { {-5.0f, 0.75f, 3.0f }, {  1.0f, 0.75f, 1.0f }, { 0.30f, 0.60f, 0.40f, 1 } }, // 低い段差
        { { 0.0f,  1.5f,-7.0f }, {  3.0f, 1.5f,  0.5f }, { 0.55f, 0.50f, 0.60f, 1 } }, // 壁
    };
    for (const auto& d : defs)
    {
        Entity e = TestSpawner::SpawnStaticBox(m_Registry, d.pos, d.half);
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateBox(device, d.half, d.color);
        m_Registry.Add<ModelComponent>(e, mc);
        m_Terrain.push_back(e);
    }

    // ---------- プレイヤー ----------
    m_Player = TestSpawner::SpawnCapsule(m_Registry,
        { m_SpawnPos[0], m_SpawnPos[1], m_SpawnPos[2] },
        m_PlayerRadius, m_PlayerHeight);
    m_Registry.Add<PlayerTag>(m_Player, {});
    m_Registry.Add<WandComponent>(m_Player, {});   // 杖を持たせる

    ModelComponent pmc;
    pmc.model = PrimitiveBuilder::CreateCapsule(device, m_PlayerRadius, m_PlayerHeight,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
    m_Registry.Add<ModelComponent>(m_Player, pmc);

    // ---------- 敵 ----------
    RespawnEnemies();

    std::cout << "[CollisionTestScene] Init complete" << std::endl;
}

// ============================================================
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// Update
// ============================================================
void CollisionTestScene::Update(float dt)
{
    SceneBase::Update(dt);

    // ---- System 実行順（重要）----
    // 操作 → 衝突収集 → 物理 → 杖発射 → 投射物 → ダメージ → カメラ
    m_PlayerControlSystem.SetMoveSpeed(m_MoveSpeed);
    m_PlayerControlSystem.SetJumpPower(m_JumpPower);
    m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);
    m_ProjectileSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ---- 命中イベント消費：ダメージ + 死亡 ----
    for (const auto& hit : m_ProjectileSystem.GetHitEvents())
    {
        if (!m_Registry.IsValid(hit.target)) continue;
        if (!m_Registry.Has<HealthComponent>(hit.target)) continue;

        auto& hp = m_Registry.Get<HealthComponent>(hit.target);
        hp.current -= hit.damage;
        if (hp.IsDead())
            m_Registry.Destroy(hit.target);
    }

    // ---- カメラ追従（物理でプレイヤーが動いた後）----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);

    // ---- 衝突体の線框表示 ----
    if (m_ShowWireframe)
    {
        const auto& pairs = m_CollisionSystem.GetPairs();
        auto isHitting = [&](Entity e) -> bool
            {
                for (const auto& p : pairs)
                    if (p.a == e || p.b == e) return true;
                return false;
            };

        m_Registry.CreateView<TransformComponent, ColliderComponent>()
            .Each([&](Entity e, TransformComponent&, ColliderComponent&)
                {
                    Color col = isHitting(e) ? Color(1.0f, 0.3f, 0.3f, 1.0f)
                        : Color(0.4f, 1.0f, 0.4f, 1.0f);
                    DrawColliderDebug(e, col);
                });

        // 杖の索敵範囲を可視化
        if (m_Registry.IsValid(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            auto& w = m_Registry.Get<WandComponent>(m_Player);
            DebugManager::Get().DrawWireSphere(tf.position + w.muzzleOffset, w.range,
                Color(0.3f, 0.5f, 1.0f, 1.0f));
        }
    }

    DrawDebugUI();
}

// ============================================================
// Render
// ============================================================
void CollisionTestScene::Render(Renderer& renderer)
{
    renderer.SetDirectionalLight(
        { m_LightDir[0], m_LightDir[1], m_LightDir[2] },
        { m_LightColor[0], m_LightColor[1], m_LightColor[2] },
        m_LightIntensity);
    renderer.SetAmbientColor({ m_AmbientColor[0], m_AmbientColor[1], m_AmbientColor[2] });

    SceneBase::Render(renderer);

    if (m_ShowMesh)
        m_RenderSystem.Render(m_Registry, renderer);
}

// ============================================================
// 衝突体の線框描画
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
    }
}

// ============================================================
// プレイヤーのメッシュと衝突体を作り直す
// ============================================================
void CollisionTestScene::RebuildPlayerMesh()
{
    if (!m_Registry.IsValid(m_Player)) return;
    auto* device = Application::Get().GetGraphics().GetDevice();

    auto& col = m_Registry.Get<ColliderComponent>(m_Player);
    col.radius = m_PlayerRadius;
    col.height = m_PlayerHeight;

    auto& mc = m_Registry.Get<ModelComponent>(m_Player);
    mc.model = PrimitiveBuilder::CreateCapsule(device, m_PlayerRadius, m_PlayerHeight,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
}

// ============================================================
// 敵を1体生成
// ============================================================
void CollisionTestScene::SpawnEnemy(const Vector3& pos)
{
    Entity e = TestSpawner::SpawnCapsule(m_Registry, pos, 0.4f, 1.0f);

    // TestSpawner の既定は Player 層なので敵層に上書き
    auto& col = m_Registry.Get<ColliderComponent>(e);
    col.layer = Layer_Enemy;
    col.mask = Layer_All;

    m_Registry.Add<HealthComponent>(e, {});

    ModelComponent mc;
    mc.model = m_EnemyModel;
    m_Registry.Add<ModelComponent>(e, mc);

    m_Enemies.push_back(e);
}

// ============================================================
// 敵を全部作り直す
// ============================================================
void CollisionTestScene::RespawnEnemies()
{
    for (Entity e : m_Enemies)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_Enemies.clear();

    SpawnEnemy({ 7.0f, 3.0f,  3.0f });
    SpawnEnemy({ -7.0f, 3.0f,  4.0f });
    SpawnEnemy({ 3.0f, 3.0f, -6.0f });
    SpawnEnemy({ -3.0f, 3.0f, -3.0f });
}

// ============================================================
// ImGui
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    ImGui::Begin("Game Test");

    // ---------- 表示 ----------
    ImGui::Checkbox("Show Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::Checkbox("Show Wireframe", &m_ShowWireframe);
    ImGui::Separator();

    // ---------- 杖 ----------
    if (ImGui::CollapsingHeader("Wand", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (m_Registry.IsValid(m_Player) && m_Registry.Has<WandComponent>(m_Player))
        {
            auto& w = m_Registry.Get<WandComponent>(m_Player);

            char buf[64];
            sprintf_s(buf, "%.0f / %.0f", w.manaCurrent, w.manaMax);
            ImGui::ProgressBar(w.manaCurrent / w.manaMax, ImVec2(-1, 0), buf);

            ImGui::DragFloat("Cast Interval", &w.castInterval, 0.01f, 0.05f, 3.0f);
            ImGui::DragFloat("Mana Cost", &w.manaCost, 0.5f, 0.0f, 100.0f);
            ImGui::DragFloat("Mana Regen", &w.manaRegen, 0.5f, 0.0f, 200.0f);
            ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);
            ImGui::DragFloat("Proj Speed", &w.projectileSpeed, 0.5f, 1.0f, 100.0f);
            ImGui::DragFloat("Damage", &w.projectileDamage, 0.5f, 0.0f, 200.0f);

            // 持続可能かの即時フィードバック
            float drain = (w.castInterval > 0.0f) ? w.manaCost / w.castInterval : 0.0f;
            bool sustainable = drain <= w.manaRegen;
            ImGui::TextColored(sustainable ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
                "Drain %.1f/s vs Regen %.1f/s  %s",
                drain, w.manaRegen, sustainable ? "(sustainable)" : "(will run dry)");
        }
    }

    // ---------- 敵 ----------
    if (ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int alive = 0;
        for (Entity e : m_Enemies)
        {
            if (!m_Registry.IsValid(e)) continue;
            ++alive;
            auto& hp = m_Registry.Get<HealthComponent>(e);
            ImGui::Text("Enemy %u : HP %.0f / %.0f", e, hp.current, hp.max);
        }
        ImGui::Text("Alive : %d", alive);
        if (ImGui::Button("Respawn Enemies")) RespawnEnemies();
    }

    // ---------- プレイヤー状態 ----------
    if (ImGui::CollapsingHeader("Player State"))
    {
        if (m_Registry.IsValid(m_Player))
        {
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            auto& rb = m_Registry.Get<RigidbodyComponent>(m_Player);

            ImGui::Text("Position : %.2f, %.2f, %.2f", tf.position.x, tf.position.y, tf.position.z);
            ImGui::Text("Velocity : %.2f, %.2f, %.2f", rb.velocity.x, rb.velocity.y, rb.velocity.z);
            ImGui::TextColored(rb.isGrounded ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.6f, 0.3f, 1),
                "Grounded : %s", rb.isGrounded ? "YES" : "no");

            ImGui::DragFloat3("Spawn Pos", m_SpawnPos, 0.1f);
            if (ImGui::Button("Reset to Spawn"))
            {
                tf.position = { m_SpawnPos[0], m_SpawnPos[1], m_SpawnPos[2] };
                rb.velocity = { 0, 0, 0 };
            }
        }
    }

    // ---------- プレイヤー設定 ----------
    if (ImGui::CollapsingHeader("Player Settings"))
    {
        bool dirty = false;
        dirty |= ImGui::DragFloat("Radius", &m_PlayerRadius, 0.01f, 0.05f, 3.0f);
        dirty |= ImGui::DragFloat("Height", &m_PlayerHeight, 0.01f, 0.05f, 5.0f);
        dirty |= ImGui::ColorEdit3("Color", m_PlayerColor);
        if (dirty) RebuildPlayerMesh();

        ImGui::DragFloat("Move Speed", &m_MoveSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Jump Power", &m_JumpPower, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Gravity", &m_Gravity, 0.5f, -100.0f, 0.0f);
    }

    // ---------- カメラ ----------
    if (ImGui::CollapsingHeader("Camera"))
    {
        ImGui::DragFloat("Distance", &m_Camera.distance, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Height", &m_Camera.height, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Stick Sens", &m_Camera.stickSensitivity, 1.0f, 10.0f, 500.0f);
        ImGui::DragFloat("Mouse Sens", &m_Camera.mouseSensitivity, 0.01f, 0.01f, 1.0f);
        ImGui::Checkbox("Invert Y", &m_Camera.invertY);
        ImGui::Text("Yaw/Pitch : %.1f / %.1f", m_Camera.GetYaw(), m_Camera.GetPitch());
    }

    // ---------- ライト ----------
    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::DragFloat3("Direction", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", m_LightColor);
        ImGui::DragFloat("Intensity", &m_LightIntensity, 0.02f, 0.0f, 5.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
    }

    // ---------- 統計 ----------
    ImGui::Separator();
    ImGui::Text("Colliders : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs     : %zu", m_CollisionSystem.GetPairs().size());

    ImGui::End();
}