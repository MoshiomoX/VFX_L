// ============================================================
// CollisionTestScene.cpp
// ============================================================
#include "CollisionTestScene.h"

#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "RigidbodyComponent.h"
#include "ModelComponent.h"
#include "ProjectileComponent.h"
#include "ProjectileVisualComponent.h"
#include "ProjectileVFXComponent.h"
#include "PlayerTag.h"
#include "PlayerStatsComponent.h"
#include "PlayerStateComponent.h"
#include "WandComponent.h"
#include "AreaStats.h"
#include "BackpackComponent.h"
#include "SpellID.h"
#include "HealthComponent.h"
#include "View.h"

#include "PlayerFactory.h"
#include "ItemDatabase.h"
#include "BackpackLogic.h"
#include "TestSpawner.h"
#include "PrimitiveBuilder.h"
#include "DebugManager.h"
#include "InputManager.h"
#include "InputMap.h"
#include "Application.h"
#include "ResourceManager.h"
#include "ResourcePaths.h"
#include "Model.h"
#include "imgui.h"

#include <unordered_set>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <chrono>

// ============================================================
// Init
// ============================================================
void CollisionTestScene::Init()
{
    std::cout << "[CollisionTestScene] Init" << std::endl;

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();

    // ---------- 画面サイズを実測値から取る ----------
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // ---------- アイテム定義表（最初に構築する）----------
    ItemDatabase::Initialize();

    // ---------- Camera ----------
    m_Camera.Init(45.0f, m_ScreenW / m_ScreenH, 0.1f, 10000.0f);
    SetCamera(&m_Camera);

    // ---------- Particle System ----------
    if (!m_ParticleSystem.Initialize(device, context, 100000))
        std::cout << "[Error] ParticleSystem init failed" << std::endl;

    m_ParticleSystem.SetCamera(&m_Camera);

    m_ParticleTexture = ResourceManager::Get().LoadTexture(Res::Tex::ParticleSheet);
    if (m_ParticleTexture)
        m_ParticleSystem.SetTexture(m_ParticleTexture);

    m_VFXContext.particleSystem = &m_ParticleSystem;

    // ---------- 投射物ビルボード ----------
    if (!m_ProjectileRenderer.Initialize(device, context, 4096))
        std::cout << "[Error] ProjectileRenderer init failed" << std::endl;
    m_ProjectileRenderer.SetTexture(
        ResourceManager::Get().LoadTexture(Res::Tex::ProjectileCore));

    // ---------- 定義表 → 各 System へ見た目と VFX を登録 ----------
    RegisterItemVisuals();

    // ---------- UI ----------
    if (!m_SpriteRenderer.Initialize(device, context, 4096))
        std::cout << "[Error] SpriteRenderer init failed" << std::endl;
    m_SpriteRenderer.SetScreenSize(m_ScreenW, m_ScreenH);

    m_BackpackUI.Initialize(ResourceManager::Get().LoadTexture(Res::Tex::BlockSolo));
    m_BackpackUI.LoadIcons();
    m_BackpackUI.Layout(m_ScreenW, m_ScreenH);

    // ---------- 共有モデル ----------
    m_EnemyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 1.0f, 0.35f, 0.35f, 1 });
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.40f, 1.00f, 1 });
    m_StressModel = PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.50f, 0.20f, 1 });

    // ---------- 地形 ----------
    struct TerrainDef { Vector3 pos; Vector3 half; Vector4 color; };
    TerrainDef defs[] = {
        { { 0.0f,  0.0f,  0.0f }, { 12.0f, 0.5f, 12.0f }, { 0.45f, 0.45f, 0.50f, 1 } },
        { { 6.0f,  1.0f,  0.0f }, {  1.0f, 1.0f,  1.0f }, { 0.70f, 0.45f, 0.30f, 1 } },
        { {-6.0f, 0.75f,  3.0f }, {  1.0f, 0.75f, 1.0f }, { 0.30f, 0.60f, 0.40f, 1 } },
        { { 0.0f,  1.5f, -9.0f }, {  4.0f, 1.5f,  0.5f }, { 0.55f, 0.50f, 0.60f, 1 } },
    };
    for (const auto& d : defs)
    {
        Entity e = TestSpawner::SpawnStaticBox(m_Registry, d.pos, d.half);
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateBox(device, d.half, d.color);
        m_Registry.Add<ModelComponent>(e, mc);
        m_Terrain.push_back(e);
    }

    // ============================================================
    // プレイヤー
    // ★組み立ては PlayerFactory に集約。
    //   ここは初期値の指定だけ。実行時の真実は Component 側。
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.spawnPos = { m_SpawnPos[0], m_SpawnPos[1], m_SpawnPos[2] };
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);

    // ---------- 敵 ----------
    RespawnEnemies();

    std::cout << "[CollisionTestScene] Init complete ("
        << (int)m_ScreenW << "x" << (int)m_ScreenH << ")" << std::endl;
}

