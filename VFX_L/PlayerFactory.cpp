// ============================================================
// PlayerFactory.cpp
// ============================================================
#include "PlayerFactory.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "RigidbodyComponent.h"
#include "ModelComponent.h"
#include "PlayerTag.h"
#include "PlayerStatsComponent.h"
#include "PlayerStateComponent.h"
#include "HealthComponent.h"
#include "WandComponent.h"
#include "BackpackComponent.h"
#include "PrimitiveBuilder.h"
#include <iostream>

namespace PlayerFactory
{
    Entity Create(Registry& reg, ID3D11Device* device, const Config& cfg)
    {
        Entity e = reg.Create();

        // ---- 位置 ----
        TransformComponent tf;
        tf.position = cfg.spawnPos;
        reg.Add<TransformComponent>(e, tf);

        // ============================================================
        // 衝突体（垂直カプセル）
        //
        // ★mask から Layer_PlayerShot を外している。
        //   muzzleOffset が {0, 0.5, 0} = カプセル内部なので、
        //   自分の弾を検出すると発射の瞬間に必ず自傷する。
        //   投射物側の mask（Enemy|Terrain）だけに頼らないこと。
        //   衝突判定が双方向か片方向かは CollisionSystem の実装依存で、
        //   ここで明示的に外しておけばどちらでも安全。
        // ============================================================
        ColliderComponent col;
        col.shape = ColliderShape::Capsule;
        col.radius = cfg.radius;
        col.height = cfg.height;
        col.layer = Layer_Player;
        col.mask = Layer_Enemy | Layer_EnemyShot | Layer_Terrain;
        reg.Add<ColliderComponent>(e, col);

        // ---- 物理 ----
        // Slide：壁で止まり床に立つ。キャラクター用。
        RigidbodyComponent rb;
        rb.useGravity = true;
        rb.response = ResponseMode::Slide;
        reg.Add<RigidbodyComponent>(e, rb);

        // ---- 見た目 ----
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateCapsule(device,
            cfg.radius, cfg.height, cfg.color);
        reg.Add<ModelComponent>(e, mc);

        // ---- タグ ----
        reg.Add<PlayerTag>(e, {});

        // ---- 能力値 ----
        // ★以降 Scene も System もこの値のコピーを持たない
        PlayerStatsComponent stats;
        stats.moveSpeed = cfg.moveSpeed;
        stats.jumpPower = cfg.jumpPower;
        stats.radius = cfg.radius;
        stats.height = cfg.height;
        reg.Add<PlayerStatsComponent>(e, stats);

        // ---- 状態機（HSM、3層）----
        // ★PlayerStateSystem が物理の後に進める。
        reg.Add<PlayerStateComponent>(e, {});

        // ---- 体力 ----
        // ★従来プレイヤーには付いていなかった。
        //   HitEvent の消費側が Has<HealthComponent> で弾くため、
        //   敵の攻撃が当たっても何も起きない状態だった。
        HealthComponent hp;
        hp.max = cfg.maxHealth;
        hp.current = cfg.maxHealth;
        reg.Add<HealthComponent>(e, hp);

        // ---- 杖（空で作る）----
        // spells / areas はバックパック集約の結果で埋まる。
        // castMode の既定は Auto（WandComponent 側の初期値）。
        if (cfg.withWand)
            reg.Add<WandComponent>(e, {});

        // ---- バックパック ----
        if (cfg.withBackpack)
            reg.Add<BackpackComponent>(e, {});

        std::cout << "[PlayerFactory] player created (entity " << e << ")" << std::endl;
        return e;
    }

    // ============================================================
    // メッシュと衝突体の作り直し
    // 寸法は PlayerStatsComponent から取る（Config ではない）
    // ============================================================
    void RebuildVisual(Registry& reg, Entity player, ID3D11Device* device,
        const Vector4& color)
    {
        if (!reg.IsValid(player)) return;
        if (!reg.Has<PlayerStateComponent>(player)) return;

        const auto& stats = reg.Get<PlayerStatsComponent>(player);

        if (reg.Has<ColliderComponent>(player))
        {
            auto& col = reg.Get<ColliderComponent>(player);
            col.radius = stats.radius;
            col.height = stats.height;
        }

        if (reg.Has<ModelComponent>(player))
        {
            auto& mc = reg.Get<ModelComponent>(player);
            mc.model = PrimitiveBuilder::CreateCapsule(device,
                stats.radius, stats.height, color);
        }
    }
}