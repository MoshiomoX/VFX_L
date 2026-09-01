// ============================================================
// CollisionTestScene.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"

#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/ModelComponent.h"
#include "Component/Projectile/ProjectileComponent.h"
#include "Component/Projectile/ProjectileVisualComponent.h"
#include "Component/Projectile/ProjectileVFXComponent.h"
#include "Player/PlayerTag.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Component/WandComponent.h"
#include "Component/AreaStats.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "SpellID.h"
#include "Item/ItemTypes.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "ECS/View.h"

#include "Player/PlayerFactory.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Debug/TestSpawner.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Debug/DebugManager.h"
#include "Manager/InputManager.h"
#include "Manager/InputMap.h"
#include "Core/Application.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "Graphics/Model/Model.h"
#include "imgui.h"
#include "Player/LevelComponent.h"
#include "Item/ExpOrbComponent.h"
#include "Item/ExpRewardComponent.h"
#include "Item/ExpOrbSystem.h"
#include "UI/LevelUpSystem.h"

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

    // ---------- ????????????? ----------
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // ---------- ???????(???????)----------
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

    // ---------- ???????? ----------
    if (!m_ProjectileRenderer.Initialize(device, context, 4096))
        std::cout << "[Error] ProjectileRenderer init failed" << std::endl;
    m_ProjectileRenderer.SetTexture(
        ResourceManager::Get().LoadTexture(Res::Tex::ProjectileCore));

    // ---------- ??? ? ? System ????? VFX ??? ----------
    RegisterItemVisuals();

    // ---------- UI ----------
    if (!m_SpriteRenderer.Initialize(device, context, 4096))
        std::cout << "[Error] SpriteRenderer init failed" << std::endl;
    m_SpriteRenderer.SetScreenSize(m_ScreenW, m_ScreenH);

    if (!m_TextRenderer.Initialize(device, context, Res::Fnt::JP))
        std::cout << "[Error] TextRenderer init failed" << std::endl;

    m_BackpackUI.Initialize(ResourceManager::Get().LoadTexture(Res::Tex::BlockSolo));
    m_BackpackUI.LoadIcons();
    m_BackpackUI.Layout(m_ScreenW, m_ScreenH);

    if (!m_HUD.Initialize(device))
        std::cout << "[Error] HUD init failed" << std::endl;
    m_HUD.Layout(m_ScreenW, m_ScreenH);

    // ---------- ????? ----------
    m_EnemyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 1.0f, 0.35f, 0.35f, 1 });
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.40f, 1.00f, 1 });
    m_StressModel = PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.50f, 0.20f, 1 });

    // ---------- ?? ----------
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
    // ?????
    // ????? PlayerFactory ????
    // ??????????????????? Component ??
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.spawnPos = { m_SpawnPos[0], m_SpawnPos[1], m_SpawnPos[2] };
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);

    // ???????? UI ????
    // ??????????????????????????????
    // Registry ???????????????????????
    // SpellbookComponent ??????????????????????
    // ???????????????????????????????
    if (m_Registry.Has<SpellbookComponent>(m_Player))
        m_BackpackUI.SetSpellbook(&m_Registry.Get<SpellbookComponent>(m_Player));
    // ---------- ?????? UI ----------
    // ?????????????????????????
    m_LevelUpUI.Initialize(ResourceManager::Get().LoadTexture(Res::Tex::BlockSolo));
    m_LevelUpUI.LoadIcons();
    m_LevelUpUI.Layout(m_ScreenW, m_ScreenH);
    // ---------- ? ----------
    RespawnEnemies();

    std::cout << "[CollisionTestScene] Init complete ("
        << (int)m_ScreenW << "x" << (int)m_ScreenH << ")" << std::endl;
}

