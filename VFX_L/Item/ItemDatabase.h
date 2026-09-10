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

    // 登録済み全ID（UI の一覧表示用）
    const std::vector<ItemID>& GetAllIDs();
}