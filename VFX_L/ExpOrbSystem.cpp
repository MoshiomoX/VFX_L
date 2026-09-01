// ============================================================
// ExpOrbSystem.cpp
// ============================================================
#include "ExpOrbSystem.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ExpOrbComponent.h"
#include "ExpRewardComponent.h"
#include "ProjectileVisualComponent.h"
#include "PlayerTag.h"
#include "LevelComponent.h"
#include "View.h"
#include <vector>

using namespace DirectX::SimpleMath;

// ============================================================
// 生成
// 敵が死んだ位置に、少し上へ跳ねる初速を付けて置く。
// ============================================================
Entity ExpOrbSystem::Spawn(Registry& reg, const Vector3& pos, float amount)
{
    Entity e = reg.Create();

    TransformComponent tf;
    tf.position = pos;
    reg.Add<TransformComponent>(e, tf);

    ExpOrbComponent orb;
    orb.amount = amount;

    // 死んだ場所から少し散らす。真上だと重なって見えないため。
    const float a = (float)rand() / RAND_MAX * 6.2831853f;
    const float r = 1.5f + (float)rand() / RAND_MAX * 1.5f;
    orb.velocity = { std::cos(a) * r, 4.0f, std::sin(a) * r };
    reg.Add<ExpOrbComponent>(e, orb);

    // 当面はビルボードで描く。draw call は投射物と共有される。
    ProjectileVisualComponent vis;
    vis.size = 0.35f;
    vis.color = { 0.4f, 1.0f, 0.6f, 1.0f };   // 緑がかった光
    vis.stretch = 0.0f;
    reg.Add<ProjectileVisualComponent>(e, vis);

    return e;
}

// ============================================================
// 更新
// ============================================================
void ExpOrbSystem::Update(Registry& reg, float dt)
{
    m_OrbCount = 0;
    m_GainedThisFrame = 0.0f;

    // ---- プレイヤーの位置を先に取る ----
    Entity player = 0;
    Vector3 playerPos;
    bool hasPlayer = false;

    reg.CreateView<TransformComponent, PlayerTag>()
        .Each([&](Entity e, TransformComponent& tf, PlayerTag&)
            {
                player = e;
                playerPos = tf.position;
                hasPlayer = true;
            });

    if (!hasPlayer) return;

    // ---- 取得したオーブを溜める ----
    // View 走査中に Destroy すると配列が動いて走査が壊れるため、
    // 後でまとめて消す。
    std::vector<Entity> collected;
    float gained = 0.0f;

    const float attractSq = attractRadius * attractRadius;
    const float pickupSq = pickupRadius * pickupRadius;

    reg.CreateView<TransformComponent, ExpOrbComponent>()
        .Each([&](Entity e, TransformComponent& tf, ExpOrbComponent& orb)
            {
                ++m_OrbCount;

                if (orb.spawnDelay > 0.0f)
                {
                    // 湧いた直後は吸われず、跳ねて散らばる
                    orb.spawnDelay -= dt;

                    orb.velocity.y += gravity * dt;
                    tf.position += orb.velocity * dt;

                    // 地面より下へ行かないよう簡易に止める。
                    // 地形との正確な当たりは取らない（数が多いため）。
                    if (tf.position.y < orbSize)
                    {
                        tf.position.y = orbSize;
                        orb.velocity = { 0.0f, 0.0f, 0.0f };
                    }
                    return;
                }

                Vector3 diff = playerPos - tf.position;
                const float distSq = diff.LengthSquared();

                // 一度吸い寄せが始まったら範囲外でも追い続ける。
                // 境界で吸ったり止まったりを繰り返さないため。
                if (!orb.attracted && distSq <= attractSq)
                    orb.attracted = true;

                if (!orb.attracted) return;

                if (distSq <= pickupSq)
                {
                    gained += orb.amount;
                    collected.push_back(e);
                    return;
                }

                // 近づくほど速くする。遠くではゆっくり寄ってきて、
                // 手元では素早く吸い込まれる方が気持ちがいい。
                orb.speed += accel * dt;
                if (orb.speed > maxSpeed) orb.speed = maxSpeed;

                diff.Normalize();
                tf.position += diff * orb.speed * dt;
            });

    // ---- 走査が終わってから消す ----
    for (Entity e : collected)
        reg.Destroy(e);

    m_GainedThisFrame = gained;

    // ---- 経験値を加算する ----
    // レベルアップの判定は LevelUpSystem が行う。ここでは足すだけ。
    if (gained > 0.0f && reg.Has<LevelComponent>(player))
        reg.Get<LevelComponent>(player).experience += gained;
}

// ============================================================
// 敵の報酬に従って落とす
//
// 呼ぶのは Destroy する前。位置と報酬を読む必要があるため。
// ============================================================
void ExpOrbSystem::DropFrom(Registry& reg, Entity deadEnemy)
{
    if (!reg.IsValid(deadEnemy)) return;
    if (!reg.Has<ExpRewardComponent>(deadEnemy)) return;
    if (!reg.Has<TransformComponent>(deadEnemy)) return;

    const auto& reward = reg.Get<ExpRewardComponent>(deadEnemy);
    const Vector3 pos = reg.Get<TransformComponent>(deadEnemy).position;

    const int n = (reward.splitCount < 1) ? 1 : reward.splitCount;
    const float each = reward.amount / (float)n;

    for (int i = 0; i < n; ++i)
        Spawn(reg, pos, each);
}