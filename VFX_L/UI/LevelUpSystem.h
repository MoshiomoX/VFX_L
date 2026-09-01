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

class LevelUpSystem
{
public:
    void Update(Registry& reg);

    // 選択を確定する。UI から呼ぶ。
    // 候補に含まれない ID を渡された場合は何もしない。
    static bool Choose(Registry& reg, Entity player, ItemID picked);

    // 誰かが選択待ちか（シーンの一時停止判定に使う）
    static bool IsAnyoneChoosing(Registry& reg);

    // ---- 調整値 ----
    int choiceCount = 3;

    // ---- 統計（ImGui 表示用）----
    int GetTotalLevelUps() const { return m_TotalLevelUps; }

private:
    void RollChoices(Registry& reg, Entity player);

    int m_TotalLevelUps = 0;
};