// ============================================================
// ???????? System ?????
// ?????????????????(? ID ???????)
// ============================================================
void CollisionTestScene::RegisterItemVisuals()
{
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // --- ????:??????? VFX ---
        if (auto* p = ItemDatabase::GetProjectile(id))
        {
            m_WeaponSystem.SetProjectileVisual(id,
                p->visualSize, p->common.color, p->visualStretch);

            if (p->vfxPath)
                m_ProjectileVFXSystem.RegisterVFX(id, p->vfxPath);
        }

        // --- AOE ?:VFX(AreaSystem ????)---
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
    m_TextRenderer.Shutdown();
    m_SpriteRenderer.Shutdown();
    m_ProjectileRenderer.Shutdown();
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// ?????????????
// UI ???????????????????????
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
    m_LevelUpUI.Layout(w, h);
    m_HUD.Layout(w, h);
    m_Camera.Init(45.0f, w / h, 0.1f, 10000.0f);

    std::cout << "[CollisionTestScene] screen resized: "
        << (int)w << "x" << (int)h << std::endl;
}
// ============================================================
// Update
// ============================================================
// ============================================================
// Update
// ============================================================
void CollisionTestScene::Update(float dt)
{
    SceneBase::Update(dt);
    m_TotalTime += dt;

    UpdateScreenSize();

    // ============================================================
    // ??????????????????? UI ???????
    //
    // ??????????????????????????
    // ???????????????????
    // ????????????????????
    // ============================================================
    if (!m_Registry.IsValid(m_Player))
        m_UI.Clear();

    UpdateUI();

    // ---- ??:????????????????? ----
    // UI ???????????????????????????????
    m_BackpackAggregate.Update(m_Registry);

    // ============================================================
    // ?????
    //
    // ??????????????
    // ???????????????????????????????
    // ============================================================
    const bool pauseByUI = m_UI.ShouldPauseGame()
        && !(m_UI.Top() == UILayer::Backpack && !m_PauseOnBackpack);

    if (!pauseByUI)
        UpdateGameplay(dt);

    DrawDebugUI();
}

