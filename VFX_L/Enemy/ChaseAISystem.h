// ============================================================
// ChaseAISystem.h
// 雑魚の移動決定。算法は steering の二規則:
//   seek（玩家への向心）+ separation（仲間との分離）
// 障害物は迂回しない。PhysicsSystem の Slide が壁沿いに
// 滑らせるので、矩形障害物ならそれで抜けられる。
//
// 書くもの: rb.velocity の x/z、tf.rotation.y
// 書かないもの: velocity.y（重力と着地は PhysicsSystem の担当）
// ============================================================
#pragma once

class Registry;

class ChaseAISystem
{
public:
    void Update(Registry& reg, float dt);

    // --- 調整（ImGui から触る）---
    float separationRadius = 1.2f;
    float separationPower = 4.0f;
};