// ============================================================
// 定義表の内容を各 System へ流し込む
// ※魔法を追加してもここは触らずに済む（全 ID を走査するため）
// ============================================================
void CollisionTestScene::RegisterItemVisuals()
{
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // --- 飛行物型：ビルボード芯と VFX ---
        if (auto* p = ItemDatabase::GetProjectile(id))
        {
            m_WeaponSystem.SetProjectileVisual(id,
                p->visualSize, p->common.color, p->visualStretch);

            if (p->vfxPath)
                m_ProjectileVFXSystem.RegisterVFX(id, p->vfxPath);
        }

        // --- AOE 型：VFX（AreaSystem は未実装）---
        if (auto* a = ItemDatabase::GetArea(id))
        {
            if (a->vfxPath)
                m_ProjectileVFXSystem.RegisterVFX(id, a->vfxPath);
        }
    }
}

// ============================================================
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    m_SpriteRenderer.Shutdown();
    m_ProjectileRenderer.Shutdown();
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// 画面サイズの変化に追従する
// ※UI の当たり判定と描画を同じ座標系に保つために必須
// ============================================================
void CollisionTestScene::UpdateScreenSize()
{
    auto& gfx = Application::Get().GetGraphics();
    float w = gfx.GetWidth();
    float h = gfx.GetHeight();

    if (w == m_ScreenW && h == m_ScreenH) return;
    if (w <= 0.0f || h <= 0.0f) return;

    m_ScreenW = w;
    m_ScreenH = h;

    m_SpriteRenderer.SetScreenSize(w, h);
    m_BackpackUI.Layout(w, h);
    m_Camera.Init(45.0f, w / h, 0.1f, 10000.0f);

    std::cout << "[CollisionTestScene] screen resized: "
        << (int)w << "x" << (int)h << std::endl;
}

// ============================================================
// Update
// ============================================================
void CollisionTestScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_TotalTime += dt;

    UpdateScreenSize();

    // ---- バックパック開閉 ----
    if (InputMap::GetBackpackToggle())
        m_BackpackOpen = !m_BackpackOpen;

    bool paused = m_BackpackOpen && m_PauseOnBackpack;

    // ---- バックパックの操作（開いている間だけ）----
    if (m_BackpackOpen && m_Registry.Has<BackpackComponent>(m_Player))
        m_BackpackUI.HandleInput(m_Registry.Get<BackpackComponent>(m_Player));

    // ---- 集約：配置が変わった時だけ杖を再構築する ----
    // ★一時停止中も実行する。配置しながら結果を確認できるようにするため。
    m_BackpackAggregate.Update(m_Registry);

    if (!paused)
        UpdateGameplay(dt);

    DrawDebugUI();
}

// ============================================================
// 投射物の数を数える（自動補充の判定用）
// ============================================================
int CollisionTestScene::CountProjectiles() const
{
    int n = 0;
    const_cast<Registry&>(m_Registry)
        .CreateView<TransformComponent, ProjectileComponent>()
        .Each([&](Entity, TransformComponent&, ProjectileComponent&) { ++n; });
    return n;
}