// ============================================================
// UI ???
//
// 1. ???????????
// 2. ?????????
// 3. ???? UI ???????
//
// ????????????????????????
// UIManager ????????????????
// ============================================================
void CollisionTestScene::UpdateUI()
{
    // ---- 1. ??????? ----
    // ???????????Push ??????????
    if (LevelUpSystem::IsAnyoneChoosing(m_Registry))
        m_UI.Push(UILayer::LevelUp, UIManager::CloseMode::Forced);

    // ---- 2. ????????? ----
    // ?????????????????????? Tab ????
    // ????????????????????????
    const bool canToggleBackpack =
        m_UI.CanReceiveInput(UILayer::Backpack) || m_UI.IsEmpty();

    if (canToggleBackpack && InputMap::GetBackpackToggle())
        m_UI.Toggle(UILayer::Backpack);

    // ---- 3. ?????????? ----
    switch (m_UI.Top())
    {
    case UILayer::LevelUp:
    {
        if (!m_Registry.Has<LevelComponent>(m_Player)) break;

        const auto& lv = m_Registry.Get<LevelComponent>(m_Player);

        ItemID picked;
        if (m_LevelUpUI.HandleInput(lv, picked))
        {
            LevelUpSystem::Choose(m_Registry, m_Player, picked);
            m_UI.Pop(UILayer::LevelUp);
        }
        break;
    }

    case UILayer::Backpack:
    {
        if (!m_Registry.Has<BackpackComponent>(m_Player)) break;
        m_BackpackUI.HandleInput(m_Registry.Get<BackpackComponent>(m_Player));
        break;
    }

    default:
        break;
    }
}
// ============================================================
// ?????????
//
// ??????????????
// ?????????????????
// ============================================================
void CollisionTestScene::UpdateLevelUpChoice()
{
    if (!m_Registry.Has<LevelComponent>(m_Player)) return;

    const auto& lv = m_Registry.Get<LevelComponent>(m_Player);

    ItemID picked;
    if (m_LevelUpUI.HandleInput(lv, picked))
        LevelUpSystem::Choose(m_Registry, m_Player, picked);

    // ?????????????????????????
    // ??????????????????????????????
    m_BackpackAggregate.Update(m_Registry);
}// ============================================================
// ???? gameplay ??
// ============================================================
void CollisionTestScene::UpdateGameplay(float dt)
{
    // ---- ????(???????????????)----
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

    // ---- ??????????(??????????)----
    if (m_StressPending > 0)
    {
        int batch = (m_StressPending < 50) ? m_StressPending : 50;
        StressSpawnProjectiles(batch);
        m_StressPending -= batch;
    }

    // ============================================================
    // System ???(??)
    // ?? ? ?? ? ?? ? ??? ? ? ? VFX?? ? ??? ? ???? ? ???
    //
    // ?????????isGrounded / velocity ???????
    // ?????????????????????1?????????
    // ????????????????????
    // ?????????(SetMoveSpeed ?)????
    // PlayerControlSystem ? PlayerStatsComponent ??????
    // ============================================================
    m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ????????? VFX ??????(WeaponSystem ???)
    for (const auto& sp : m_WeaponSystem.GetSpawned())
        m_ProjectileVFXSystem.AttachVFX(m_Registry, sp.entity, sp.id, m_VFXContext);

    m_ProjectileSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ============================================================
    // ????????:???? + ??
    // ?????? PlayerStateSystem::TryApplyHit ????
    // ?????????????1???????
    // (2?????????????????????)?
    // ============================================================
    for (const auto& hit : m_ProjectileSystem.GetHitEvents())
    {
        if (!m_Registry.IsValid(hit.target)) continue;
        if (!m_Registry.Has<HealthComponent>(hit.target)) continue;

        if (m_Registry.Has<PlayerStateComponent>(hit.target))
        {
            PlayerStateSystem::TryApplyHit(m_Registry, hit.target, hit.damage);
            continue;   // ?????? Destroy ???(Dead ?????)
        }

        auto& hp = m_Registry.Get<HealthComponent>(hit.target);
        hp.current -= hit.damage;

        // ????? 0 ????(???????????)
        if (hp.invincible && hp.current < 0.0f)
            hp.current = 0.0f;

        if (hp.IsDead())
        {
            ExpOrbSystem::DropFrom(m_Registry, hit.target);
            m_Registry.Destroy(hit.target);
        }
    }

    // ---- ?????(????)----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);

    // ============================================================
    // VFX:???? emitter ??? ? 1??? Flush
    // ============================================================
    m_ProjectileVFXSystem.Update(m_Registry, dt, m_VFXContext);

    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();
    // ---- ?????? ----
    // ?????????????????????
    m_ExpOrbSystem.Update(m_Registry, dt);

	// ============================================================
	// ???????????????
	// ============================================================
    m_LevelUpSystem.Update(m_Registry);
    // ---- ?????(????)----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);
    // ============================================================
    // 1?????1????????????????????
    // Flush ?????????????????????
    // 0 ????????????????? GPU ???????
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        m_ParticleSystem.Flush(dt, m_TotalTime);

        auto t1 = std::chrono::high_resolution_clock::now();
        m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        m_FlushMsAvg = m_FlushMsAvg * 0.95 + m_FlushMs * 0.05;
        if (m_FlushMs > m_FlushMsPeak) m_FlushMsPeak = m_FlushMs;
    }

    // ---- ???????? ----
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
                    // ???????????????????
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

    // ---- 1) ??????? ----
    if (m_ShowMesh)
        m_RenderSystem.Render(m_Registry, renderer);

    // ---- 2) ?????(??????????)----
    if (m_ShowBillboard)
        m_ProjectileRenderer.Render(m_Registry, GetCamera());

    // ---- 3) ??????(????)----
    if (m_ShowParticle)
    {
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.Render();
    }

    // ============================================================
    // 4) UI(??????????)
    //
    // Begin / End ?1????????????????
    // ??? UI ?????1?? draw call ?????
    // ============================================================
    m_SpriteRenderer.Begin();
    m_TextRenderer.Begin();

    // ---- HUD（常時表示、モーダル UI が開いている間は隠す）----
    // 文字はスプライトの後に一括で描かれるため、隠さないと
    // 暗幕の上に HUD の文字だけが浮いてしまう
    if (m_UI.IsEmpty()
        && m_Registry.IsValid(m_Player)
        && m_Registry.Has<HealthComponent>(m_Player)
        && m_Registry.Has<ManaComponent>(m_Player)
        && m_Registry.Has<LevelComponent>(m_Player))
    {
        m_HUD.Draw(m_SpriteRenderer, m_TextRenderer,
            m_Registry.Get<HealthComponent>(m_Player),
            m_Registry.Get<ManaComponent>(m_Player),
            m_Registry.Get<LevelComponent>(m_Player));
    }

    DrawUI();

    m_SpriteRenderer.End();
    m_TextRenderer.End();
}

