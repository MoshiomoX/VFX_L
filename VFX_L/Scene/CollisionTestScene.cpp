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
#include "World/TerrainGenerator.h"

#include "Enemy/EnemyTags.h"
#include "Enemy/ChaseAIComponent.h"
#include <unordered_set>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <random>

// ============================================================
// Init
// ============================================================
void CollisionTestScene::Init()
{
    std::cout << "[CollisionTestScene] Init" << std::endl;

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();

    // ---------- 画面サイズを最初に確定させる ----------
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // ---------- アイテム定義の登録（他より先に行う）----------
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

    // ---------- 見た目 と 各 System が使う VFX の登録 ----------
    RegisterItemVisuals();


    // ---------- UI ----------
      // ※ItemDatabase::Initialize の後（LoadIcons が定義を読む）
    if (!m_GameUI.Initialize(device, context, m_ScreenW, m_ScreenH))
        std::cout << "[Error] GameUI init failed" << std::endl;


    // ---------- 使い回すモデル ----------
    m_EnemyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 1.0f, 0.35f, 0.35f, 1 });
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.40f, 1.00f, 1 });
    m_StressModel = PrimitiveBuilder::CreateSphere(device, 0.25f, { 1.0f, 0.50f, 0.20f, 1 });

    // ---------- 地形（格子对齐）----------
    // 場地: 100 x 100 マス = 200m x 200m（旧場地 24m の約8倍幅）。
    // 障害物の配置は seed で再現できる
    m_Grid.Init(100, 100);

    TerrainGenerator::Config tcfg;
    tcfg.seed = m_TerrainSeed;
    tcfg.obstacleCount = 40;
    TerrainGenerator::Generate(m_Registry, device, m_Grid, tcfg, m_Terrain);

    // ============================================================
    // プレイヤー
    // 組み立ては PlayerFactory に任せる。
    // シーンはどの Component が付いているかを知らなくてよい。
    // ============================================================
    PlayerFactory::Config pcfg;
    pcfg.color = { m_PlayerColor[0], m_PlayerColor[1], m_PlayerColor[2], 1.0f };

    m_Player = PlayerFactory::Create(m_Registry, device, pcfg);
}