// ============================================================
// 通常時の gameplay 一式
// ============================================================
void CollisionTestScene::UpdateGameplay(float dt)
{
    // ---- 自動補充（粒子プールを枯渇状態で維持する）----
    if (m_StressAutoRefill)
    {
        m_RefillTimer += dt;
        if (m_RefillTimer >= m_RefillInterval)
        {
            m_RefillTimer = 0.0f;
            if (CountProjectiles() + m_StressPending < m_RefillTarget)
                m_StressPending += m_RefillBatch;
        }
    }

    // ---- 負荷テストの分割生成（生成スパイクを避ける）----
    if (m_StressPending > 0)
    {
        int batch = (m_StressPending < 50) ? m_StressPending : 50;
        StressSpawnProjectiles(batch);
        m_StressPending -= batch;
    }

    // ============================================================
    // System 実行順（重要）
    // 操作 → 衝突 → 物理 → 状態機 → 杖 → VFX付与 → 投射物 → ダメージ → カメラ
    //
    // ★状態機は物理の後。isGrounded / velocity が今フレームの
    //   最終値になっているため。物理の前だと接地が1フレーム古くなり、
    //   着地アニメーションのタイミングがずれる。
    // ★速度と跳躍力の注入（SetMoveSpeed 等）は廃止。
    //   PlayerControlSystem が PlayerStatsComponent を直接読む。
    // ============================================================
    m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // 生成された投射物に VFX 実例を付ける（WeaponSystem の直後）
    for (const auto& sp : m_WeaponSystem.GetSpawned())
        m_ProjectileVFXSystem.AttachVFX(m_Registry, sp.entity, sp.id, m_VFXContext);

    m_ProjectileSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ============================================================
    // 命中イベント消費：ダメージ + 死亡
    // ★プレイヤーは PlayerStateSystem::TryApplyHit を通す。
    //   無敵判定と硬直の開始をそこ1ヶ所に集約する
    //   （2ヶ所で無敵を見ると必ず片方だけ直して壊れる）。
    // ============================================================
    for (const auto& hit : m_ProjectileSystem.GetHitEvents())
    {
        if (!m_Registry.IsValid(hit.target)) continue;
        if (!m_Registry.Has<HealthComponent>(hit.target)) continue;

        if (m_Registry.Has<PlayerStateComponent>(hit.target))
        {
            PlayerStateSystem::TryApplyHit(m_Registry, hit.target, hit.damage);
            continue;   // プレイヤーは Destroy しない（Dead 状態で残す）
        }

        auto& hp = m_Registry.Get<HealthComponent>(hit.target);
        hp.current -= hit.damage;

        // 無敵の的は 0 で止める（累計ダメージ計測のため）
        if (hp.invincible && hp.current < 0.0f)
            hp.current = 0.0f;

        if (hp.IsDead())
            m_Registry.Destroy(hit.target);
    }

    // ---- カメラ追従（物理の後）----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);

    // ============================================================
    // VFX：追従して emitter を積む → 1回だけ Flush
    // ============================================================
    m_ProjectileVFXSystem.Update(m_Registry, dt, m_VFXContext);

    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();

    // ============================================================
    // ★1フレームに1回だけ。これが無いと粒子が一切動かない。
    //   Flush はコマンドを積むだけなので、負荷をかけても
    //   0 に近いままであるべき。高いままなら GPU を待っている。
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        m_ParticleSystem.Flush(dt, m_TotalTime);

        auto t1 = std::chrono::high_resolution_clock::now();
        m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        m_FlushMsAvg = m_FlushMsAvg * 0.95 + m_FlushMs * 0.05;
        if (m_FlushMs > m_FlushMsPeak) m_FlushMsPeak = m_FlushMs;
    }

    // ---- 衝突体の線框表示 ----
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
                    // 投射物は数が多すぎるので線框を描かない
                    if (m_Registry.Has<ProjectileComponent>(e)) return;

                    Color col = hitting.count(e) ? Color(1.0f, 0.3f, 0.3f, 1.0f)
                        : Color(0.4f, 1.0f, 0.4f, 1.0f);
                    DrawColliderDebug(e, col);
                });
    }

    if (m_ShowWandDebug)
        DrawWandDebug();
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

    // ---- 1) 不透明メッシュ ----
    if (m_ShowMesh)
        m_RenderSystem.Render(m_Registry, renderer);

    // ---- 2) 投射物の芯（ビルボード、加算合成）----
    if (m_ShowBillboard)
        m_ProjectileRenderer.Render(m_Registry, GetCamera());

    // ---- 3) パーティクル（加算合成）----
    m_ParticleSystem.SetCamera(GetCamera());
    m_ParticleSystem.Render();

    // ---- 4) UI（深度なし、常に最前面）----
    if (m_BackpackOpen && m_Registry.Has<BackpackComponent>(m_Player))
    {
        m_SpriteRenderer.Begin();
        m_BackpackUI.Draw(m_SpriteRenderer, m_Registry.Get<BackpackComponent>(m_Player));
        m_SpriteRenderer.End();
    }
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

    Vector3 muzzle = tf.position + wand.muzzleOffset;
    dbg.DrawWireSphere(muzzle, wand.range, Color(0.25f, 0.4f, 0.8f, 1.0f));

    if (!aim.hasTarget) return;

    // 目標マーク
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

    // 出力源ごとに分裂の扇形（実際の発射計算と同じ）
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

        for (int k = 0; k < s.pendingCasts; ++k)
        {
            Vector3 p = origin + aim.dir * 0.4f + Vector3(0.15f * (float)k, 0.0f, 0.0f);
            dbg.AddDebugLine(p, p + Vector3(0.0f, 0.18f, 0.0f), Color(1.0f, 0.5f, 0.1f, 1.0f));
        }
    }
}

