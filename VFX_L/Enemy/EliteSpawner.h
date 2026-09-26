// ============================================================
// EliteSpawner.h
// CPU に残る敵（精英・計測用の的）。雑魚は GPU（SwarmSystem）にしか居ない。
//   ・的は無敵・動かない。累計ダメージを読むためのもので、HP は減るが 0 で止まる
//   ・見た目は骨付きの KayKit Skeleton_Warrior（Idle ループ）。読めなければカプセル
//   ・HP が尽きた CPU 実体（精英など。玩家は PlayerStateSystem が扱う）は燃焼消滅
//     （DissolveComponent + DeathBurn）。消え終わったら MeshVFXSystem が実体を破棄する
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <SimpleMath.h>
#include <memory>
#include <vector>

struct ID3D11Device;
class Model;
class MeshVFXSystem;
struct VFXContext;

class EliteSpawner
{
public:
    // 骨付きモデルが読めない時のカプセルを作る
    void Init(ID3D11Device* device);

    // 並べ直す（プレイヤーの前に 1 体）。player が null なら消すだけ
    void Respawn(Registry& reg, const DirectX::SimpleMath::Vector3* player);

    // HP が尽きた CPU 実体に燃焼消滅を付ける（MeshVFXSystem::Update の前に呼ぶ）
    void UpdateDeaths(Registry& reg, MeshVFXSystem& meshVfx, const VFXContext& ctx);

    const std::vector<Entity>& GetElites() const { return m_Elites; }

    // Enemies 面板の精英の段。「Respawn Elites」が押されたら true（呼ぶ側が Respawn する）
    bool DrawImGui(Registry& reg, const MeshVFXSystem& meshVfx);

private:
    void Spawn(Registry& reg, const DirectX::SimpleMath::Vector3& pos);
    bool AttachVisual(Registry& reg, Entity e);   // 骨付きモデルを付ける（駄目なら false）

    std::vector<Entity> m_Elites;
    std::shared_ptr<Model> m_DummyModel;
    float m_BurnDuration = 1.5f;   // 燃焼消滅の秒数（ImGui で調整）
};
