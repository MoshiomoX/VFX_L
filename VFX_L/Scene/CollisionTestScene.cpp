// ============================================================
// CollisionTestScene.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"

#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/ModelComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/SkinnedModelGPU.h"
#include <algorithm>
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
#include "Component/DissolveComponent.h"
#include "Component/ManaComponent.h"
#include "ECS/View.h"
#include "VFX_Editor/VFXId.h"
#include "VFX_Editor/VFXDatabase.h"
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
#include "Item/ExpRewardComponent.h"
#include "World/TerrainGenerator.h"
#include "Scene/RunResult.h"

#include "Enemy/EnemyTags.h"
#include <unordered_set>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <random>
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"

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

    // 燃焼消滅（Mesh 発射 + 溶解の縁）。道具ではないので VFXDatabase から直接引く
    if (const char* path = VFXDatabase::GetPath(VFXId::DeathBurn))
        m_MeshVFXSystem.RegisterVFX(VFXId::DeathBurn, path);

    // ---------- UI ----------
    // ※ItemDatabase::Initialize の後（LoadIcons が定義を読む）
    if (!m_GameUI.Initialize(device, context, m_ScreenW, m_ScreenH))
        std::cout << "[Error] GameUI init failed" << std::endl;

    // ---------- 使い回すモデル ----------
    // 雑魚のモデルは SwarmSystem が自前で持つ。ここには置かない
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.40f, 1.00f, 1 });

    // ---------- 地形（格子对齐）----------
    // 場地: 100 x 100 マス = 200m x 200m（旧場地 24m の約8倍幅）。
    // 障害物の配置は seed で再現できる
    m_Grid.Init(100, 100);

    TerrainGenerator::Config tcfg;
    tcfg.seed = m_TerrainSeed;
    tcfg.obstacleCount = 40;
    TerrainGenerator::Generate(m_Registry, device, m_Grid, tcfg, m_Terrain);

    // ---------- GPU 側 gameplay（雑魚・投射物・オーブ）----------
    // ※地形の生成後に呼ぶ。格子表をそのまま上げるため
    m_Swarm.SetParticleSystem(&m_ParticleSystem);
    if (!m_Swarm.Initialize(device, context))
        std::cout << "[Error] SwarmSystem init failed" << std::endl;

    m_Swarm.UploadTerrain(m_Grid);
    m_Swarm.BuildVFXTable();
    // 範囲攻撃と投射物の飛び方（編集器で作った json）を GPU の表へ。
    // 先に範囲：投射物の「命中で出す範囲」が範囲の番号を引くため
    AreaProfileDB::LoadAll();
    m_Swarm.SetAreaDefs(AreaProfileDB::BuildDefs(m_Swarm.GetVFXTable()));
    ProjectileProfileDB::LoadAll();
    m_Swarm.SetMotions(ProjectileProfileDB::BuildMotions());
    m_WeaponSystem.SetAreaVFX(&m_AreaVFX, &m_VFXContext);
    m_Swarm.SetRecycleMinDist(m_SpawnDirector.rMax);
    m_WeaponSystem.SetSwarm(&m_Swarm);

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

            // CPU 経路（精英の弾など）は今まで通り VFXEffect を張る。
            // パスは VFXDatabase から引く（道具は ID しか知らない）
            if (const char* path = VFXDatabase::GetPath(p->vfxId))
                m_ProjectileVFXSystem.RegisterVFX(id, path);
        }

        // --- AOE 型: VFX のみ（AreaSystem は未実装）---
        if (auto* a = ItemDatabase::GetArea(id))
        {
            if (const char* path = VFXDatabase::GetPath(a->vfxId))
                m_ProjectileVFXSystem.RegisterVFX(id, path);
        }
    }
}

