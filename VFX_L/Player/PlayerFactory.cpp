// ============================================================
// PlayerFactory.cpp
// ============================================================
#include "Player/PlayerFactory.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/ModelComponent.h"
#include "Player/LevelComponent.h"
#include "Item/BackpackLogic.h"
#include "Player/PlayerTag.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Graphics/PrimitiveBuilder.h"
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
        // 衝突体（カプセル）
        //
        // ※mask に Layer_PlayerShot を入れない。
        //   muzzleOffset が {0, 0.5, 0} = 胴体の真ん中なので、
        //   撃った瞬間に自分の弾と重なっている。
        //   弾側の mask（Enemy|Terrain）にも Player が無いので、
        //   双方向で弾いているが、CollisionSystem は
        //   両方が許可した時だけ判定するので片方でも足りる。
        // ============================================================
        ColliderComponent col;
        col.shape = ColliderShape::Capsule;
        col.radius = cfg.radius;
        col.height = cfg.height;
        col.layer = Layer_Player;
        col.mask = Layer_Enemy | Layer_EnemyShot | Layer_Terrain;
        reg.Add<ColliderComponent>(e, col);

        // ---- 物理 ----
        // Slide: 壁に当たったら沿って滑る（跳ね返らない）
        RigidbodyComponent rb;
        rb.useGravity = true;
        rb.response = ResponseMode::Slide;
        reg.Add<RigidbodyComponent>(e, rb);

        // ---- 見た目 ----
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateCapsule(device,
            cfg.radius, cfg.height, cfg.color);
        reg.Add<ModelComponent>(e, mc);

        // ---- 目印 ----
        reg.Add<PlayerTag>(e, {});

        // ---- 能力値 ----
        // 以後は Scene も System も値を持たない。ここが真値。
        PlayerStatsComponent stats;
        stats.moveSpeed = cfg.moveSpeed;
        stats.jumpPower = cfg.jumpPower;
        stats.radius = cfg.radius;
        stats.height = cfg.height;
        reg.Add<PlayerStatsComponent>(e, stats);

        reg.Add<LevelComponent>(e, {});

        // ---- 状態機（HSM 3層）----
        // ※PlayerStateSystem が毎フレーム書く
        reg.Add<PlayerStateComponent>(e, {});

        // ---- 体力 ----
        // プレイヤーも敵と同じ HealthComponent を使う。
        //   HitEvent の消費側が Has<HealthComponent> で振り分けるので、
        //   プレイヤー専用の体力型を作る必要はない。
        HealthComponent hp;
        hp.max = cfg.maxHealth;
        hp.current = cfg.maxHealth;
        reg.Add<HealthComponent>(e, hp);

        // ---- 魔力 ----
        // ※現時点では HUD 表示のみ。
        //   実際の消費・回復は WandComponent::manaCurrent で行われていて、
        //   この Component とは繋がっていない（二重管理。要統合）。
        ManaComponent mana;
        mana.max = cfg.maxMana;
        mana.current = cfg.maxMana;
        mana.regen = cfg.manaRegen;
        reg.Add<ManaComponent>(e, mana);
        // ---- 杖（空で作る）----
        // spells / areas はグリッドの集約で埋まる。
        // castMode の既定は Auto（WandComponent の初期値のまま）。
        if (cfg.withWand)
            reg.Add<WandComponent>(e, {});

        // ---- バックパック ----
        if (cfg.withBackpack)
        {
            BackpackComponent bpc;

            // 初期枠を中央に敷く。枠が無いと魔法を1つも置けない。
            // 最初の三択で枠を引くまで何もできない、を避けるための 3x3。
            // Rect のアンカーは左上なので (2,2) で 2..4 行 2..4 列を覆う
            BackpackLogic::PlaceFrame(bpc, ItemID::Frame3x3, 2, 2, 0);

            reg.Add<BackpackComponent>(e, bpc);
        }

        // ---- 魔法書 ----
        // 初期装備。グリッドに置く前の「持っている」状態を作る
        SpellbookComponent book;
        book.Learn(ItemID::Fireball, 1);
        book.Learn(ItemID::Frame3x3, 1);   // 初期枠の分を1つ持たせる
        reg.Add<SpellbookComponent>(e, book);
        std::cout << "[PlayerFactory] player created (entity " << e << ")" << std::endl;
        return e;
    }

    // ============================================================
    // 衝突体と見た目の作り直し
    // 数値は PlayerStatsComponent から読む（Config は使わない）
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