// ============================================================
// プレイヤーのメッシュと衝突体を作り直す
// ★寸法は PlayerStatsComponent が持つので PlayerFactory へ委譲する
// ============================================================
void CollisionTestScene::RebuildPlayerMesh()
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    PlayerFactory::RebuildVisual(m_Registry, m_Player, device,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
}

// ============================================================
// 敵を1体生成
// ============================================================
void CollisionTestScene::SpawnEnemy(const Vector3& pos, bool invincible)
{
    Entity e = TestSpawner::SpawnCapsule(m_Registry, pos, 0.4f, 1.0f);

    auto& col = m_Registry.Get<ColliderComponent>(e);
    col.layer = Layer_Enemy;
    col.mask = Layer_All;

    HealthComponent hp;
    hp.invincible = invincible;
    if (invincible) { hp.max = 9999.0f; hp.current = 9999.0f; }
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

    SpawnEnemy({ 0.0f, 3.0f, 8.0f }, true);   // 無敵の的（DPS 計測用）
    SpawnEnemy({ 8.0f, 3.0f,  4.0f });
    SpawnEnemy({ -8.0f, 3.0f,  5.0f });
    SpawnEnemy({ 4.0f, 3.0f, -7.0f });
}

// ============================================================
// 負荷テスト：投射物を一気に生成
// ★With VFX を ON にすると emitter が生まれ、粒子発射経路の負荷試験になる。
// ============================================================
void CollisionTestScene::StressSpawnProjectiles(int count)
{
    if (!m_Registry.IsValid(m_Player)) return;
    Vector3 origin = m_Registry.Get<TransformComponent>(m_Player).position + Vector3(0, 2.0f, 0);

    for (int i = 0; i < count; ++i)
    {
        float a = (float)rand() / RAND_MAX * 6.2831853f;
        float b = (float)rand() / RAND_MAX * 6.2831853f;
        Vector3 dir(std::cos(a) * std::cos(b), std::sin(b) * 0.3f, std::sin(a) * std::cos(b));
        dir.Normalize();

        Entity p = m_Registry.Create();

        TransformComponent tf;
        tf.position = origin;
        m_Registry.Add<TransformComponent>(p, tf);

        if (m_StressWithCollider)
        {
            ColliderComponent col;
            col.shape = ColliderShape::Sphere;
            col.radius = 0.25f;
            col.layer = Layer_PlayerShot;
            col.mask = Layer_Enemy | Layer_Terrain;
            m_Registry.Add<ColliderComponent>(p, col);
        }

        ProjectileComponent pj;
        pj.velocity = dir * 8.0f;
        pj.damage = 1.0f;
        pj.lifetime = 30.0f;
        m_Registry.Add<ProjectileComponent>(p, pj);

        ProjectileVisualComponent vis;
        vis.size = 0.5f;
        vis.color = { 1.0f, 0.5f, 0.2f, 1.0f };
        vis.stretch = 0.0f;
        m_Registry.Add<ProjectileVisualComponent>(p, vis);

        if (m_StressWithModel && m_StressModel)
        {
            ModelComponent mc;
            mc.model = m_StressModel;
            m_Registry.Add<ModelComponent>(p, mc);
        }

        // ★VFX（= emitter）を付ける。粒子プールを枯渇させるのが目的。
        if (m_StressWithVFX)
            m_ProjectileVFXSystem.AttachVFX(m_Registry, p, m_StressVFXItem, m_VFXContext);
    }
}

// ============================================================
// プリセット適用
// ============================================================
void CollisionTestScene::ApplyStressPreset(const StressPreset& p)
{
    std::vector<Entity> toKill;
    m_Registry.CreateView<TransformComponent, ProjectileComponent>()
        .Each([&](Entity e, TransformComponent&, ProjectileComponent&) { toKill.push_back(e); });
    for (Entity e : toKill) m_Registry.Destroy(e);

    m_StressPending = 0;

    m_RefillTarget = p.target;
    m_RefillBatch = p.batch;
    m_StressAutoRefill = p.autoRefill;
    m_StressWithVFX = p.withVFX;
    m_StressWithCollider = p.withCollider;
    m_StressWithModel = p.withModel;

    m_FlushMsPeak = 0.0;
    m_FlushMsAvg = 0.0;
    m_RefillTimer = 0.0f;

    std::cout << "[Stress] preset applied: " << p.name
        << "  (" << p.purpose << ")" << std::endl;
}



