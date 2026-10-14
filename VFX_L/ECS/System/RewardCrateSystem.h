// ============================================================
// RewardCrateSystem.h
// 報酬の箱：近づいて F → レベルアップと同じ三択（レベルは上がらない）。
//   ・開始時にプレイヤーの周りの歩けるマスへ固定数を置く。使ったら消える（補充しない）
//   ・開けるには金貨が要る（2026-10-04、ユーザー指定：12 個、20 金貨から開ける毎に ×1.3）。
//     値段はこの面で開けた数で決まる（面が変わると最初から）
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
    // モデルを読む（シーンの Init から 1 回）
    void Init();

    // 並べ直す（開始時・地形の作り直し・パネルのボタン）。
    // center の周り（minDist〜maxDist）の歩けるマスへ count 個。乱数は地形の seed から。
    // summitCells / mineCells（TerrainGenerator::Layout のマス）があれば、そこにも summitCount / mineCount 個
    // （2026-10-02、フィールドの三層：登る・潜るご褒美）
    void Spawn(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center,
        uint32_t seed, InteractionSystem& interaction,
        const std::vector<int>* summitCells = nullptr, const std::vector<int>* mineCells = nullptr,
        const std::vector<DirectX::SimpleMath::Vector4>* fixed = nullptr);
    // fixed: 地図に置いてある箱（MapData::Placement。xyz = 底の中心、w = 向きの度）。空でなければ乱数で並べず、その通りに置く

    // InteractionSystem が返した「使われた物」を処理する。
    // 報酬の箱で、金貨が足りて三択が出せたら代金を払って箱を消して true（openedPos = 箱の置き場所）。
    // 金貨が足りない時は箱を残して false（ConsumeDenied が true になる）。
    // 同じフレームでレベルアップが三択を出していたら出せない → 箱は残す（払わない）
    bool TryOpen(Registry& reg, Entity used, Entity player, LevelUpSystem& levelUp,
        InteractionSystem& interaction, DirectX::SimpleMath::Vector3& openedPos);

    const std::vector<Entity>& GetCrates() const { return m_Crates; }

    // 次に開ける箱の値段（金貨）
    int Price() const;
    // この面で開けた数
    int Opened() const { return m_Opened; }
    // 直前の TryOpen が「金貨が足りない」で断ったか（取ったら false に戻る。音・案内用）
    bool ConsumeDenied() { const bool d = m_Denied; m_Denied = false; return d; }

    // ---- 値段 ----
    int   basePrice = 20;      // 1 個目
    float priceGrowth = 1.3f;  // 開ける毎に掛ける（20 → 26 → 34 → 44 → 57 …）

    // パネル。「Respawn Crates」が押されたら true（並べ直しは呼ぶ側が Spawn する）
    bool DrawImGui(InteractionSystem& interaction, const LevelUpSystem& levelUp);

private:
    std::vector<Entity> m_Crates;
    std::shared_ptr<Model> m_Model;
    // 2026-10-04 金貨で買う物になったので各 2 → 4（全部で 12 個）
    int   m_Count = 4;                // スポーン地点の周り
    int   m_SummitCount = 4;
    int   m_MineCount = 4;
    int   m_Opened = 0;               // この面で開けた数（値段が上がる）
    bool  m_Denied = false;           // 直前の TryOpen が金貨不足で断った
    float m_MinDist = 6.0f;           // スポーン地点からの距離（m）
    float m_MaxDist = 22.0f;
    float m_Spacing = 5.0f;           // 箱同士の最小間隔（m）
    float m_Size = 0.9f;              // 一辺（m）。モデルの包囲箱から倍率を決める
};
