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

        // ---- ?? ----
        TransformComponent tf;
        tf.position = cfg.spawnPos;
        reg.Add<TransformComponent>(e, tf);

        // ============================================================
        // ???(??????)
        //
        // ?mask ?? Layer_PlayerShot ???????
        //   muzzleOffset ? {0, 0.5, 0} = ??????????
        //   ???????????????????????
        //   ????? mask(Enemy|Terrain)??????????
        //   ?????????????? CollisionSystem ???????
        //   ?????????????????????
        // ============================================================
        ColliderComponent col;
        col.shape = ColliderShape::Capsule;
        col.radius = cfg.radius;
        col.height = cfg.height;
        col.layer = Layer_Player;
        col.mask = Layer_Enemy | Layer_EnemyShot | Layer_Terrain;
        reg.Add<ColliderComponent>(e, col);

        // ---- ?? ----
        // Slide:??????????????????
        RigidbodyComponent rb;
        rb.useGravity = true;
        rb.response = ResponseMode::Slide;
        reg.Add<RigidbodyComponent>(e, rb);

        // ---- ??? ----
        ModelComponent mc;
        mc.model = PrimitiveBuilder::CreateCapsule(device,
            cfg.radius, cfg.height, cfg.color);
        reg.Add<ModelComponent>(e, mc);

        // ---- ?? ----
        reg.Add<PlayerTag>(e, {});

        // ---- ??? ----
        // ??? Scene ? System ?????????????
        PlayerStatsComponent stats;
        stats.moveSpeed = cfg.moveSpeed;
        stats.jumpPower = cfg.jumpPower;
        stats.radius = cfg.radius;
        stats.height = cfg.height;
        reg.Add<PlayerStatsComponent>(e, stats);

        reg.Add<LevelComponent>(e, {});
        // ---- ???(HSM?3?)----
        // ?PlayerStateSystem ??????????
        reg.Add<PlayerStateComponent>(e, {});

        // ---- ?? ----
        // ???????????????????
        //   HitEvent ????? Has<HealthComponent> ??????
        //   ??????????????????????
        HealthComponent hp;
        hp.max = cfg.maxHealth;
        hp.current = cfg.maxHealth;
        reg.Add<HealthComponent>(e, hp);

        // ---- 魔力 ----
        // 現時点では HUD 表示のみ。消費・回復は未実装
        ManaComponent mana;
        mana.max = cfg.maxMana;
        mana.current = cfg.maxMana;
        reg.Add<ManaComponent>(e, mana);

        // ---- ?(????)----
        // spells / areas ?????????????????
        // castMode ???? Auto(WandComponent ?????)?
        if (cfg.withWand)
            reg.Add<WandComponent>(e, {});

        // ---- ?????? ----
        if (cfg.withBackpack)
        {
            BackpackComponent bpc;

            // ????????????????????????
            // ???1???????????? 3x3 ???????
            // Rect ??????????? (2,2) ? 2..4 ? 2..4 ?????
            BackpackLogic::PlaceFrame(bpc, ItemID::Frame3x3, 2, 2, 0);

            reg.Add<BackpackComponent>(e, bpc);
        }
        // ---- ?????? ----
        // ?????????????????????????
        SpellbookComponent book;
        book.Learn(ItemID::Fireball, 1);
        book.Learn(ItemID::Frame3x3, 1);   // ???????1?
        reg.Add<SpellbookComponent>(e, book);
        std::cout << "[PlayerFactory] player created (entity " << e << ")" << std::endl;
        return e;
    }

    // ============================================================
    // ?????????????
    // ??? PlayerStatsComponent ????(Config ????)
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