// ============================================================
// ImGui：バックパック
// ============================================================
void CollisionTestScene::DrawBackpackPanel()
{
    if (!ImGui::CollapsingHeader("Backpack", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    ImGui::Text("Open : %s   (Tab / I / E / Start)", m_BackpackOpen ? "YES" : "no");
    ImGui::Checkbox("Pause On Open", &m_PauseOnBackpack);
    ImGui::TextDisabled("LMB: place   RMB: remove   Wheel/R: rotate");
    ImGui::Text("Rotation : %d", m_BackpackUI.GetRotation());

    ImGui::Separator();
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;

        bool selected = m_BackpackUI.HasSelection() && m_BackpackUI.GetSelectedItem() == id;

        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(c->color.x * 0.6f, c->color.y * 0.6f, c->color.z * 0.6f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(c->color.x * 0.9f, c->color.y * 0.9f, c->color.z * 0.9f, 1.0f));

        if (ImGui::Button(c->name))
            m_BackpackUI.SetSelectedItem(id);

        ImGui::PopStyleColor(2);

        if (selected)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "<-");
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (ImGui::Button("Clear Selection")) m_BackpackUI.ClearSelection();

    if (m_Registry.Has<BackpackComponent>(m_Player))
    {
        auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
        ImGui::SameLine();
        ImGui::Text("| Placed : %zu", bp.items.size());
        ImGui::SameLine();
        if (ImGui::Button("Clear Grid"))
        {
            bp.items.clear();
            BackpackLogic::RebuildOccupancy(bp);
            bp.dirty = true;
        }
    }

    ImGui::Separator();
    ImGui::Text("Aggregate (rebuilt %d times)", m_BackpackAggregate.GetRebuildCount());

    const auto& logs = m_BackpackAggregate.GetLog();
    if (logs.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "No attack block placed -> wand fires nothing");
    }
    for (const auto& log : logs)
    {
        ImGui::Text("%s (%d,%d)", log.sourceName.c_str(), log.row, log.col);
        if (log.influencedBy.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  no influence");
        }
        else
        {
            for (const auto& n : log.influencedBy)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1, 1, 1), " <- %s", n.c_str());
            }
        }
    }

    if (ImGui::Button("Force Rebuild"))
        m_BackpackAggregate.ForceRebuild(m_Registry, m_Player);

    if (ImGui::TreeNode("Layout"))
    {
        bool dirty = false;

        int anchorIdx = (int)m_BackpackUI.anchor;
        const char* anchorNames[] = { "Top Left", "Top Right", "Center",
                                      "Bottom Left", "Bottom Right" };
        if (ImGui::Combo("Anchor", &anchorIdx, anchorNames, 5))
        {
            m_BackpackUI.anchor = (BackpackUI::Anchor)anchorIdx;
            dirty = true;
        }

        dirty |= ImGui::DragFloat("Grid Ratio", &m_BackpackUI.gridScreenRatio, 0.005f, 0.10f, 0.95f);
        dirty |= ImGui::DragFloat("Gap Ratio", &m_BackpackUI.cellGapRatio, 0.002f, 0.00f, 0.50f);
        dirty |= ImGui::DragFloat("Pad Ratio", &m_BackpackUI.framePadRatio, 0.005f, 0.00f, 1.00f);
        dirty |= ImGui::DragFloat("Margin Ratio", &m_BackpackUI.marginRatio, 0.002f, 0.00f, 0.30f);
        if (dirty) m_BackpackUI.Layout(m_ScreenW, m_ScreenH);

        ImGui::Text("Screen : %.0f x %.0f", m_ScreenW, m_ScreenH);
        ImGui::Text("Cell : %.1f px   Gap : %.1f px",
            m_BackpackUI.GetCellSize(), m_BackpackUI.GetCellGap());

        ImGui::ColorEdit4("Frame Color", &m_BackpackUI.frameColor.x);
        ImGui::ColorEdit4("Cell Color", &m_BackpackUI.cellColor.x);

        ImGui::Text("Sprites : %u   Draw calls : %u",
            m_SpriteRenderer.GetLastSpriteCount(), m_SpriteRenderer.GetLastDrawCalls());
        ImGui::TreePop();
    }
}