// ============================================================
// 見た目と VFX を System に登録する
// アイテム定義側にある値をそのまま流す（ID ごとの対応表を作る）
// ============================================================
void CollisionTestScene::RegisterItemVisuals()
{
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        // --- 飛行物型: ビルボードの芯 + VFX ---
        if (auto* p = ItemDatabase::GetProjectile(id))
        {
            m_WeaponSystem.SetProjectileVisual(id,
                p->visualSize, p->common.color, p->visualStretch);

            if (p->vfxPath)
                m_ProjectileVFXSystem.RegisterVFX(id, p->vfxPath);
        }

        // --- AOE 型: VFX のみ（AreaSystem は未実装）---
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
    m_GameUI.Shutdown();
    m_ProjectileRenderer.Shutdown();
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// 画面サイズの変化に追従する
// UI は全部ピクセル指定なので、変わった時だけ組み直す。
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

    m_GameUI.Layout(w, h);
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

    // ---- UI（開閉・入力・プレイヤー消失時の後始末は全部 GameUI の中）----
    m_GameUI.Update(m_Registry, m_Player, dt);

    // ---- 集約: グリッドが変わっていれば杖を組み直す ----
    // UI の直後に置く。編成した結果を同じフレームで反映させるため
    m_BackpackAggregate.Update(m_Registry);

    if (!m_GameUI.ShouldPauseGame())
        UpdateGameplay(dt);

    DrawDebugUI();
}
// ============================================================
// 実際の gameplay 更新
// ============================================================
void CollisionTestScene::UpdateGameplay(float dt)
{
    // ---- 補充（枯渇状態を維持し続けるための自動生成）----
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

    // ---- 生成は小分けにする（1フレームに集中させない）----
    if (m_StressPending > 0)
    {
        int batch = (m_StressPending < 50) ? m_StressPending : 50;
        StressSpawnProjectiles(batch);
        m_StressPending -= batch;
    }

    // ============================================================
    // System の実行順（固定）
    // 操作 → 衝突 → 物理 → 状態機 → 杖 → 投射物 → VFX収集
    //      → 経験値 → カメラ → 粒子 Flush
    //
    // ※状態機は物理の後。isGrounded / velocity が
    //   今フレームの最終値になっている必要があるため。
    //   物理より前に置くと接地判定が1フレーム古くなり、
    //   着地の見た目がずれる。
    //
    // ※能力値の注入（SetMoveSpeed 等）は行わない。
    //   PlayerControlSystem が PlayerStatsComponent を直接読む。
    // ============================================================
    m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

    m_ChaseAISystem.Update(m_Registry, dt);
        // 湧き管理（環帯生成 + 押し出し）。敵の組み立ては SpawnEnemy に委ねる
    if (m_Registry.IsValid(m_Player))
    {
        m_SpawnDirector.Update(m_Registry, m_Grid,
            m_Registry.Get<TransformComponent>(m_Player).position, dt,
            [this](const Vector3& pos) { SpawnEnemy(pos); });
    }
    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);
   
    m_ManaSystem.Update(m_Registry, dt);
    // 生まれたばかりの投射物に VFX を取り付ける（WeaponSystem の直後）
    for (const auto& sp : m_WeaponSystem.GetSpawned())
        m_ProjectileVFXSystem.AttachVFX(m_Registry, sp.entity, sp.id, m_VFXContext);

    m_ProjectileSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ============================================================
    // 命中イベントの消費: ダメージ + 死亡処理
    //
    // ※プレイヤーは PlayerStateSystem::TryApplyHit を通す。
    //   無敵時間の判定を1ヶ所に閉じ込めるため。
    //   （2ヶ所に書くと必ず片方だけ直され、食い違う）
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

        // 無敵の的は 0 で止める（累計ダメージを読むための的）
        if (hp.invincible && hp.current < 0.0f)
            hp.current = 0.0f;

        if (hp.IsDead())
        {
            ExpOrbSystem::DropFrom(m_Registry, hit.target);
            m_Registry.Destroy(hit.target);
        }
    }

    // ============================================================
    // VFX: 各投射物の emitter を積む（Flush はまだ呼ばない）
    // ============================================================
    m_ProjectileVFXSystem.Update(m_Registry, dt, m_VFXContext);

    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();

    // ---- 経験値オーブ ----
    // 投射物の後。プレイヤーの位置が確定してから吸い寄せる。
    m_ExpOrbSystem.Update(m_Registry, dt);

    // ============================================================
    // レベルアップの判定（候補の抽選まで）
    // ============================================================
    m_LevelUpSystem.Update(m_Registry);

    // ---- カメラ追従（最後）----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);

    // ============================================================
    // 粒子は1フレームに1回だけ Flush する。
    // Flush はコマンドを積むだけの処理なので、
    // 0 に近いままのはず。伸びるなら GPU を待っている。
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        m_ParticleSystem.Flush(dt, m_TotalTime);

        auto t1 = std::chrono::high_resolution_clock::now();
        m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

        m_FlushMsAvg = m_FlushMsAvg * 0.95 + m_FlushMs * 0.05;
        if (m_FlushMs > m_FlushMsPeak) m_FlushMsPeak = m_FlushMs;
    }

    // ---- 衝突体のワイヤ表示 ----
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
                    // 投射物は数が多すぎるので描かない
                    if (m_Registry.Has<ProjectileComponent>(e)) return;

                    Color col = hitting.count(e) ? Color(1.0f, 0.3f, 0.3f, 1.0f)
                        : Color(0.4f, 1.0f, 0.4f, 1.0f);
                    DrawColliderDebug(e, col);
                });
    }

    if (m_ShowWandDebug)
        DrawWandDebug();
    if (m_Registry.IsValid(m_Player))
        m_Grid.DrawDebug(m_Registry.Get<TransformComponent>(m_Player).position);
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

    // ---- 1) モデル描画 ----
    if (m_ShowMesh)
        m_RenderSystem.Render(m_Registry, renderer);

    // ---- 2) ビルボード（投射物とオーブの芯）----
    if (m_ShowBillboard)
        m_ProjectileRenderer.Render(m_Registry, GetCamera());

    // ---- 3) 粒子（VFX 本体）----
    if (m_ShowParticle)
    {
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.Render();
    }

    // ============================================================
     // 4) UI（一番手前。Begin/End の管理は GameUI の中）
     // ============================================================
    m_GameUI.Render(m_Registry, m_Player);
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

        // 待機中の二重釈放を短い縦線で数える
        for (int k = 0; k < s.pendingCasts; ++k)
        {
            Vector3 p = origin + aim.dir * 0.4f + Vector3(0.15f * (float)k, 0.0f, 0.0f);
            dbg.AddDebugLine(p, p + Vector3(0.0f, 0.18f, 0.0f), Color(1.0f, 0.5f, 0.1f, 1.0f));
        }
    }
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
// 敵を1体作る
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

    if (!invincible)
        m_Registry.Add<ChaseAIComponent>(e, {});

    // 倒された時に落とす経験値。無ければ何も落とさない。
    ExpRewardComponent reward;
    reward.amount = 20.0f;
    reward.splitCount = 1;
    m_Registry.Add<ExpRewardComponent>(e, reward);

    ModelComponent mc;
    mc.model = invincible ? m_DummyModel : m_EnemyModel;
    m_Registry.Add<ModelComponent>(e, mc);
    // 死んだ分を間引く
    if (m_Enemies.size() > 128)
    {
        m_Enemies.erase(
            std::remove_if(m_Enemies.begin(), m_Enemies.end(),
                [this](Entity en) { return !m_Registry.IsValid(en); }),
            m_Enemies.end());
    }
    m_Enemies.push_back(e);
}
// ============================================================
// 敵を並べ直す
// 生成点を格子上のランダムな通行可能マスに取り、
// 各点に 1~4 体を点の周囲へ散らして出す。
//
// ※生成点の合法性は grid.IsWalkable で保証される
//   （障害物の中に敵が湧く事故はここで潰れている）
// ============================================================
void CollisionTestScene::RespawnEnemies()
{
    for (Entity e : m_Enemies)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_Enemies.clear();

    if (m_Registry.IsValid(m_Player))
    {
        Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        SpawnEnemy({ pp.x, 3.0f, pp.z + 8.0f }, true);
    }
}
// ============================================================
// 負荷テスト: 投射物をばら撒く
// With VFX が ON の時だけ emitter が積まれ、粒子側の経路に負荷がかかる
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

        // VFX（= emitter）を付けるかどうかで負荷の質が変わる
        if (m_StressWithVFX)
            m_ProjectileVFXSystem.AttachVFX(m_Registry, p, m_StressVFXItem, m_VFXContext);
    }
}

