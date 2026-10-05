// ============================================================
// PickupSystem.h
// 場に落ちている拾い物（触れたら効く。F は要らない）。今は磁石だけ（Megabonk の Magnet）。
//   ・磁石：触れると場の経験値オーブを全部吸い寄せる（SwarmSystem::MagnetAllOrbs）
//   ・開始時に initialCount 個（プレイヤーから startRingMin〜Max m）、その後 respawnInterval 秒毎に
//     プレイヤーから ringMin〜ringMax m へ 1 個足す（場に maxOnMap 個まで）
//   ・見た目は赤い U 字 + 銀の先の蹄鉄形（六面体を組んだ 1 つのモデル）。浮いて回る・点光源・画面外の目印
// 時間は戦闘の経過秒（止まっている間は湧かない）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include <SimpleMath.h>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

struct ID3D11Device;
class Registry;
class GridWorld;
class SwarmSystem;
class Model;

class PickupSystem
{
public:
    void Init(ID3D11Device* device);

    // 置き直す（開始時・地形の作り直し）。乱数は地形の seed から
    void Reset(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center, uint32_t seed);

    // 浮遊・回転、時間で足す、触れたら使う。拾った数を返す（拾った場所は outPicked に）
    int Update(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& player,
        float dt, float runTime, SwarmSystem& swarm, DirectX::SimpleMath::Vector3* outPicked = nullptr);

    // 目印の点光源（止まっている間も積む）
    void SubmitLights() const;

    // 画面外の目印用
    std::vector<DirectX::SimpleMath::Vector3> GetPositions() const;

    // Game Test パネルの段
    void DrawImGui(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& player);

    // center の周り rMin〜rMax m の歩けるマスに 1 個置く（パネルのボタン・自動テスト用）
    bool Place(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center,
        float rMin, float rMax);

    // ---- 調整値 ----
    int   initialCount = 2;
    int   maxOnMap = 3;
    float respawnInterval = 60.0f;   // 秒（戦闘の経過時間）
    float startRingMin = 15.0f;      // 開始時の置き場所（m）
    float startRingMax = 45.0f;
    float ringMin = 20.0f;           // 途中で足す置き場所（プレイヤーから m）
    float ringMax = 40.0f;
    float pickupRadius = 1.3f;       // プレイヤーの中心からの水平距離（m）
    float magnetSeconds = 0.3f;      // 吸い寄せ半径を場全体にしておく秒（その間に吸われた球は離れない）
    float bobHeight = 0.25f;
    float spinSpeed = 90.0f;         // 度/秒

private:
    struct Magnet { Entity e; DirectX::SimpleMath::Vector3 base; float phase; };

    std::shared_ptr<Model> m_Model;
    std::vector<Magnet> m_Magnets;
    std::mt19937 m_Rng;
    float m_NextSpawn = 0.0f;   // 次に足す経過秒
    float m_Time = 0.0f;
    int   m_Picked = 0;
};