// ============================================================
// UI ???
//
// ????????????????????????
// ???????????????????
// ============================================================
void CollisionTestScene::DrawUI()
{
    for (UILayer layer : m_UI.GetStack())
    {
        switch (layer)
        {
        case UILayer::Backpack:
            if (m_Registry.Has<BackpackComponent>(m_Player))
                m_BackpackUI.Draw(m_SpriteRenderer,
                    m_Registry.Get<BackpackComponent>(m_Player));
            break;

        case UILayer::LevelUp:
            if (m_Registry.Has<LevelComponent>(m_Player))
                m_LevelUpUI.Draw(m_SpriteRenderer,
                    m_Registry.Get<LevelComponent>(m_Player));
            break;

        default:
            break;
        }
    }
}

// ============================================================
// ????????
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
// ??????:???? / ?? / ????? / ????
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

    // ?????
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

    // ???????????(??????????)
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
// ???????????????????
// ??? PlayerStatsComponent ????? PlayerFactory ?????
// ============================================================
void CollisionTestScene::RebuildPlayerMesh()
{
    auto* device = Application::Get().GetGraphics().GetDevice();
    PlayerFactory::RebuildVisual(m_Registry, m_Player, device,
        { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f });
}

// ============================================================
// ??1???
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

    // ????????????????????????
    ExpRewardComponent reward;
    reward.amount = 20.0f;
    reward.splitCount = 1;
    m_Registry.Add<ExpRewardComponent>(e, reward);

    ModelComponent mc;
    mc.model = invincible ? m_DummyModel : m_EnemyModel;
    m_Registry.Add<ModelComponent>(e, mc);

    m_Enemies.push_back(e);
}
// ============================================================
// ????????
// ============================================================
void CollisionTestScene::RespawnEnemies()
{
    for (Entity e : m_Enemies)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_Enemies.clear();

    SpawnEnemy({ 0.0f, 3.0f, 8.0f }, true);   // ????(DPS ???)
    SpawnEnemy({ 8.0f, 3.0f,  4.0f });
    SpawnEnemy({ -8.0f, 3.0f,  5.0f });
    SpawnEnemy({ 4.0f, 3.0f, -7.0f });
}

// ============================================================
// ?????:?????????
// With VFX ? ON ???? emitter ????????????????????
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

        // VFX(= emitter)?????????????????????
        if (m_StressWithVFX)
            m_ProjectileVFXSystem.AttachVFX(m_Registry, p, m_StressVFXItem, m_VFXContext);
    }
}