// ============================================================
// 既定値セットを適用する
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
// 投射物の数を数える（表示用なので毎フレームでよい）
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
// ImGui: 杖（集約の結果を読むだけ）
// ============================================================
void CollisionTestScene::DrawWandPanel()
{
    if (!ImGui::CollapsingHeader("Wand (result of aggregation)", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<WandComponent>(m_Player))
        return;

    auto& w = m_Registry.Get<WandComponent>(m_Player);

    // マナは使い手のもの。ここでは持続判定のために regen を読むだけ
    const float regen = m_Registry.Has<ManaComponent>(m_Player)
        ? m_Registry.Get<ManaComponent>(m_Player).regen : 0.0f;

    ImGui::DragFloat("Range", &w.range, 0.5f, 1.0f, 60.0f);

    // ---- 発射の仕方 ----
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

    // ---- AOE 型（AreaSystem が未実装なので表示だけ）----
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

    bool sustainable = totalDrain <= regen;
    ImGui::TextColored(sustainable ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(1, 0.4f, 0.4f, 1),
        "Total Drain %.1f/s  vs  Regen %.1f/s   %s",
        totalDrain, regen, sustainable ? "(sustainable)" : "(will run dry)");
}
// ============================================================
// ImGui: プレイヤー（状態機 + 能力値）
// 能力値は PlayerStatsComponent を直接いじる。シーンは持たない。
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

    // ---- 魔力 ----
    // 実行時に書くのは ManaSystem だけ。
    // ここで max / regen を触るのはデバッグ調整の例外（hp や stats と同じ扱い）。
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mana = m_Registry.Get<ManaComponent>(m_Player);
        char mbuf[64];
        sprintf_s(mbuf, "%.0f / %.0f", mana.current, mana.max);
        ImGui::ProgressBar((mana.max > 0.0f) ? mana.current / mana.max : 0.0f,
            ImVec2(-1, 0), mbuf);
        ImGui::DragFloat("Mana Max", &mana.max, 1.0f, 10.0f, 1000.0f);
        ImGui::DragFloat("Mana Regen", &mana.regen, 0.5f, 0.0f, 300.0f);
    }
    // ---- 経験値とレベル ----
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

    // ---- 状態機（3層）----
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

    // ---- 能力値 ----
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

    // 重力はプレイヤーの能力ではなく環境の値なのでシーンが持つ
    ImGui::Separator();
    ImGui::DragFloat("Gravity (scene)", &m_Gravity, 0.5f, -100.0f, 0.0f);
}

