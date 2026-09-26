// ============================================================
// RewardCrateSystem.h
// 報酬の箱：近づいて F → 升級と同じ三択（レベルは上がらない）。
//   ・開局に玩家の周りの歩けるマスへ固定数を置く。使ったら消える（補充しない）
//   ・近くの物の検出・案内・浮遊は InteractionSystem（汎用）。ここは箱の配置と
//     「使われた箱が報酬の箱なら三択を出して消す」だけ
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include <SimpleMath.h>
#include <memory>
#include <vector>
#include <cstdint>

class GridWorld;
class InteractionSystem;
class LevelUpSystem;
class Model;

class RewardCrateSystem
{
public:
    // モデルを読む（場面の Init から 1 回）
    void Init();

    // 並べ直す（開局・地形の作り直し・面板のボタン）。
    // center の周り（minDist〜maxDist）の歩けるマスへ count 個。乱数は地形の seed から
    void Spawn(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center,
        uint32_t seed, InteractionSystem& interaction);

    // InteractionSystem が返した「使われた物」を処理する。
    // 報酬の箱で、三択が出せたら箱を消して true（openedPos = 箱の置き場所）。
    // 同じフレームで升級が三択を出していたら出せない → 箱は残す
    bool TryOpen(Registry& reg, Entity used, Entity player, LevelUpSystem& levelUp,
        InteractionSystem& interaction, DirectX::SimpleMath::Vector3& openedPos);

    const std::vector<Entity>& GetCrates() const { return m_Crates; }

    // 面板。「Respawn Crates」が押されたら true（並べ直しは呼ぶ側が Spawn する）
    bool DrawImGui(InteractionSystem& interaction, const LevelUpSystem& levelUp);

private:
    std::vector<Entity> m_Crates;
    std::shared_ptr<Model> m_Model;
    int   m_Count = 4;
    float m_MinDist = 6.0f;           // 出生点からの距離（m）
    float m_MaxDist = 22.0f;
    float m_Spacing = 5.0f;           // 箱同士の最小間隔（m）
    float m_Size = 0.9f;              // 一辺（m）。モデルの包囲箱から倍率を決める
};
