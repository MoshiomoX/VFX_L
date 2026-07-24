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
#include "SpellID.h"
#include "HealthComponent.h"
#include "View.h"
#include "ProjectileComponent.h"
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
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.4f, 1.0f, 1 });
    // 投射物モデル（種類ごとに登録）
    m_WeaponSystem.SetProjectileModel(ItemID::Fireball,
        PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.9f, 0.3f, 1 }));
    m_WeaponSystem.SetProjectileModel(ItemID::Lightning,
        PrimitiveBuilder::CreateSphere(device, 0.18f, { 0.5f, 0.8f, 1.0f, 1 }));
    m_StressModel = PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.5f, 0.2f, 1 });
    // ---------- 地形 ----------
    struct TerrainDef { Vector3 pos; Vector3 half; Vector4 color; };
    TerrainDef defs[] = {
        { { 0.0f,  0.0f, 0.0f }, { 12.0f, 0.5f, 12.0f }, { 0.45f, 0.45f, 0.50f, 1 } }, // 床
        { { 6.0f,  1.0f, 0.0f }, {  1.0f, 1.0f,  1.0f }, { 0.70f, 0.45f, 0.30f, 1 } }, // 高い段差
        { {-6.0f, 0.75f, 3.0f }, {  1.0f, 0.75f, 1.0f }, { 0.30f, 0.60f, 0.40f, 1 } }, // 低い段差
        { { 0.0f,  1.5f,-9.0f }, {  4.0f, 1.5f,  0.5f }, { 0.55f, 0.50f, 0.60f, 1 } }, // 壁
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

    ModelComponent pmc;
    pmc.model = PrimitiveBuilder::CreateCapsule(device, m_PlayerRadius, m_PlayerHeight,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
    m_Registry.Add<ModelComponent>(m_Player, pmc);

    // ---------- 杖（第1版は spells を手動構築。後でバックパック集約に置き換える）----------
    WandComponent wand;
    {
        SpellStats fire;
        fire.id = ItemID::Fireball;
        fire.damage = 10.0f;
        fire.castInterval = 0.5f;
        fire.manaCost = 10.0f;
        wand.spells.push_back(fire);
    }
    m_Registry.Add<WandComponent>(m_Player, wand);

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

    for (const auto& hit : m_ProjectileSystem.GetHitEvents())
    {
        if (!m_Registry.IsValid(hit.target)) continue;
        if (!m_Registry.Has<HealthComponent>(hit.target)) continue;

        auto& hp = m_Registry.Get<HealthComponent>(hit.target);
        hp.current -= hit.damage;

        // 無敵の的は 0 で止める（数値が無限に減るのを防ぐ）
        if (hp.invincible && hp.current < 0.0f)
            hp.current = 0.0f;

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
    }

    // ---- 施法の可視化 ----
    if (m_ShowWandDebug)
        DrawWandDebug();

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
// 施法の可視化：索敵範囲 / 照準 / 分裂の扇形 / 連射待ち
// ============================================================
void CollisionTestScene::DrawWandDebug()
{
    if (!m_Registry.IsValid(m_Player)) return;
    if (!m_Registry.Has<WandComponent>(m_Player)) return;

    auto& dbg = DebugManager::Get();
    auto& wand = m_Registry.Get<WandComponent>(m_Player);
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    const auto& aim = m_WeaponSystem.GetAimDebug();

    // --- 索敵範囲（薄い青の球）---
    Vector3 muzzle = tf.position + wand.muzzleOffset;
    dbg.DrawWireSphere(muzzle, wand.range, Color(0.25f, 0.4f, 0.8f, 1.0f));

    if (!aim.hasTarget) return;

    // --- 目標マーク（黄色の十字）---
    if (m_Registry.IsValid(aim.target))
    {
        Vector3 tp = m_Registry.Get<TransformComponent>(aim.target).position;
        if (m_Registry.Has<ColliderComponent>(aim.target))
            tp += m_Registry.Get<ColliderComponent>(aim.target).offset;

        const float m = 0.6f;
        Color mark(1.0f, 1.0f, 0.2f, 1.0f);
        dbg.AddDebugLine(tp - Vector3(m, 0, 0), tp + Vector3(m, 0, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, m, 0), tp + Vector3(0, m, 0), mark);
        dbg.AddDebugLine(tp - Vector3(0, 0, m), tp + Vector3(0, 0, m), mark);
    }

    // --- 出力源ごとに分裂の扇形を描く（実際の発射計算と同じ）---
    for (size_t i = 0; i < wand.spells.size(); ++i)
    {
        const auto& s = wand.spells[i];

        // 出力源ごとに高さをずらして重なりを避ける
        Vector3 origin = aim.muzzle + Vector3(0.0f, (float)i * 0.22f, 0.0f);

        // 連射待ち中は橙、通常は緑
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

        // 連射の残り回数を短い縦線で表示
        for (int k = 0; k < s.pendingCasts; ++k)
        {
            Vector3 p = origin + aim.dir * 0.4f + Vector3(0.15f * (float)k, 0.0f, 0.0f);
            dbg.AddDebugLine(p, p + Vector3(0.0f, 0.18f, 0.0f), Color(1.0f, 0.5f, 0.1f, 1.0f));
        }
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
// 敵を1体生成（invincible = テスト用の不死の的）
// ============================================================
void CollisionTestScene::SpawnEnemy(const Vector3& pos, bool invincible)
{
    Entity e = TestSpawner::SpawnCapsule(m_Registry, pos, 0.4f, 1.0f);

    // TestSpawner の既定は Player 層なので敵層に上書き
    auto& col = m_Registry.Get<ColliderComponent>(e);
    col.layer = Layer_Enemy;
    col.mask = Layer_All;

    HealthComponent hp;
    hp.invincible = invincible;
    if (invincible)
    {
        // バーが減りきらないように大きめに取る（累計ダメージ計測用）
        hp.max = 9999.0f;
        hp.current = 9999.0f;
    }
    m_Registry.Add<HealthComponent>(e, hp);

    ModelComponent mc;
    mc.model = invincible ? m_DummyModel : m_EnemyModel;
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

    // 無敵の的（射撃テスト用。累計ダメージで DPS を見る）
    SpawnEnemy({ 0.0f, 3.0f, 8.0f }, true);

    // 通常の敵
    SpawnEnemy({ 8.0f, 3.0f,  4.0f } ,false);
    SpawnEnemy({ -8.0f, 3.0f,  5.0f },false);
    SpawnEnemy({ 4.0f, 3.0f, -7.0f } ,false);
}
// ============================================================
// 負荷テスト：投射物を一気に生成して限界を測る
// ============================================================
void CollisionTestScene::StressSpawnProjectiles(int count)
{
    if (!m_Registry.IsValid(m_Player)) return;
    Vector3 origin = m_Registry.Get<TransformComponent>(m_Player).position + Vector3(0, 2.0f, 0);

    for (int i = 0; i < count; ++i)
    {
        // 全方位にランダムな向きで飛ばす
        float a = (float)rand() / RAND_MAX * 6.2831853f;
        float b = (float)rand() / RAND_MAX * 6.2831853f;
        Vector3 dir(std::cos(a) * std::cos(b), std::sin(b) * 0.3f, std::sin(a) * std::cos(b));
        dir.Normalize();

        Entity p = m_Registry.Create();

        TransformComponent tf;
        tf.position = origin;
        m_Registry.Add<TransformComponent>(p, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Sphere;
        col.radius = 0.25f;
        col.layer = Layer_PlayerShot;
        col.mask = Layer_Enemy | Layer_Terrain;
        m_Registry.Add<ColliderComponent>(p, col);

        ProjectileComponent pj;
        pj.velocity = dir * 8.0f;
        pj.damage = 1.0f;
        pj.lifetime = 30.0f;      // 長寿命にして数を維持する
        m_Registry.Add<ProjectileComponent>(p, pj);

        if (m_StressModel)
        {
            ModelComponent mc;
            mc.model = m_StressModel;
            m_Registry.Add<ModelComponent>(p, mc);
        }
    }
}
// ============================================================
// ImGui
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    ImGui::Begin("Game Test");

    // ---------- 表示切替 ----------
    ImGui::Checkbox("Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::Checkbox("Collider", &m_ShowWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Wand Debug", &m_ShowWandDebug);
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

            ImGui::DragFloat("Mana Max", &w.manaMax, 1.0f, 10.0f, 1000.0f);
            ImGui::DragFloat("Mana Regen", &w.manaRegen, 0.5f, 0.0f, 300.0f);
            ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);
            ImGui::Separator();

            // --- 出力源ごとの編集 ---
            float totalDrain = 0.0f;
            for (size_t i = 0; i < w.spells.size(); ++i)
            {
                auto& s = w.spells[i];
                ImGui::PushID((int)i);

                const char* name = (s.id == ItemID::Fireball) ? "Fireball" : "Lightning";
                if (ImGui::TreeNodeEx("spell", ImGuiTreeNodeFlags_DefaultOpen,
                    "[%d] %s   split x%d / cast x%d", (int)i, name,
                    s.projectileCount, s.castCount))
                {
                    ImGui::SliderInt("Split (shots)", &s.projectileCount, 1, 8);
                    ImGui::DragFloat("Spread Angle", &s.spreadAngle, 1.0f, 0.0f, 180.0f);
                    ImGui::SliderInt("Double (casts)", &s.castCount, 1, 5);
                    ImGui::DragFloat("Cast Delay", &s.castDelay, 0.01f, 0.02f, 1.0f);
                    ImGui::Separator();
                    ImGui::DragFloat("Cast Interval", &s.castInterval, 0.01f, 0.05f, 3.0f);
                    ImGui::DragFloat("Mana Cost", &s.manaCost, 0.5f, 0.0f, 100.0f);
                    ImGui::DragFloat("Damage", &s.damage, 0.5f, 0.0f, 200.0f);
                    ImGui::DragFloat("Speed", &s.speed, 0.5f, 1.0f, 100.0f);
                    ImGui::DragFloat("Lifetime", &s.lifetime, 0.1f, 0.2f, 20.0f);
                    ImGui::Text("pending=%d  timer=%.2f", s.pendingCasts, s.castTimer);
                    ImGui::TreePop();
                }

                // この出力源の消費レート（二重釈放は castCount 倍）
                if (s.castInterval > 0.0f)
                    totalDrain += (s.manaCost * (float)s.castCount) / s.castInterval;

                ImGui::PopID();
            }

            // --- 追加 / 削除 ---
            if (ImGui::Button("+ Fireball"))
            {
                SpellStats s;
                s.id = ItemID::Fireball;
                w.spells.push_back(s);
            }
            ImGui::SameLine();
            if (ImGui::Button("+ Lightning"))
            {
                SpellStats s;
                s.id = ItemID::Lightning;
                s.damage = 25.0f;
                s.manaCost = 25.0f;
                s.castInterval = 1.0f;
                s.speed = 35.0f;
                s.radius = 0.18f;
                w.spells.push_back(s);
            }
            ImGui::SameLine();
            if (ImGui::Button("- Remove") && !w.spells.empty())
                w.spells.pop_back();

            // --- 持続可能かの即時フィードバック（全出力源の合計）---
            bool sustainable = totalDrain <= w.manaRegen;
            ImGui::TextColored(sustainable ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
                "Total Drain %.1f/s  vs  Regen %.1f/s   %s",
                totalDrain, w.manaRegen, sustainable ? "(sustainable)" : "(will run dry)");
        }
    }

  // ---------- 敵 ----------
    if (ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        int alive = 0;
        for (size_t i = 0; i < m_Enemies.size(); ++i)
        {
            Entity e = m_Enemies[i];
            if (!m_Registry.IsValid(e)) continue;
            ++alive;

            ImGui::PushID((int)i);
            auto& hp = m_Registry.Get<HealthComponent>(e);

            ImGui::Text("Enemy %u : %.0f / %.0f", e, hp.current, hp.max);
            ImGui::SameLine();
            ImGui::Checkbox("Invincible", &hp.invincible);

            // 無敵の的は累計被ダメージを表示（DPS 確認用）
            if (hp.invincible)
            {
                ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f),
                    "   damage taken : %.0f", hp.max - hp.current);
                ImGui::SameLine();
                if (ImGui::Button("Reset HP")) hp.current = hp.max;
            }
            ImGui::PopID();
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
    // ---------- 負荷テスト ----------
    if (ImGui::CollapsingHeader("Stress Test", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // 現在の投射物数を数える
        int projCount = 0;
        m_Registry.CreateView<TransformComponent, ProjectileComponent>()
            .Each([&](Entity, TransformComponent&, ProjectileComponent&) { ++projCount; });

        ImGui::Text("Projectiles : %d", projCount);
        ImGui::Text("Colliders   : %zu", m_CollisionSystem.GetWorldColliders().size());

        ImGui::SliderInt("Spawn Count", &m_StressCount, 50, 2000);
        if (ImGui::Button("+ Spawn")) StressSpawnProjectiles(m_StressCount);
        ImGui::SameLine();
        if (ImGui::Button("Clear All"))
        {
            std::vector<Entity> toKill;
            m_Registry.CreateView<TransformComponent, ProjectileComponent>()
                .Each([&](Entity e, TransformComponent&, ProjectileComponent&) { toKill.push_back(e); });
            for (Entity e : toKill) m_Registry.Destroy(e);
        }

        ImGui::Separator();

    }

    // ---------- 統計 ----------
    ImGui::Separator();
    ImGui::Text("Colliders : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs     : %zu", m_CollisionSystem.GetPairs().size());

    ImGui::End();
}