// ============================================================
// ImGui：杖（集約結果の確認用）
// ============================================================
void CollisionTestScene::DrawWandPanel()
{
    if (!ImGui::CollapsingHeader("Wand (result of aggregation)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<WandComponent>(m_Player))
        return;

    auto& w = m_Registry.Get<WandComponent>(m_Player);

    char buf[64];
    sprintf_s(buf, "%.0f / %.0f", w.manaCurrent, w.manaMax);
    ImGui::ProgressBar(w.manaCurrent / w.manaMax, ImVec2(-1, 0), buf);

    ImGui::DragFloat("Mana Max", &w.manaMax, 1.0f, 10.0f, 1000.0f);
    ImGui::DragFloat("Mana Regen", &w.manaRegen, 0.5f, 0.0f, 300.0f);
    ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);

    // ---- 施法モード ----
    int modeIdx = (int)w.castMode;
    const char* modeNames[] = { "Auto", "Manual", "Debug Burst" };
    if (ImGui::Combo("Cast Mode", &modeIdx, modeNames, 3))
        w.castMode = (CastMode)modeIdx;

    switch (w.castMode)
    {
    case CastMode::Auto:
        ImGui::TextDisabled("Fires whenever a target is in range.");
        break;
    case CastMode::Manual:
        ImGui::TextDisabled("LMB / Pad X to fire. Aim is still automatic.");
        ImGui::TextColored(w.castRequested ? ImVec4(1, 0.9f, 0.3f, 1) : ImVec4(0.5f, 0.5f, 0.5f, 1),
            "requested: %s", w.castRequested ? "YES" : "no");
        break;
    case CastMode::DebugBurst:
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
            "Ignores cast interval. Mana still applies,");
        ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1),
            "so the real rate is bounded by mana regen.");
        break;
    }

    ImGui::Text("Cast Anim : %.2f s", w.castAnimTimer);
    ImGui::DragFloat("Anim Duration", &w.castAnimDuration, 0.01f, 0.05f, 2.0f);

    ImGui::TextDisabled("spells / areas are read-only (from backpack)");
    ImGui::Separator();

    float totalDrain = 0.0f;

    // ---- 飛行物型 ----
    for (size_t i = 0; i < w.spells.size(); ++i)
    {
        const auto& s = w.spells[i];
        const ItemCommon* c = ItemDatabase::GetCommon(s.id);
        const char* name = c ? c->name : "Unknown";

        ImGui::PushID((int)i);
        ImGui::Text("[%d] %s", (int)i, name);
        ImGui::Indent();
        ImGui::Text("shots=%d  spread=%.0f  casts=%d  delay=%.2f",
            s.projectileCount, s.spreadAngle, s.castCount, s.castDelay);
        ImGui::Text("damage=%.1f  speed=%.1f  interval=%.2f  mana=%.1f",
            s.damage, s.speed, s.castInterval, s.manaCost);
        ImGui::Text("pending=%d  timer=%.2f", s.pendingCasts, s.castTimer);
        ImGui::Unindent();
        ImGui::PopID();

        if (s.castInterval > 0.0f)
            totalDrain += (s.manaCost * (float)s.castCount) / s.castInterval;
    }

    // ---- AOE 型（AreaSystem 未実装、表示のみ）----
    for (size_t i = 0; i < w.areas.size(); ++i)
    {
        const auto& a = w.areas[i];
        const ItemCommon* c = ItemDatabase::GetCommon(a.id);
        const char* name = c ? c->name : "Unknown";

        ImGui::PushID(1000 + (int)i);
        ImGui::Text("[AOE %d] %s", (int)i, name);
        ImGui::Indent();
        ImGui::Text("radius=%.1f  duration=%.1f  tick=%.2f  dmg/tick=%.1f",
            a.radius, a.duration, a.tickInterval, a.damagePerTick);
        ImGui::TextDisabled("AreaSystem not implemented yet");
        ImGui::Unindent();
        ImGui::PopID();

        if (a.castInterval > 0.0f)
            totalDrain += a.manaCost / a.castInterval;
    }

    bool sustainable = totalDrain <= w.manaRegen;
    ImGui::TextColored(sustainable ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
        "Total Drain %.1f/s  vs  Regen %.1f/s   %s",
        totalDrain, w.manaRegen, sustainable ? "(sustainable)" : "(will run dry)");
}