// ============================================================
// ???????
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
// ?????????(????????)
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
// ImGui:??????
// ============================================================
// ============================================================
// ImGui:??????
// ============================================================
void CollisionTestScene::DrawBackpackPanel()
{
    if (!ImGui::CollapsingHeader("Backpack", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player)) return;
    if (!m_Registry.Has<BackpackComponent>(m_Player)) return;
    if (!m_Registry.Has<SpellbookComponent>(m_Player)) return;

    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto& book = m_Registry.Get<SpellbookComponent>(m_Player);

    // ============================================================
    // UI ???????
    // ??????????????????????????????????
    // ============================================================
    ImGui::Text("UI Stack :");
    {
        const auto& stack = m_UI.GetStack();
        if (stack.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(empty)");
        }
        for (size_t i = 0; i < stack.size(); ++i)
        {
            ImGui::SameLine();
            const bool top = (i + 1 == stack.size());
            ImGui::TextColored(top ? ImVec4(1, 0.9f, 0.3f, 1)
                : ImVec4(0.6f, 0.6f, 0.6f, 1),
                "[%s]", UIManager::LayerName(stack[i]));
        }
    }

    ImGui::Text("Open : %s   (Tab / I / E / Start)",
        m_UI.IsOpen(UILayer::Backpack) ? "YES" : "no");
    ImGui::Checkbox("Pause On Open", &m_PauseOnBackpack);
    ImGui::TextDisabled("Drag to move   RMB: remove   Wheel/R: rotate");
    ImGui::Text("Rotation : %d   %s", m_BackpackUI.GetRotation(),
        m_BackpackUI.IsDragging() ? "(dragging)" : "");

    ImGui::Separator();

    // ============================================================
    // ?????
    // ??????????????????????????
    //   Spell : ??????????
    //   Frame : ??????????????????
    // ============================================================
    int modeIdx = (int)m_BackpackUI.GetEditMode();
    const char* modeNames[] = { "Spell", "Frame" };
    if (ImGui::Combo("Edit Mode", &modeIdx, modeNames, 2))
        m_BackpackUI.SetEditMode((BackpackUI::EditMode)modeIdx);

    const bool frameMode = (m_BackpackUI.GetEditMode() == BackpackUI::EditMode::Frame);

    ImGui::TextDisabled(frameMode
        ? "Frames widen the placeable area. Spells ride along when moved."
        : "Spells can only be placed on top of a frame.");

    ImGui::Separator();

    // ============================================================
    // ????(?????????)
    //
    // x ??????????????= ??? - ????
    // ????????????????????????
    // ============================================================
    bool anyShown = false;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;

        if (ItemDatabase::IsFrame(id) != frameMode) continue;
        if (!book.HasLearned(id)) continue;

        anyShown = true;

        const int avail = m_BackpackUI.GetAvailableCount(bp, id);
        const bool usable = (avail > 0);

        // ??????????????(????????????????)
        const float dim = usable ? 1.0f : 0.35f;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(c->color.x * 0.6f * dim, c->color.y * 0.6f * dim,
                c->color.z * 0.6f * dim, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(c->color.x * 0.9f * dim, c->color.y * 0.9f * dim,
                c->color.z * 0.9f * dim, 1.0f));

        char label[128];
        sprintf_s(label, "%s  x%d", c->name, avail);

        if (ImGui::Button(label) && usable)
            m_BackpackUI.SetSelectedItem(id);

        ImGui::PopStyleColor(2);

        bool selected = m_BackpackUI.HasSelection()
            && m_BackpackUI.GetSelectedItem() == id;
        if (selected)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "<-");
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (!anyShown)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            frameMode ? "No frame learned yet" : "No spell learned yet");

    if (ImGui::Button("Clear Selection")) m_BackpackUI.ClearSelection();

    // ---- ???? ----
    ImGui::Separator();
    ImGui::Text("Spells : %zu   Frames : %zu", bp.items.size(), bp.frames.size());

    // ?????????????????????????????
    const int placeable = bp.PlaceableCount();
    const int total = BackpackComponent::GRID * BackpackComponent::GRID;
    ImGui::Text("Placeable : %d / %d cells", placeable, total);

    if (placeable == 0)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
            "No frame placed -> no spell can be placed");

    if (ImGui::Button("Clear Spells"))
    {
        bp.items.clear();
        BackpackLogic::RebuildOccupancy(bp);
        bp.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Frames"))
    {
        // ????????????????????????
        // ???????????(?????????????????)?
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        BackpackLogic::ValidateItems(bp);
        bp.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to 3x3"))
    {
        bp.items.clear();
        bp.frames.clear();
        BackpackLogic::RebuildOccupancy(bp);
        BackpackLogic::RebuildFrameOccupancy(bp);

        // ??????RectCentered ???????????????
        const int center = BackpackComponent::GRID / 2;
        BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, center, center, 0);
        bp.dirty = true;
    }

    // ---- ?????????????????? ----
    if (m_BackpackUI.GetLastEvicted() > 0)
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "%d spell(s) returned (lost their frame)", m_BackpackUI.GetLastEvicted());
        ImGui::SameLine();
        if (ImGui::Button("OK")) m_BackpackUI.ClearLastEvicted();
    }

    // ============================================================
    // ??(?????)
    // ???????????????????????????
    // ============================================================
    if (ImGui::TreeNode("Debug: Learn"))
    {
        ImGui::TextDisabled("Temporary. The level-up choice is the real path.");
        ImGui::TextDisabled("Reducing below the placed count is allowed;");
        ImGui::TextDisabled("it just blocks taking new ones out.");

        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;

            ImGui::PushID((int)id);
            if (ImGui::SmallButton("+")) book.Learn(id, 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) book.Forget(id, 1);
            ImGui::SameLine();

            const int owned = book.GetCount(id);
            const int placedN = ItemDatabase::IsFrame(id)
                ? BackpackLogic::CountPlacedFrames(bp, id)
                : BackpackLogic::CountPlaced(bp, id);

            ImGui::Text("%-14s owned %d / placed %d", c->name, owned, placedN);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // ---- ???? ----
    if (ImGui::TreeNode("Display"))
    {
        ImGui::Checkbox("Influence on hover only", &m_BackpackUI.showInfluenceOnHover);
        ImGui::TextDisabled("Showing every influence cell at once fills the grid");
        ImGui::TextDisabled("with color and hides which block affects which.");

        ImGui::Checkbox("Highlight influenced blocks", &m_BackpackUI.highlightInfluenced);
        ImGui::DragFloat("Drag Alpha", &m_BackpackUI.dragAlpha, 0.01f, 0.1f, 1.0f);
        ImGui::TreePop();
    }

    // ---- ????(???? ? ?)----
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

    // ---- ???????(?????)----
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
        ImGui::ColorEdit4("Locked Cell", &m_BackpackUI.lockedCellColor.x);

        ImGui::Text("Sprites : %u   Draw calls : %u",
            m_SpriteRenderer.GetLastSpriteCount(), m_SpriteRenderer.GetLastDrawCalls());
        ImGui::TreePop();
    }
}
// ============================================================
// ImGui:?(????????)
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

    // ---- ????? ----
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

    // ---- ???? ----
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

    // ---- AOE ?(AreaSystem ????????)----
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
// ImGui:?????(??? + ???)
// ????? PlayerStatsComponent??????????????
// ============================================================
void CollisionTestScene::DrawPlayerPanel()
{
    if (!ImGui::CollapsingHeader("Player", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player)) return;

    // ---- ????? ----
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

    // ---- ?? ----
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        char buf[64];
        sprintf_s(buf, "%.0f / %.0f", hp.current, hp.max);
        ImGui::ProgressBar(hp.current / hp.max, ImVec2(-1, 0), buf);
    }
    // ---- ??? ----
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Level %d", lv.level);

        char ebuf[64];
        sprintf_s(ebuf, "%.0f / %.0f", lv.experience, lv.ExpToNext());
        ImGui::ProgressBar(lv.Progress(), ImVec2(-1, 0), ebuf);

        if (lv.IsChoosing())
            ImGui::TextColored(ImVec4(1, 0.9f, 0.3f, 1),
                "Choosing a reward (%zu options)", lv.pendingChoices.size());

        ImGui::Text("Level ups : %d", m_LevelUpSystem.GetTotalLevelUps());
        ImGui::DragInt("Choice Count", &m_LevelUpSystem.choiceCount, 1, 1, 5);
        ImGui::DragFloat("Exp Base", &lv.expBase, 5.0f, 10.0f, 1000.0f);
        ImGui::DragFloat("Exp / Level", &lv.expPerLevel, 5.0f, 0.0f, 500.0f);

        if (ImGui::Button("+50 Exp")) lv.experience += 50.0f;
        ImGui::SameLine();
        if (ImGui::Button("Reset Level"))
        {
            lv.level = 1;
            lv.experience = 0.0f;
            lv.ClearChoices();
        }
    }
    // ---- ???(3?)----
    if (m_Registry.Has<PlayerStateComponent>(m_Player))
    {
        auto& state = m_Registry.Get<PlayerStateComponent>(m_Player);

        const char* moveNames[] = { "Idle", "Run", "Jump", "Fall" };
        const char* actionNames[] = { "None", "Casting" };
        const char* dmgNames[] = { "Normal", "Hurt", "Dead" };

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "State Machine (3 layers)");
        ImGui::Text("Move   : %-6s  %.2fs", moveNames[(int)state.move], state.moveTime);
        ImGui::Text("Action : %-8s  %.2fs", actionNames[(int)state.action], state.actionTime);

        ImVec4 dcol = state.IsDead() ? ImVec4(1, 0.3f, 0.3f, 1)
            : (state.damage == DamageStateID::Hurt) ? ImVec4(1, 0.8f, 0.3f, 1)
            : ImVec4(0.4f, 1, 0.4f, 1);
        ImGui::TextColored(dcol, "Damage : %-7s  %.2fs",
            dmgNames[(int)state.damage], state.damageTime);

        if (state.IsInvincible())
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 1, 1),
                "  invincible %.2fs", state.invincibleTimer);

        const uint32_t mask = state.SuppressMask();
        if (mask != Mask_None)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "  suppressing: %s%s",
                (mask & Mask_Move) ? "Move " : "", (mask & Mask_Action) ? "Action" : "");

        ImGui::DragFloat("Hurt Duration", &state.hurtDuration, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Invincible Time", &state.invincibleAfterHit, 0.05f, 0.0f, 5.0f);

        if (ImGui::Button("Take 10 Damage"))
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, 10.0f);
        ImGui::SameLine();
        if (ImGui::Button("Revive"))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            hp.current = hp.max;
            state.damage = DamageStateID::Normal;
            state.damageTime = 0.0f;
            state.invincibleTimer = 0.0f;
        }
    }

    // ---- ??? ----
    if (m_Registry.Has<PlayerStatsComponent>(m_Player))
    {
        auto& stats = m_Registry.Get<PlayerStatsComponent>(m_Player);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Stats (PlayerStatsComponent)");

        bool dirty = false;
        dirty |= ImGui::DragFloat("Radius", &stats.radius, 0.01f, 0.05f, 3.0f);
        dirty |= ImGui::DragFloat("Height", &stats.height, 0.01f, 0.05f, 5.0f);
        dirty |= ImGui::ColorEdit3("Color", m_PlayerColor);
        if (dirty) RebuildPlayerMesh();

        ImGui::DragFloat("Move Speed", &stats.moveSpeed, 0.1f, 0.0f, 30.0f);
        ImGui::DragFloat("Jump Power", &stats.jumpPower, 0.1f, 0.0f, 30.0f);
        ImGui::Text("Jump CD : %.2f", stats.jumpCooldown);
    }

    // ????????????????????????????
    ImGui::Separator();
    ImGui::DragFloat("Gravity (scene)", &m_Gravity, 0.5f, -100.0f, 0.0f);
}
// ============================================================
// ImGui:?????
// ============================================================
void CollisionTestScene::DrawStressPanel()
{
    if (!ImGui::CollapsingHeader("Stress Test"))
        return;

    // ---------- ????? ----------
    // ??? slider ???????????????????????
    // ???1????????????????????
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

    // ---------- ????? ----------
    ImGui::Separator();

    int projCount = CountProjectiles();
    ImGui::Text("Projectiles : %d   (pending %d)", projCount, m_StressPending);
    ImGui::Text("Exp Orbs    : %d", m_ExpOrbSystem.GetOrbCount());
    ImGui::Text("Colliders   : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs       : %zu", m_CollisionSystem.GetPairs().size());

    // ---------- ?????? ----------
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

    // ---------- ?????????? ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Pool Starvation Test");
    ImGui::TextDisabled("Keeps the dead list empty. This is the only state");
    ImGui::TextDisabled("where the EmitCS guard actually matters.");

    ImGui::Checkbox("Auto Refill", &m_StressAutoRefill);
    ImGui::SliderInt("Target Projectiles", &m_RefillTarget, 100, 4000);
    ImGui::SliderInt("Refill Batch", &m_RefillBatch, 10, 500);

    if (m_StressAutoRefill && !m_StressWithVFX)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
            "Auto Refill without VFX does not stress the pool!");

    // ---------- ?????? ----------
    ImGui::Separator();
    if (ImGui::TreeNode("Exp Orb"))
    {
        ImGui::TextDisabled("Orbs do not use CollisionSystem.");
        ImGui::TextDisabled("Only the distance to the player is needed,");
        ImGui::TextDisabled("and the CPU pair test would choke on the count.");

        ImGui::DragFloat("Attract Radius", &m_ExpOrbSystem.attractRadius, 0.1f, 0.5f, 30.0f);
        ImGui::DragFloat("Pickup Radius", &m_ExpOrbSystem.pickupRadius, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Accel", &m_ExpOrbSystem.accel, 1.0f, 1.0f, 200.0f);
        ImGui::DragFloat("Max Speed", &m_ExpOrbSystem.maxSpeed, 0.5f, 1.0f, 100.0f);
        ImGui::DragFloat("Orb Gravity", &m_ExpOrbSystem.gravity, 0.5f, -50.0f, 0.0f);
        ImGui::DragFloat("Orb Size", &m_ExpOrbSystem.orbSize, 0.01f, 0.05f, 2.0f);

        ImGui::TreePop();
    }

    // ---------- ?????????? ----------
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

    // ???? GPU ???????????????????????
    // ?????????????????????
    ImGui::TextDisabled("Alive count lives on the GPU only.");
    ImGui::TextDisabled("Judge by the screen and by Flush ms below.");

    // ---------- Flush ? CPU ?? ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Flush CPU Time");
    ImGui::TextDisabled("Flush only queues commands. It should stay near 0");
    ImGui::TextDisabled("even under load. If not, something waits for the GPU.");

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
    ImGui::Checkbox("Particle", &m_ShowParticle);
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
            case ItemCategory::Frame:      cat = "Frame";      break;
            case ItemCategory::Unknown:    cat = "Unknown";    break;
            }

            ImGui::TextColored(ImVec4(c->color.x, c->color.y, c->color.z, 1.0f),
                "%-14s [%s]  occupy=%zu  influence=%zu  icon=%s",
                c->name, cat, c->occupyCells.size(), c->influenceCells.size(),
                c->iconPath ? "yes" : "no");
        }
    }

    // ---------- ? ----------
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

    // ---------- ??? ----------
    if (ImGui::CollapsingHeader("Camera"))
    {
        ImGui::DragFloat("Distance", &m_Camera.distance, 0.1f, 1.0f, 30.0f);
        ImGui::DragFloat("Height", &m_Camera.height, 0.05f, 0.0f, 5.0f);
        ImGui::DragFloat("Stick Sens", &m_Camera.stickSensitivity, 1.0f, 10.0f, 500.0f);
        ImGui::DragFloat("Mouse Sens", &m_Camera.mouseSensitivity, 0.01f, 0.01f, 1.0f);
        ImGui::Checkbox("Invert Y", &m_Camera.invertY);
        ImGui::Text("Yaw/Pitch : %.1f / %.1f", m_Camera.GetYaw(), m_Camera.GetPitch());
    }

    // ---------- ??? ----------
    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::DragFloat3("Direction", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", m_LightColor);
        ImGui::DragFloat("Intensity", &m_LightIntensity, 0.02f, 0.0f, 5.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
    }

    ImGui::End();
}