// ============================================================
// LevelUpSystem.h
// 経験値がしきい値を超えたら習得候補を抽選する。
//
// 選ばせる処理はここに置かない。
// System は「候補を作る」「選ばれたものを適用する」だけを担当し、
// 提示と入力は UI 側が持つ。
//
// 連続レベルアップは1回ずつ処理する。
// まとめて上げると三択が何度も続けて出て、選ぶ作業だけが残る。
// 余った経験値は持ち越して、選び終わってから次へ進む。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "SpellID.h"

class Registry;
struct StatItemDef;
struct LevelComponent;

class LevelUpSystem
{
public:
    void Update(Registry& reg);

    // 選択を確定する。UI から呼ぶ。
    // 候補に含まれない ID を渡された場合は何もしない。
    static bool Choose(Registry& reg, Entity player, ItemID picked);

    // 誰かが選択待ちか（シーンの一時停止判定に使う）
    static bool IsAnyoneChoosing(Registry& reg);

    // 升級と同じ三択を出す。レベルは上がらず、経験値も減らない（報酬の箱など）。
    // 既に選択待ち・候補が無い時は false（出していない）
    bool OfferChoices(Registry& reg, Entity player);

    // ---- 調整値 ----
    int choiceCount = 4;        // 候補の数（2026-09-29 用户の依頼で 3 → 4）
    float statWeight = 0.35f;   // 能力値の札 1 枚の出やすさ（魔法・ルーン 1 枚 = 1）

    // ---- 統計（ImGui 表示用）----
    int GetTotalLevelUps() const { return m_TotalLevelUps; }
    int GetTotalOffers() const { return m_TotalOffers; }

private:
    void RollChoices(Registry& reg, Entity player);
    // 候補を pendingChoices に詰める（升級と報酬で共通）。詰めた数を返す
    int FillChoices(LevelComponent& lv);
    int m_TotalOffers = 0;   // 報酬の三択を出した回数

    // 能力値（生命・魔力の上限）をその場で適用する
    static void ApplyStat(Registry& reg, Entity player, const StatItemDef& stat);

    int m_TotalLevelUps = 0;
};