// ============================================================
// ImGui：プレイヤー（能力値 + 状態機）
// ★数値は全て PlayerStatsComponent。シーンはコピーを持たない。
// ============================================================
void CollisionTestScene::DrawPlayerPanel()
{
    if (!ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player)) return;

    // ---- 位置と速度 ----
    if (m_Registry.Has<TransformComponent>(m_Player) &&
        m_Registry.Has<RigidbodyComponent>(m_Player))
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

    // ---- 体力 ----
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        char buf[64];
        sprintf_s(buf, "%.0f / %.0f", hp.current, hp.max);
        ImGui::ProgressBar(hp.current / hp.max, ImVec2(-1, 0), buf);
    }

    // ---- 状態機（3層）----
    if (m_Registry.Has<PlayerStateComponent>(m_Player))
    {
        auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);

        const char* moveNames[] = { "Idle", "Run", "Jump", "Fall" };
        const char* actionNames[] = { "None", "Casting" };
        const char* dmgNames[] = { "Normal", "Hurt", "Dead" };

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "State Machine (3 layers)");
        ImGui::Text("Move   : %-6s  %.2fs", moveNames[(int)st.move], st.moveTime);
        ImGui::Text("Action : %-8s  %.2fs", actionNames[(int)st.action], st.actionTime);

        ImVec4 dcol = st.IsDead() ? ImVec4(1, 0.3f, 0.3f, 1)
            : (st.damage == DamageStateID::Hurt) ? ImVec4(1, 0.8f, 0.3f, 1)
            : ImVec4(0.4f, 1, 0.4f, 1);
        ImGui::TextColored(dcol, "Damage : %-7s  %.2fs",
            dmgNames[(int)st.damage], st.damageTime);

        if (st.IsInvincible())
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1, 1),
                "  invincible %.2fs", st.invincibleTimer);

        const uint32_t mask = st.SuppressMask();
        if (mask != Mask_None)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "  suppressing: %s%s",
                (mask & Mask_Move) ? "Move " : "", (mask & Mask_Action) ? "Action" : "");

        ImGui::DragFloat("Hurt Duration", &st.hurtDuration, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Invincible Time", &st.invincibleAfterHit, 0.05f, 0.0f, 5.0f);

        if (ImGui::Button("Take 10 Damage"))
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, 10.0f);
        ImGui::SameLine();
        if (ImGui::Button("Revive"))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.current = hp.max;
            st.damage = DamageStateID::Normal;
            st.damageTime = 0.0f;
            st.invincibleTimer = 0.0f;
        }
    }

    // ---- 能力値 ----
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        auto& stt = m_Registry.Get<PlayerStatsComponent>(m_Player);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Stats (PlayerStatsComponent)");

        bool dirty = false;
        dirty |= ImGui::DragFloat("Radius", &stt.radius, 0.01f, 0.05f, 3.0f);
        dirty |= ImGui::DragFloat("Height", &stt.height, 0.01f, 0.05f, 5.0f);
        dirty |= ImGui::ColorEdit3("Color", m_PlayerColor);
        if (dirty) RebuildPlayerMesh();

        ImGui::DragFloat("Move Speed", &stt.moveSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Jump Power", &stt.jumpPower, 0.1f, 0.0f, 30.0f);
        ImGui::Text("Jump CD : %.2f", stt.jumpCooldown);
    }

    // 重力は場のパラメータなのでプレイヤーではなくシーンが持つ
    ImGui::Separator();
    ImGui::DragFloat("Gravity (scene)", &m_Gravity, 0.5f, -100.0f, 0.0f);
}