// ============================================================
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    m_Swarm.Shutdown();
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

    // ---- 死亡 → 倒れた姿を少し見せてからリザルトへ ----
    // 一時停止中でも進める（三択を開いたまま死ぬ事は無いが、止まると戻れない）
    if (m_Registry.IsValid(m_Player) && m_Registry.Has<PlayerStateComponent>(m_Player)
        && m_Registry.Get<PlayerStateComponent>(m_Player).IsDead())
    {
        m_DeathTimer += dt;
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }

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
    // 上限は SwarmSystem::SpawnProjectile の1フレーム受付数。超えた分は静かに捨てられる
    if (m_StressPending > 0)
    {
        const int maxPerFrame = (int)Swarm::kMaxSpawnProjPerFrame;
        int batch = (m_StressPending < maxPerFrame) ? m_StressPending : maxPerFrame;
        StressSpawnProjectiles(batch);
        m_StressPending -= batch;
    }

    // ============================================================
    // System の実行順（固定）
    // 操作 → 湧き依頼 → 衝突 → 物理 → 状態機 → 杖 → アニメ → マナ結算 → レベル判定
    //      → カメラ → GPU gameplay Flush → 粒子 Flush
    //
    // ※雑魚の AI は GPU（SwarmEnemyAICS）。CPU には無い。
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

    // ---- 湧き管理 ----
    // 数えるのは GPU の存活数（回読なので 1〜2 フレーム古い）。
    // 空き枠が無い分は「遠い雑魚の転送」として GPU に投げる
    if (m_Registry.IsValid(m_Player))
    {
        m_Swarm.SetRecycleMinDist(m_SpawnDirector.rMax);

        const float groundY = m_Swarm.GetAIParams().groundY;
        m_SpawnDirector.Update(m_Grid,
            m_Registry.Get<TransformComponent>(m_Player).position, dt,
            (int)m_Swarm.GetCounters().aliveEnemies,
            [this, groundY](const Vector3& pos)
            {
                m_Swarm.SpawnEnemy({ pos.x, groundY, pos.z }, m_MobHp, m_MobSpeed);
            },
            [this, groundY](const Vector3& pos)
            {
                m_Swarm.RecycleEnemy({ pos.x, groundY, pos.z }, m_MobHp, m_MobSpeed);
            });
    }

    m_CollisionSystem.Update(m_Registry);

    m_PhysicsSystem.SetGravity(m_Gravity);
    m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);

    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ---- 見た目のアニメ（杖の後: 「今フレーム撃った」を拾うため）----
    m_PlayerAnimSystem.Update(m_Registry, dt);
    m_SkinnedAnimSystem.Update(m_Registry, dt);

    m_ManaSystem.Update(m_Registry, dt);

    // 投射物の生成・移動・命中・撃破報酬は全部 GPU（SwarmSystem）。
    // CPU 側にはもう無い。精英の弾を CPU に戻す時はここに書く

    m_LastEmitterCount = m_ParticleSystem.GetPendingEmitterCount();
    m_LastDropped = m_ParticleSystem.GetDroppedEmitterCount();

    // ============================================================
    // レベルアップの判定（候補の抽選まで）
    // ============================================================
    m_LevelUpSystem.Update(m_Registry);

    // ---- カメラ追従（最後）----
    if (m_Registry.IsValid(m_Player))
        m_Camera.SetFollowTarget(m_Registry.Get<TransformComponent>(m_Player).position);
    m_Camera.Update(dt);

    // ============================================================
    // GPU 側 gameplay の Flush（粒子と同じ位置、粒子より前）
    //
    // 中で固定ステップを回す。渡すのは実 dt でよい。
    // 玩家の位置はここまでで確定しているので、この時点の値を上げる。
    //
    // 回読は 1〜2 フレーム古い。被弾は TryApplyHit へ流し、
    // 無敵時間の判定は向こうに任せる（CPU 側の接触ダメージと同じ窓口）
    // ============================================================
    if (m_Registry.IsValid(m_Player))
    {
        const auto& ptf = m_Registry.Get<TransformComponent>(m_Player);

        float playerRadius = 0.4f;
        if (m_Registry.Has<ColliderComponent>(m_Player))
            playerRadius = m_Registry.Get<ColliderComponent>(m_Player).radius;

        bool playerAlive = true;
        if (m_Registry.Has<PlayerStateComponent>(m_Player))
            playerAlive = !m_Registry.Get<PlayerStateComponent>(m_Player).IsDead();
      
        // 玩家の体格は PlayerStats が持つ。接触判定用に毎フレーム GPU 側へ渡す
        if (m_Registry.Has<PlayerStatsComponent>(m_Player))
            m_Swarm.GetAIParams().playerCapsuleHalf =
            m_Registry.Get<PlayerStatsComponent>(m_Player).height * 0.5f;
       
        m_Swarm.Flush(ptf.position, playerRadius, playerAlive, dt, m_TotalTime);

        // GPU 上で受けたダメージを CPU の玩家へ反映
        const float gpuDamage = m_Swarm.ConsumePlayerDamage();
        if (gpuDamage > 0.0f)
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, gpuDamage);
        // GPU 上で拾った経験値を CPU の玩家へ反映。
        // レベルアップの判定は LevelUpSystem（次フレーム頭）に任せて、ここは足すだけ
        const float gpuExp = m_Swarm.ConsumeExp();
        if (gpuExp > 0.0f) m_ExpGained += gpuExp;   // 戦績用
        if (gpuExp > 0.0f && m_Registry.Has<LevelComponent>(m_Player))
            m_Registry.Get<LevelComponent>(m_Player).experience += gpuExp;
    }

    // ============================================================
    // 死亡 → 燃焼消滅
    // HP が尽きた CPU 実体（精英など。玩家は PlayerStateSystem が扱う）に
    // DissolveComponent + DeathBurn を付ける。消え終わったら MeshVFXSystem が実体を破棄する
    // ============================================================
    {
        std::vector<Entity> dead;
        m_Registry.CreateView<HealthComponent, ModelComponent>()
            .Each([&](Entity e, HealthComponent& hp, ModelComponent&)
                {
                    if (!hp.IsDead()) return;
                    if (m_Registry.Has<PlayerTag>(e)) return;
                    if (m_Registry.Has<DissolveComponent>(e)) return;   // 既に燃えている
                    dead.push_back(e);
                });
        for (Entity e : dead)
            m_MeshVFXSystem.StartBurn(m_Registry, e, VFXId::DeathBurn, m_VFXContext, m_BurnDuration);
    }
    m_MeshVFXSystem.Update(m_Registry, dt, m_VFXContext);

    // Mesh 発射の動作確認（Flush の前に積む）
    UpdateMeshEmitTest(dt);

    // ============================================================
    // 粒子は1フレームに1回だけ Flush する。
    // Flush はコマンドを積むだけの処理なので、
    // 0 に近いままのはず。伸びるなら GPU を待っている。
    // ============================================================
    {
        auto t0 = std::chrono::high_resolution_clock::now();

        // 範囲攻撃の見た目（emitter を積むので粒子の Flush より前）
        m_AreaVFX.Update(dt, m_Registry.IsValid(m_Player)
            ? m_Registry.Get<TransformComponent>(m_Player).position : Vector3::Zero);

        m_ParticleSystem.Flush(dt, m_TotalTime);

        // 点光源: CPU 側の VFX（範囲・精英）はもう積み終わっている。弾と範囲の分を GPU で追記
        m_Swarm.CollectLights();

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
// Mesh 発射の動作確認（仮設）
// 玩家の胶囊 Mesh（VERTEX_3D）を発射源に登録し、
// TransformComponent から作った世界行列を毎フレーム emitter に渡す。
// 期待：粒子が胶囊の表面から法線方向に出て、玩家が向きを変えると付いて回る
// ============================================================
void CollisionTestScene::UpdateMeshEmitTest(float dt)
{
    if (!m_MeshEmitTest)
    {
        if (m_MeshEmitSourceId >= 0)
        {
            m_ParticleSystem.UnregisterEmitSource(m_MeshEmitSourceId);
            m_MeshEmitSourceId = -1;
            m_MeshEmitModel.reset();
        }
        return;
    }

    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<ModelComponent>(m_Player))
        return;

    auto model = m_Registry.Get<ModelComponent>(m_Player).model;
    if (!model || model->GetSubMeshes().empty() || !model->GetSubMeshes()[0].mesh)
        return;

    // RebuildVisual でモデルが差し替わったら登録し直す
    if (model != m_MeshEmitModel)
    {
        if (m_MeshEmitSourceId >= 0)
            m_ParticleSystem.UnregisterEmitSource(m_MeshEmitSourceId);

        const auto& mesh = model->GetSubMeshes()[0].mesh;
        m_MeshEmitSourceId = m_ParticleSystem.RegisterEmitSource(
            mesh->GetVertexSRV(), mesh->GetVertexCount(), GPUParticleSystem::kLayoutStatic,
            mesh->GetIndexSRV(), mesh->GetIndexCount(), mesh->GetIndexBytes());
        m_MeshEmitModel = model;

        m_MeshEmitter.emitType = EmitType::Mesh;
        m_MeshEmitter.shape.sourceId = m_MeshEmitSourceId;
        m_MeshEmitter.shape.sourceCount = (int)mesh->GetVertexCount();
        m_MeshEmitter.shape.edgeMode = 0;
        m_MeshEmitter.position = { 0, 0, 0 };
        m_MeshEmitter.speedRange = { 0.3f, 1.0f };
        m_MeshEmitter.lifetimeRange = { 0.4f, 0.8f };
        m_MeshEmitter.sizeRange = { 0.06f, 0.10f, 0.0f, 0.02f };
        m_MeshEmitter.startColorMin = { 1.0f, 0.6f, 0.2f, 1.0f };
        m_MeshEmitter.startColorMax = { 1.0f, 0.9f, 0.4f, 1.0f };
        m_MeshEmitter.endColorMin = { 1.0f, 0.2f, 0.0f, 0.0f };
        m_MeshEmitter.endColorMax = { 1.0f, 0.4f, 0.0f, 0.0f };
        m_MeshEmitter.gravity = { 0, 0.5f, 0 };
        m_MeshEmitter.dragCoeff = 0.5f;
        m_MeshEmitter.atlasRows = 1;
        m_MeshEmitter.atlasCols = 1;
        m_MeshEmitter.atlasIndex = 0;
        m_MeshEmitter.colorKeyCount = 0;

        std::cout << "[MeshEmitTest] source id=" << m_MeshEmitSourceId
            << " verts=" << mesh->GetVertexCount() << std::endl;
    }

    if (m_MeshEmitSourceId < 0) return;

    // Transform::UpdateWorldMatrix と同じ式（scale * rot(yaw=y, pitch=x, roll=z) * trans）
    const auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    m_MeshEmitter.world =
        Matrix::CreateScale(tf.scale) *
        Matrix::CreateFromYawPitchRoll(
            DirectX::XMConvertToRadians(tf.rotation.y),
            DirectX::XMConvertToRadians(tf.rotation.x),
            DirectX::XMConvertToRadians(tf.rotation.z)) *
        Matrix::CreateTranslation(tf.position);

    m_MeshEmitter.emitRate = m_MeshEmitRate;
    m_MeshEmitter.Update(dt);

    std::vector<GPUEmitter> emitters{ m_MeshEmitter.ToGPU() };
    std::vector<ColorKey>   keys;
    m_ParticleSystem.SubmitEmitters(emitters, keys);
}

