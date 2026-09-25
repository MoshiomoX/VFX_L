// ============================================================
// ItemDatabase.h
// アイテム定義の登録所。
// 実際の定義は Items/ 以下の各ファイルに書く。
// Initialize() が Make◯◯() を呼んで登録するだけの薄い層。
// ============================================================
#pragma once
#include "Item/ItemTypes.h"

namespace ItemDatabase
{
    void Initialize();   // 起動時に1回だけ呼ぶ。二回目以降は何もしない

    ItemCategory GetCategory(ItemID id);

    // 種類ごとの定義を取る。無ければ nullptr
    const ProjectileItemDef* GetProjectile(ItemID id);
    const FunctionItemDef* GetFunction(ItemID id);
    const AreaItemDef* GetArea(ItemID id);
    const FrameItemDef* GetFrame(ItemID id);

    // 種類を問わず共通部分だけ取る（表示用に便利）
    const ItemCommon* GetCommon(ItemID id);

    // 攻撃型（Projectile / Area）かどうか
    bool IsAttackType(ItemID id);

    // 設置枠かどうか（バックパック側の判定で使う）
    bool IsFrame(ItemID id);

    // 修飾符を SpellStats / AreaStats へ適用する
    void ApplyModifier(SpellStats& stats, const ParamModifier& mod);
    void ApplyModifier(AreaStats& stats, const AreaModifier& mod);

    // 登録済み全ID（UI の一覧表示用）。能力値（Stat）は含まない
    const std::vector<ItemID>& GetAllIDs();

    // 能力値の成長（生命・魔力の上限など）。レベルアップの候補にだけ出る
    const StatItemDef* GetStat(ItemID id);
    const std::vector<ItemID>& GetLevelUpOnlyIDs();

    // ---- 形（占位格・影響格）の編集（投射物編集器の Item Shapes 頁から使う）----
    // 形を差し替える。置いてある物との整合は呼ぶ側が BackpackLogic::Refit で取る
    void SetShape(ItemID id, const std::vector<CellOffset>& occupy,
        const std::vector<CellOffset>& influence);
    // Items/*.h に書いてある形（json で上書きする前）。無い ID なら false
    bool GetCodeShape(ItemID id, std::vector<CellOffset>& occupy,
        std::vector<CellOffset>& influence);
}