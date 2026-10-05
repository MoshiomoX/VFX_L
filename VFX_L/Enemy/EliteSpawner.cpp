// ============================================================
// EliteSpawner.cpp
// ============================================================
#include "Enemy/EliteSpawner.h"
#include "Enemy/EnemyTags.h"
#include "ECS/View.h"
#include "ECS/System/MeshVFXSystem.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/ModelComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/HealthComponent.h"
#include "Component/DissolveComponent.h"
#include "Item/ExpRewardComponent.h"
#include "Player/PlayerTag.h"
#include "Debug/TestSpawner.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Model/SkinnedModelGPU.h"
#include "Manager/ResourceManager.h"
#include "Core/Application.h"
#include "VFX_Editor/VFXId.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <algorithm>

using DirectX::SimpleMath::Vector3;

void EliteSpawner::Init(ID3D11Device* device)
{
    m_Elites.clear();
    m_DummyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f, { 0.7f, 0.40f, 1.00f, 1 });
}

// ============================================================
// 的を並べ直す（プレイヤーの前に1体）
// ============================================================
void EliteSpawner::Respawn(Registry& reg, const Vector3* player)
{
    for (Entity e : m_Elites)
        if (reg.IsValid(e)) reg.Destroy(e);
    m_Elites.clear();

    if (player)
        Spawn(reg, { player->x, 3.0f, player->z + 8.0f });
}

// ============================================================
// エリート（無敵の的）を1体作る
// 雑魚はここでは作らない。雑魚は SwarmSystem::SpawnEnemy へ
// ============================================================
void EliteSpawner::Spawn(Registry& reg, const Vector3& pos)
{
    Entity e = TestSpawner::SpawnCapsule(reg, pos, 0.4f, 1.0f);

    auto& col = reg.Get<ColliderComponent>(e);
    col.layer = Layer_Enemy;
    col.mask = Layer_All;

    // ---- 物理から外す ----
    // 的は動かない。PhysicsSystem の押し出しに参加させる理由が無い
    auto& rb = reg.Get<RigidbodyComponent>(e);
    rb.isStatic = true;
    rb.useGravity = false;

    // 無敵。累計ダメージを読むための的なので HP は減るが 0 で止まる
    HealthComponent hp;
    hp.invincible = true;
    hp.max = 9999.0f;
    hp.current = 9999.0f;
    reg.Add<HealthComponent>(e, hp);

    // 消えない側（SpawnDirector の計数外）
    reg.Add<EliteTag>(e, {});

    ExpRewardComponent reward;
    reward.amount = 20.0f;
    reward.splitCount = 1;
    reg.Add<ExpRewardComponent>(e, reward);

    // 見た目: 骨付きの Skeleton_Warrior（Idle ループ）。読めなければ従来のカプセル
    if (!AttachVisual(reg, e))
    {
        ModelComponent mc;
        mc.model = m_DummyModel;
        reg.Add<ModelComponent>(e, mc);
    }

    m_Elites.push_back(e);
}

// ============================================================
// エリートの骨付きモデル
// プレイヤーと同じ SkinnedAnimComponent 経路（SkinnedAnimSystem が時計、RenderSystem が描画）。
// ステートマシンは無いので base 層に Idle を流すだけ。的なのでプレイヤーの方（-Z）を向かせる
// ============================================================
bool EliteSpawner::AttachVisual(Registry& reg, Entity e)
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
    reg.Add<SkinnedAnimComponent>(e, anim);

    if (reg.Has<TransformComponent>(e))
        reg.Get<TransformComponent>(e).rotation.y = 180.0f;   // プレイヤーの方を向く
    return true;
}

// ============================================================
// 死亡 → 燃焼消滅
// HP が尽きた CPU 実体（エリートなど。プレイヤーは PlayerStateSystem が扱う）に
// DissolveComponent + DeathBurn を付ける。消え終わったら MeshVFXSystem が実体を破棄する
// ============================================================
void EliteSpawner::UpdateDeaths(Registry& reg, MeshVFXSystem& meshVfx, const VFXContext& ctx)
{
    std::vector<Entity> dead;
    reg.CreateView<HealthComponent, ModelComponent>()
        .Each([&](Entity e, HealthComponent& hp, ModelComponent&)
            {
                if (!hp.IsDead()) return;
                if (reg.Has<PlayerTag>(e)) return;
                if (reg.Has<DissolveComponent>(e)) return;   // 既に燃えている
                dead.push_back(e);
            });
    for (Entity e : dead)
        meshVfx.StartBurn(reg, e, VFXId::DeathBurn, ctx, m_BurnDuration);
}

// ============================================================
// ImGui: Enemies パネルのエリート（CPU）の段
// ============================================================
bool EliteSpawner::DrawImGui(Registry& reg, const MeshVFXSystem& meshVfx)
{
    int alive = 0;
    for (size_t i = 0; i < m_Elites.size(); ++i)
    {
        Entity e = m_Elites[i];
        if (!reg.IsValid(e)) continue;
        ++alive;

        ImGui::PushID((int)i);
        auto& hp = reg.Get<HealthComponent>(e);

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
    ImGui::Text("Mesh VFX active : %zu", meshVfx.GetActiveCount());
    return ImGui::Button("Respawn Elites");
}
