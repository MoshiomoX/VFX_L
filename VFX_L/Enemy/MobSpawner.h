// ============================================================
// MobSpawner.h
// 雑魚の湧き管理（CPU 側の窓口）。実体は GPU（SwarmSystem）にしか居ない。
//   ・SpawnDirector が「何体をどこへ」を決め、ここが GPU へ生成 / 遠い雑魚の転送を依頼する
//   ・数えるのは GPU の存活数（回読なので 1〜2 フレーム古い）。
//     空き枠が無い分は「遠い雑魚の転送」として GPU に投げる
//   ・雑魚の初期値（HP / 速さ）と Mob AI の定数（GPU、次の固定ステップから効く）の面板
// ============================================================
#pragma once
#include "Enemy/SpawnDirector.h"
#include <SimpleMath.h>

class GridWorld;
class SwarmSystem;

class MobSpawner
{
public:
    void Init(SwarmSystem& swarm);
    void Update(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float dt, SwarmSystem& swarm);

    SpawnDirector& Director() { return m_Director; }

    // Enemies 面板の湧き管理・雑魚の初期値・Mob AI の段
    void DrawImGui(SwarmSystem& swarm);

private:
    SpawnDirector m_Director;
    float m_MobHp = 30.0f;
    float m_MobSpeed = 3.5f;
};