// ============================================================
// ImGui: 負荷テスト
// ============================================================
void CollisionTestScene::DrawStressPanel()
{
    if (!ImGui::CollapsingHeader("Stress Test"))
        return;

    // ---------- 既定値セット ----------
    // 毎回 slider を合わせ直すと条件がぶれて比較にならない。
    // ボタン1つで同じ条件を再現できるようにする。
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

    // ---------- 現在の数 ----------
    ImGui::Separator();

    int projCount = CountProjectiles();
    ImGui::Text("Projectiles : %d   (pending %d)", projCount, m_StressPending);
    ImGui::Text("Exp Orbs    : %d", m_ExpOrbSystem.GetOrbCount());
    ImGui::Text("Colliders   : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs       : %zu", m_CollisionSystem.GetPairs().size());

    // ---------- 生成の中身 ----------
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

    // ---------- プール枯渇テスト ----------
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

    // ---------- 経験値オーブ ----------
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

    // ---------- 粒子システムの状態 ----------
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

    // 生存数の真実は GPU 上にしか無い。数字を出すと嘘になる。
    // 画面の見た目と下の Flush ms で判断する。
    ImGui::TextDisabled("Alive count lives on the GPU only.");
    ImGui::TextDisabled("Judge by the screen and by Flush ms below.");

    // ---------- Flush の CPU 時間 ----------
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
// ImGui: 全体
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
    DrawWandPanel();
    m_GameUI.DrawDebugUI(m_Registry, m_Player, m_BackpackAggregate);
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

    // ---------- 地形 ----------
    if (ImGui::CollapsingHeader("Terrain"))
    {
        ImGui::Text("Grid : %d x %d  (%.0fm x %.0fm)",
            m_Grid.Width(), m_Grid.Depth(),
            m_Grid.WorldWidth(), m_Grid.WorldDepth());

        int seed = (int)m_TerrainSeed;
        if (ImGui::InputInt("Seed", &seed)) m_TerrainSeed = (uint32_t)(seed < 0 ? 0 : seed);

        if (ImGui::Button("Regenerate"))
        {
            // 古い地形を全部消して作り直す。
            // 障害物の中に取り残された敵が出るので、敵も湧き直す
            for (Entity e : m_Terrain)
                if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
            m_Terrain.clear();
            m_Grid.ClearAll();

            auto* device = Application::Get().GetGraphics().GetDevice();
            TerrainGenerator::Config tcfg;
            tcfg.seed = m_TerrainSeed;
            tcfg.obstacleCount = 40;
            TerrainGenerator::Generate(m_Registry, device, m_Grid, tcfg, m_Terrain);

            RespawnEnemies();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("seed reproducible");
    }
    // ---------- 敵 ----------
    if (ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // ---- 湧き管理 ----
        ImGui::Checkbox("Director Enabled", &m_SpawnDirector.enabled);
        ImGui::Text("Mobs : %d / %d   (spawned %d, evicted %d)",
            m_SpawnDirector.GetLastMobCount(), m_SpawnDirector.spawnCap,
            m_SpawnDirector.GetTotalSpawned(), m_SpawnDirector.GetTotalEvicted());
        ImGui::DragInt("Spawn Cap", &m_SpawnDirector.spawnCap, 1, 0, 500);
        ImGui::DragFloat("Interval", &m_SpawnDirector.spawnInterval, 0.05f, 0.1f, 10.0f);
        ImGui::DragInt("Per Tick", &m_SpawnDirector.spawnPerTick, 1, 1, 20);
        ImGui::DragFloat("Ring Min", &m_SpawnDirector.rMin, 0.5f, 5.0f, 100.0f);
        ImGui::DragFloat("Ring Max", &m_SpawnDirector.rMax, 0.5f, 5.0f, 120.0f);
        ImGui::Separator();

        ImGui::DragFloat("Separation Radius", &m_ChaseAISystem.separationRadius, 0.05f, 0.5f, 5.0f);
        ImGui::DragFloat("Separation Power", &m_ChaseAISystem.separationPower, 0.1f, 0.0f, 20.0f);
        ImGui::TextDisabled("chase/detect ranges live on each ChaseAIComponent");
        ImGui::Separator();
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

    // ---------- 照明 ----------
    if (ImGui::CollapsingHeader("Lighting"))
    {
        ImGui::DragFloat3("Direction", m_LightDir, 0.02f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", m_LightColor);
        ImGui::DragFloat("Intensity", &m_LightIntensity, 0.02f, 0.0f, 5.0f);
        ImGui::ColorEdit3("Ambient", m_AmbientColor);
    }
    ImGui::End();
}
