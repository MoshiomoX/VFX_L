// ============================================================
// Tumbleweeds.h
// 転がり草（2026-10-22、ユーザー「風に影響される物を足すともっと面白い」→ 四択から「転がり草 / タンポポの綿毛」と
// 「マントが局所の風を受ける」を選んだ。こちらは転がり草）。
//   砂漠（と遺跡の枯れ草の玉）に、細い枝を絡めた玉が count 個転がっている。CPU で 1 個ずつ動かし、
//   見た目は ECS の実体（ModelComponent）= 影・トゥーン・アウトラインは他のモデルと同じ。
//   動き：世界の風（WindState、草と同じ波）+ 局所の風源（竜巻・渦・砂旋・突風。SwarmSystem::GetLocalWind）へ空気抵抗で寄る。
//     地面では転がる（摩擦・坂を下る・小さく跳ねる）、上昇気流（風源の updraft × liftMul）で浮き上がって渦の周りを回り、
//     芯を外れると放り出される。崖・木・岩・水たまり（通れないマス）には跳ね返る。プレイヤーに当たると蹴られる。
//     プレイヤーから despawn m 離れたら、風上の respawnMin〜Max m に置き直す（ずっと周りに数個居る）
//   時間は戦闘の dt（一時停止中は呼ばない = 止まる）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "World/TerrainGenerator.h"
#include <SimpleMath.h>
#include <cstdint>
#include <memory>
#include <random>
#include <vector>

struct ID3D11Device;
class Registry;
class GridWorld;
class Model;
struct WindState;
struct LocalWind;

class Tumbleweeds
{
public:
    struct Settings
    {
        bool  enabled = true;
        int   countDesert = 14;      // 砂漠
        int   countRuins = 7;        // 遺跡（灰色の枯れ草の玉）
        int   countGrass = 0;        // 草原（タンポポの方。DandelionField）
        float radiusMin = 0.35f;     // 玉の半径 m
        float radiusMax = 0.6f;
        float spawnMin = 8.0f;       // 最初に置く輪（プレイヤーから m）
        float spawnMax = 40.0f;
        float respawnMin = 30.0f;    // 置き直す時：風上のこの輪
        float respawnMax = 44.0f;
        float despawn = 56.0f;       // これより離れたら置き直す
        float gravity = 14.0f;
        float airDrag = 1.6f;        // 空中・縦：空気の速さへ寄る速さ（1/s）。終端の落下 = gravity / airDrag
        float groundDrag = 1.1f;     // 接地中の横：空気の速さへ寄る速さ（1/s）
        float rollFriction = 0.45f;  // 転がる時の減速 m/s²
        float slopePull = 0.6f;      // 坂を下る強さ（重力 × 勾配 × これ）
        float bounce = 0.35f;        // 着地の跳ね返り
        float hopRate = 0.7f;        // 転がっている時（1 m/s 以上）に小さく跳ねる回数 / 秒
        float hopSpeed = 1.6f;       // 跳ねる上向きの速さ m/s（速く転がるほど大きく）
        float liftMul = 2.6f;        // 局所の風源の吹き上げに掛ける倍率（軽いので風を受ける）
        float suck = 3.0f;           // 風源の芯へ吸い寄せる加速度 = LocalWind::Inward × これ（m/s² / (m/s)。竜巻・渦に巻き込まれる）
        float localFadeLow = 3.0f;   // 地面からこの高さまでは局所の風源が丸ごと効き、
        float localFadeHigh = 6.5f;  // ここで 0（竜巻の上へ飛び出し続けない）
        float kick = 1.2f;           // プレイヤーに当たった時の蹴り（相対速度 × これ）
        float kickUp = 2.2f;         // 同じく上へ m/s
        DirectX::SimpleMath::Vector3 colorDesert = { 0.50f, 0.36f, 0.17f };   // 線形（明るい砂地でも枝が見える濃さ）
        DirectX::SimpleMath::Vector3 colorRuins = { 0.30f, 0.27f, 0.22f };
    };

    void Init(ID3D11Device* device);
    // 地形を作り直した時（面の切り替えも）。前の玉を消して、この面の数だけ center の周りに置く
    void Reset(Registry& reg, const GridWorld& grid, TerrainGenerator::Biome biome, uint32_t seed,
        const DirectX::SimpleMath::Vector3& center);
    void Clear(Registry& reg);

    // player = プレイヤーの体の中心（null = 居ない）、playerVel = その速度。local = 局所の風源（null = 無し）
    void Update(Registry& reg, const GridWorld& grid, float dt, const WindState& wind, float windTime,
        const LocalWind* local, const DirectX::SimpleMath::Vector3* player, const DirectX::SimpleMath::Vector3& playerVel);

    void DrawImGui(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center);
    Settings& GetSettings() { return m_Settings; }
    int Count() const { return (int)m_Weeds.size(); }
    // 自動テスト用：i 個目の中心・速さ・浮いているか
    bool GetDebug(int i, DirectX::SimpleMath::Vector3& pos, DirectX::SimpleMath::Vector3& vel, bool& airborne) const;

private:
    struct Weed
    {
        Entity e = 0;
        DirectX::SimpleMath::Vector3 pos;    // 玉の中心
        DirectX::SimpleMath::Vector3 vel;
        DirectX::SimpleMath::Quaternion rot;
        DirectX::SimpleMath::Vector3 spinAxis = { 1, 0, 0 };
        float spin = 0.0f;                   // rad/s（空中では惰性で回り続ける）
        float radius = 0.5f;
        bool  airborne = false;
    };

    bool Place(const GridWorld& grid, Weed& w, const DirectX::SimpleMath::Vector3& center,
        float rMin, float rMax, const DirectX::SimpleMath::Vector2* upwind);
    void Step(Weed& w, const GridWorld& grid, float dt, const WindState& wind, float windTime,
        const LocalWind* local, const DirectX::SimpleMath::Vector3* player, const DirectX::SimpleMath::Vector3& playerVel);
    void BuildModels(ID3D11Device* device, const DirectX::SimpleMath::Vector3& color);

    ID3D11Device* m_Device = nullptr;
    std::vector<std::shared_ptr<Model>> m_Models;   // 枝の絡み方の違う数種類
    DirectX::SimpleMath::Vector3 m_ModelColor = { -1, -1, -1 };
    std::vector<Weed> m_Weeds;
    TerrainGenerator::Biome m_Biome = TerrainGenerator::Biome::Grassland;
    std::mt19937 m_Rng{ 1u };
    Settings m_Settings;
};
