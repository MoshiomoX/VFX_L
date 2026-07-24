// ============================================================
// TestSpawner.cpp
// ============================================================
#include "TestSpawner.h"
#include "Registry.h"
#include "TransformComponent.h"
#include "ColliderComponent.h"
#include "RigidbodyComponent.h"

namespace TestSpawner
{
    Entity SpawnStaticBox(Registry& reg, const Vector3& pos, const Vector3& halfExtents)
    {
        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = pos;
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::AABB;
        col.halfExtents = halfExtents;
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);

        return e;
    }

    Entity SpawnDynamicSphere(Registry& reg, const Vector3& pos, float radius)
    {
        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = pos;
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Sphere;
        col.radius = radius;
        col.layer = Layer_Player;   // 仮: 動的テスト物体
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.useGravity = true;
        rb.response = ResponseMode::Slide;
        reg.Add<RigidbodyComponent>(e, rb);

        return e;
    }

    Entity SpawnCapsule(Registry& reg, const Vector3& pos, float radius, float height)
    {
        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = pos;
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Capsule;
        col.radius = radius;
        col.height = height;
        col.layer = Layer_Player;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.useGravity = true;
        rb.response = ResponseMode::Slide;
        reg.Add<RigidbodyComponent>(e, rb);

        return e;
    }

    Entity SpawnStaticSphere(Registry& reg, const Vector3& pos, float radius)
    {
        Entity e = reg.Create();

        TransformComponent tf;
        tf.position = pos;
        reg.Add<TransformComponent>(e, tf);

        ColliderComponent col;
        col.shape = ColliderShape::Sphere;
        col.radius = radius;
        col.layer = Layer_Terrain;
        col.mask = Layer_All;
        reg.Add<ColliderComponent>(e, col);

        RigidbodyComponent rb;
        rb.isStatic = true;
        rb.useGravity = false;
        reg.Add<RigidbodyComponent>(e, rb);

        return e;
    }
}