// ============================================================
// プレイ終了 → リザルトへ
// 戦績を g_LastRun に写してから切替を依頼する。2 回目以降は何もしない
// ============================================================
void CollisionTestScene::EndRun()
{
    if (m_RunEnded) return;
    m_RunEnded = true;

    g_LastRun.valid = true;
    g_LastRun.survivedSec = m_TotalTime;
    g_LastRun.level = (m_Registry.IsValid(m_Player) && m_Registry.Has<LevelComponent>(m_Player))
        ? m_Registry.Get<LevelComponent>(m_Player).level : 1;
    g_LastRun.kills = m_Swarm.GetCounters().killCount;   // 回読なので 1〜2 フレーム古い。許容
    g_LastRun.expGained = m_ExpGained;

    Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::RESULT);
    std::cout << "[CollisionTestScene] run ended: " << (int)m_TotalTime << "s, kills " << g_LastRun.kills << std::endl;
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

    // ---- 1) モデル描画（CPU の実体 + GPU の雑魚）----
    if (m_ShowMesh)
    {
        m_RenderSystem.Render(m_Registry, renderer);
        m_Swarm.Render(GetCamera(), renderer.GetLightData());
    }
    if (m_ShowSwarmDebug)
        m_Swarm.RenderDebug(GetCamera());

    // ---- 2) ビルボード（投射物とオーブの芯）----
    if (m_ShowBillboard)
        m_ProjectileRenderer.Render(m_Registry, GetCamera());

    // ---- 3) 粒子（VFX 本体）----
    if (m_ShowParticle)
    {
        m_ParticleSystem.SetCamera(GetCamera());
        m_ParticleSystem.SetLight(renderer.GetLightData());   // 立方体粒子の Lambert 用
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
    case ColliderShape::Convex:  dbg.DrawWireAABB(c, col.halfExtents, color); break;   // 包囲箱だけ
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
            : Color(1.0f, 1.0f, 0.2f, 1.0f);  // 精英 = 黄
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
// 精英（無敵の的）を1体作る
// 雑魚はここでは作らない。雑魚は SwarmSystem::SpawnEnemy へ
// ============================================================
void CollisionTestScene::SpawnElite(const Vector3& pos)
{
    Entity e = TestSpawner::SpawnCapsule(m_Registry, pos, 0.4f, 1.0f);

    auto& col = m_Registry.Get<ColliderComponent>(e);
    col.layer = Layer_Enemy;
    col.mask = Layer_All;

    // ---- 物理から外す ----
    // 的は動かない。PhysicsSystem の押し出しに参加させる理由が無い
    auto& rb = m_Registry.Get<RigidbodyComponent>(e);
    rb.isStatic = true;
    rb.useGravity = false;

    // 無敵。累計ダメージを読むための的なので HP は減るが 0 で止まる
    HealthComponent hp;
    hp.invincible = true;
    hp.max = 9999.0f;
    hp.current = 9999.0f;
    m_Registry.Add<HealthComponent>(e, hp);

    // 消えない側（SpawnDirector の計数外）
    m_Registry.Add<EliteTag>(e, {});

    ExpRewardComponent reward;
    reward.amount = 20.0f;
    reward.splitCount = 1;
    m_Registry.Add<ExpRewardComponent>(e, reward);

    // 見た目: 骨付きの Skeleton_Warrior（Idle ループ）。読めなければ従来のカプセル
    if (!AttachEliteVisual(e))
    {
        ModelComponent mc;
        mc.model = m_DummyModel;
        m_Registry.Add<ModelComponent>(e, mc);
    }

    m_Elites.push_back(e);
}

// ============================================================
// 精英の骨付きモデル
// 玩家と同じ SkinnedAnimComponent 経路（SkinnedAnimSystem が時計、RenderSystem が描画）。
// 状態機は無いので base 層に Idle を流すだけ。的なので玩家の方（-Z）を向かせる
// ============================================================
bool CollisionTestScene::AttachEliteVisual(Entity e)
{
    auto loaded = ResourceManager::Get().LoadModelAuto(Res::Mdl::KayKit_SkeletonWarrior);
    if (loaded.kind != ModelKind::Skinned || !loaded.skinnedModel) return false;

    auto& gfx = Application::Get().GetGraphics();
    auto gpu = std::make_shared<SkinnedModelGPU>();
    if (!gpu->Initialize(gfx.GetContext(), gfx.GetDevice(), *loaded.skinnedModel)) return false;

    SkinnedAnimComponent anim;
    anim.model = loaded.skinnedModel;
    anim.gpu = gpu;
    anim.yawOffsetDeg = 180.0f;                          // KayKit は -Z が正面
    anim.offset = { 0.0f, -(0.5f + 0.4f), 0.0f };        // SpawnCapsule(0.4, 1.0) の中心 → 足元
    anim.base.Play((std::max)(0, loaded.skinnedModel->FindClip("Idle")), true);
    m_Registry.Add<SkinnedAnimComponent>(e, anim);

    if (m_Registry.Has<TransformComponent>(e))
        m_Registry.Get<TransformComponent>(e).rotation.y = 180.0f;   // 玩家の方を向く
    return true;
}

// ============================================================
// 的を並べ直す（プレイヤーの前に1体）
// ============================================================
void CollisionTestScene::RespawnElites()
{
    for (Entity e : m_Elites)
        if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
    m_Elites.clear();

    if (m_Registry.IsValid(m_Player))
    {
        Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        SpawnElite({ pp.x, 3.0f, pp.z + 8.0f });
    }
}

// ============================================================
// 負荷テスト: 投射物をばら撒く
// 生成先は GPU（SwarmSystem）。弾1つにつき emitter が1つ積まれるので、
// 粒子側の経路（SwarmEmitCS / deadList）に負荷がかかる。
// 1フレームの受付上限は kMaxSpawnProjPerFrame。呼び出し側が小分けにする
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

        m_Swarm.SpawnProjectile(VFXId::Fireball, origin, dir * 8.0f, 1.0f, 0.25f, 30.0f);
    }
}