// ============================================================
// ImGui：負荷テスト
// ============================================================
void CollisionTestScene::DrawStressPanel()
{
    if (!ImGui::CollapsingHeader("Stress Test"))
        return;

    // ---------- プリセット ----------
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Presets");
    ImGui::TextDisabled("Clears projectiles and resets counters, then applies.");

    const int count = (int)(sizeof(kStressPresets) / sizeof(kStressPresets[0]));
    for (int i = 0; i < count; ++i)
    {
        const auto& p = kStressPresets[i];
        if (i % 2 != 0) ImGui::SameLine();

        const bool active = (m_LastPresetIndex == i);
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.9f, 1.0f));

        if (ImGui::Button(p.name, ImVec2(150, 0)))
        {
            ApplyStressPreset(p);
            m_LastPresetIndex = i;
        }

        if (active) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.purpose);
    }

    if (m_LastPresetIndex >= 0)
        ImGui::TextDisabled("current: %s", kStressPresets[m_LastPresetIndex].name);

    ImGui::Separator();

    int projCount = CountProjectiles();
    ImGui::Text("Projectiles : %d   (pending %d)", projCount, m_StressPending);
    ImGui::Text("Colliders   : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs       : %zu", m_CollisionSystem.GetPairs().size());

    ImGui::Separator();
    ImGui::Checkbox("With Collider", &m_StressWithCollider);
    ImGui::SameLine();
    ImGui::Checkbox("With 3D Model", &m_StressWithModel);
    ImGui::TextDisabled("Uncheck 3D Model -> billboard only (1 draw call)");

    ImGui::Checkbox("With VFX (emit path test)", &m_StressWithVFX);
    if (!m_StressWithVFX)
    {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "<- OFF = EmitCS never runs");
    }

    ImGui::SliderInt("Spawn Count", &m_StressCount, 50, 2000);
    if (ImGui::Button("+ Spawn")) m_StressPending += m_StressCount;
    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        std::vector<Entity> toKill;
        m_Registry.CreateView<TransformComponent, ProjectileComponent>()
            .Each([&](Entity e, TransformComponent&, ProjectileComponent&) { toKill.push_back(e); });
        for (Entity e : toKill) m_Registry.Destroy(e);
        m_StressPending = 0;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Pool Starvation Test");
    ImGui::TextDisabled("Keeps the dead list empty. This is the only state");
    ImGui::TextDisabled("where the EmitCS deadCount guard actually matters.");

    ImGui::Checkbox("Auto Refill", &m_StressAutoRefill);
    ImGui::SliderInt("Target Projectiles", &m_RefillTarget, 100, 4000);
    ImGui::SliderInt("Refill Batch", &m_RefillBatch, 10, 500);

    if (m_StressAutoRefill && !m_StressWithVFX)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
            "Auto Refill without VFX does not stress the pool!");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Particle System");

    float ratio = (float)m_LastEmitterCount / (float)m_ParticleSystem.GetMaxEmitters();
    ImVec4 col = (ratio > 0.9f) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (ratio > 0.7f) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(col, "Emitters : %zu / %zu",
        m_LastEmitterCount, m_ParticleSystem.GetMaxEmitters());

    if (m_StressWithVFX && m_LastEmitterCount == 0)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
            "0 emitters: no VFX template for this item (check vfxPath)");

    if (m_LastDropped > 0)
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
            "Dropped : %zu  (some projectiles have no VFX)", m_LastDropped);

    ImGui::Text("Pool Size      : %u", m_ParticleSystem.GetMaxParticles());
    ImGui::Text("Projectile VFX : %zu", m_ProjectileVFXSystem.GetActiveVFXCount());
    ImGui::Text("Billboards     : %u  (1 draw call)",
        m_ProjectileRenderer.GetLastDrawCount());

    ImGui::TextDisabled("Alive count lives on the GPU only.");
    ImGui::TextDisabled("Judge by the screen and by Flush ms below.");

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Flush CPU Time");
    ImGui::TextDisabled("Flush only queues commands. It should stay near 0.");

    ImVec4 fcol = (m_FlushMsAvg > 1.0) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (m_FlushMsAvg > 0.3) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(fcol, "now %.4f ms   avg %.4f ms   peak %.4f ms",
        m_FlushMs, m_FlushMsAvg, m_FlushMsPeak);

    if (ImGui::Button("Reset Peak"))
    {
        m_FlushMsPeak = 0.0;
        m_FlushMsAvg = 0.0;
    }
    ImGui::SameLine();
    ImGui::Text("| FPS %.1f", ImGui::GetIO().Framerate);
}

// ============================================================
// ImGui
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    ImGui::Begin("Game Test");

    ImGui::Checkbox("Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::Checkbox("Billboard", &m_ShowBillboard);
    ImGui::SameLine();
    ImGui::Checkbox("Collider", &m_ShowWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Wand Debug", &m_ShowWandDebug);
    ImGui::Separator();

    DrawPlayerPanel();
    DrawBackpackPanel();
    DrawWandPanel();
    DrawStressPanel();

    // ---------- Item Database ----------
    if (ImGui::CollapsingHeader("Item Database"))
    {
        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;

            const char* cat = "?";
            switch (c->category)
            {
            case ItemCategory::Projectile: cat = "Projectile"; break;
            case ItemCategory::Function:   cat = "Function";   break;
            case ItemCategory::Area:       cat = "Area";       break;
            }

            ImGui::TextColored(ImVec4(c->color.x, c->color.y, c->color.z, 1.0f),
                "%-14s [%s]  occupy=%zu  influence=%zu  icon=%s",
                c->name, cat, c->occupyCells.size(), c->influenceCells.size(),
                c->iconPath ? "yes" : "no");
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

    ImGui::End();
}