// ============================================================
// 既定値セットを適用する
// ============================================================
void CollisionTestScene::ApplyStressPreset(const StressPreset& p)
{
    m_Swarm.ClearProjectiles();
    m_StressPending = 0;

    m_RefillTarget = p.target;
    m_RefillBatch = p.batch;
    m_StressAutoRefill = p.autoRefill;

    m_FlushMsPeak = 0.0;
    m_FlushMsAvg = 0.0;
    m_RefillTimer = 0.0f;

    std::cout << "[Stress] preset applied: " << p.name
        << "  (" << p.purpose << ")" << std::endl;
}

// ============================================================
// 投射物の数: GPU の存活数（回読なので 1〜2 フレーム古い）
// ============================================================
int CollisionTestScene::CountProjectiles() const
{
    return (int)m_Swarm.GetCounters().aliveProjectiles;
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

    // ---- AOE 型（判定は GPU の Area。発動は WeaponSystem）----
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
        ImGui::TextDisabled("cast by WeaponSystem, damage on GPU (profile #%d)", a.profile);
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

    // ---- アニメ（状態機 → クリップの写像の確認用）----
    if (m_Registry.Has<SkinnedAnimComponent>(m_Player))
    {
        auto& anim = m_Registry.Get<SkinnedAnimComponent>(m_Player);
        auto clipName = [&](const SkinnedAnimLayer& L) -> const char*
            {
                return (anim.model && L.clip >= 0) ? anim.model->GetClipName(L.clip).c_str() : "-";
            };

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Animation (3 layers)");
        ImGui::Text("base  : %-18s %.2fs", clipName(anim.base), anim.base.time);
        ImGui::Text("upper : %-18s %.2fs  w=%.2f%s", clipName(anim.upper), anim.upper.time,
            anim.upper.weight, anim.upper.finished ? " (end)" : "");
        ImGui::Text("over  : %-18s %.2fs  w=%.2f%s", clipName(anim.over), anim.over.time,
            anim.over.weight, anim.over.finished ? " (end)" : "");
        ImGui::Checkbox("Show Skinned", &anim.visible);
        ImGui::SameLine();
        ImGui::DragFloat("Yaw Offset", &anim.yawOffsetDeg, 1.0f, -180.0f, 180.0f);
        ImGui::DragFloat3("Model Offset", &anim.offset.x, 0.01f);
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
    ImGui::Text("Colliders   : %zu", m_CollisionSystem.GetWorldColliders().size());
    ImGui::Text("Pairs       : %zu", m_CollisionSystem.GetPairs().size());

    // ---------- 生成 ----------
    ImGui::Separator();
    ImGui::SliderInt("Spawn Count", &m_StressCount, 50, 2000);
    if (ImGui::Button("+ Spawn")) m_StressPending += m_StressCount;
    ImGui::SameLine();
    if (ImGui::Button("Clear All"))
    {
        m_Swarm.ClearProjectiles();
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

    // ---------- 粒子システムの状態 ----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Particle System");

    float ratio = (float)m_LastEmitterCount / (float)m_ParticleSystem.GetMaxEmitters();
    ImVec4 col = (ratio > 0.9f) ? ImVec4(1, 0.4f, 0.4f, 1)
        : (ratio > 0.7f) ? ImVec4(1, 0.9f, 0.4f, 1)
        : ImVec4(0.4f, 1, 0.4f, 1);
    ImGui::TextColored(col, "Emitters : %zu / %zu",
        m_LastEmitterCount, m_ParticleSystem.GetMaxEmitters());

    if (m_LastDropped > 0)
        ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
            "Dropped : %zu  (some projectiles have no VFX)", m_LastDropped);

    ImGui::Text("Pool Size      : %u", m_ParticleSystem.GetMaxParticles());
    ImGui::Text("Billboards     : %u  (1 draw call)",
        m_ProjectileRenderer.GetLastDrawCount());

    // ---------- Mesh 発射の動作確認（仮設）----------
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Mesh Emit Test");
    ImGui::TextDisabled("Particles from the player capsule vertices (raw VB source).");
    ImGui::Checkbox("Emit from Player Mesh", &m_MeshEmitTest);
    ImGui::SliderFloat("Mesh Emit Rate", &m_MeshEmitRate, 0.0f, 5000.0f);
    ImGui::Text("source id : %d", m_MeshEmitSourceId);

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
    ImGui::Checkbox("Gamma (1/2.2)", &bp.gamma);

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
// ImGui: 全体
// ============================================================
void CollisionTestScene::DrawDebugUI()
{
    ImGui::Begin("Game Test");
    ImGui::SameLine();
    ImGui::Checkbox("Swarm Debug", &m_ShowSwarmDebug);
    ImGui::Checkbox("Mesh", &m_ShowMesh);
    ImGui::SameLine();
    ImGui::Checkbox("Billboard", &m_ShowBillboard);
    ImGui::SameLine();
    ImGui::Checkbox("Particle", &m_ShowParticle);
    ImGui::SameLine();
    ImGui::Checkbox("Collider", &m_ShowWireframe);
    ImGui::SameLine();
    ImGui::Checkbox("Wand Debug", &m_ShowWandDebug);
    ImGui::SameLine();
    if (ImGui::Button("End Run -> Result")) EndRun();   // リザルト画面の確認用
    ImGui::Separator();

    DrawPlayerPanel();
    DrawWandPanel();
    m_GameUI.DrawDebugUI(m_Registry, m_Player, m_BackpackAggregate);
    DrawStressPanel();

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

        ImGui::Separator();
        ImGui::TextDisabled("Test Fire: alive proj should rise to ~100");
        ImGui::TextDisabled("then fall back to 0 within 3 seconds.");
        ImGui::Text("requested %u   dispatched %u   steps %u",
            m_Swarm.GetTotalRequested(), m_Swarm.GetTotalDispatched(), m_Swarm.GetTotalSteps());
        // ---- 範囲攻撃の確認：道具を持っていなくても、編集器のプロファイルを直接出せる ----
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

        // 生成経路が通っているかの最短確認。玩家の周りへ放射状に撃つ
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
            // ※GPU 側は KillAll で全消し（雑魚・弾・オーブ）。
            //   counter は残るので撃破数などの累計は続く
            for (Entity e : m_Terrain)
                if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
            m_Terrain.clear();
            m_Grid.ClearAll();

            auto* device = Application::Get().GetGraphics().GetDevice();
            TerrainGenerator::Config tcfg;
            tcfg.seed = m_TerrainSeed;
            tcfg.obstacleCount = 40;
            TerrainGenerator::Generate(m_Registry, device, m_Grid, tcfg, m_Terrain);

            // GPU 側の格子表も差し替える（古い表のままだと弾が壁を抜ける）
            m_Swarm.KillAll();
            m_Swarm.UploadTerrain(m_Grid);
            m_Swarm.BuildVFXTable();
            RespawnElites();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("seed reproducible");
    }

    // ---------- 敵 ----------
    if (ImGui::CollapsingHeader("Enemies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        // ---- 湧き管理 ----
        ImGui::Checkbox("Director Enabled", &m_SpawnDirector.enabled);
        ImGui::Text("Mobs : %d / %d   (spawned %d, recycled %d)",
            m_SpawnDirector.GetLastMobCount(), m_SpawnDirector.spawnCap,
            m_SpawnDirector.GetTotalSpawned(), m_SpawnDirector.GetTotalRecycled());
        ImGui::DragInt("Spawn Cap", &m_SpawnDirector.spawnCap, 1, 0, 4096);
        ImGui::DragFloat("Interval", &m_SpawnDirector.spawnInterval, 0.05f, 0.1f, 10.0f);
        ImGui::DragInt("Per Tick", &m_SpawnDirector.spawnPerTick, 1, 1, 20);
        ImGui::DragFloat("Ring Min", &m_SpawnDirector.rMin, 0.5f, 5.0f, 100.0f);
        ImGui::DragFloat("Ring Max", &m_SpawnDirector.rMax, 0.5f, 5.0f, 120.0f);
        ImGui::TextDisabled("recycle: enemies farther than Ring Max get teleported");
        ImGui::Separator();

        // ---- 雑魚の初期値 ----
        ImGui::DragFloat("Mob HP", &m_MobHp, 1.0f, 1.0f, 1000.0f);
        ImGui::DragFloat("Mob Speed", &m_MobSpeed, 0.1f, 0.0f, 20.0f);
        ImGui::Separator();

        // ---- 雑魚 AI（GPU の定数。次の固定ステップから効く）----
        auto& ai = m_Swarm.GetAIParams();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Mob AI (GPU)");
        ImGui::DragFloat("Player Push Out", &ai.playerPushOut, 0.5f, 0.0f, 40.0f);
        ImGui::DragFloat("Separation Radius", &ai.separationRadius, 0.05f, 0.5f, 5.0f);
        ImGui::DragFloat("Separation Power", &ai.separationPower, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Avoid Power", &ai.avoidPower, 0.1f, 0.0f, 20.0f);
        ImGui::DragFloat("Look Ahead", &ai.lookAhead, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Ground Y", &ai.groundY, 0.01f, -1.0f, 3.0f);
        ImGui::DragFloat("Enemy Radius", &ai.enemyRadius, 0.01f, 0.1f, 2.0f);
        ImGui::DragFloat("Velocity Lag", &ai.velocityLag, 0.5f, 1.0f, 40.0f);
        ImGui::DragFloat("Max Speed Mul", &ai.maxSpeedMul, 0.05f, 1.0f, 4.0f);
        ImGui::DragFloat("Turn Speed", &ai.turnSpeed, 0.5f, 1.0f, 40.0f);

        ImGui::DragFloat("Contact Damage", &ai.contactDamage, 0.5f, 0.0f, 100.0f);
        ImGui::DragFloat("Attack Interval", &ai.attackInterval, 0.05f, 0.1f, 5.0f);
        ImGui::DragFloat("Hit Stun (s)", &ai.hitStun, 0.005f, 0.0f, 0.5f);
        ImGui::DragFloat("Hit Flash Gain", &ai.hitFlash, 0.1f, 1.0f, 10.0f);
        ImGui::Separator();

        // ---- 精英（CPU）----
        int alive = 0;
        for (size_t i = 0; i < m_Elites.size(); ++i)
        {
            Entity e = m_Elites[i];
            if (!m_Registry.IsValid(e)) continue;
            ++alive;

            ImGui::PushID((int)i);
            auto& hp = m_Registry.Get<HealthComponent>(e);

            ImGui::Text("Elite %u : %.0f / %.0f", e, hp.current, hp.max);
            ImGui::SameLine();
            ImGui::Checkbox("Invincible", &hp.invincible);

            if (hp.invincible)
            {
                ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f),
                    "   damage taken : %.0f", hp.max - hp.current);
                ImGui::SameLine();
                if (ImGui::Button("Reset HP")) hp.current = hp.max;
            }
            ImGui::SameLine();
            // 無敵を外して HP 0 → 次フレームの死亡判定で燃焼消滅が始まる
            if (ImGui::Button("Burn"))
            {
                hp.invincible = false;
                hp.current = 0.0f;
            }
            ImGui::PopID();
        }
        ImGui::Text("Elites alive : %d", alive);
        ImGui::SliderFloat("Burn Duration", &m_BurnDuration, 0.3f, 5.0f);
        ImGui::Text("Mesh VFX active : %zu", m_MeshVFXSystem.GetActiveCount());
        if (ImGui::Button("Respawn Elites")) RespawnElites();
        ImGui::SameLine();
        // GPU 側を全消し。counter は残るので kills (total) は減らない
        if (ImGui::Button("Kill All (GPU)")) m_Swarm.KillAll();
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

    DrawBloomPanel();
    ImGui::End();
}