// ============================================================
// ItemDatabase.h
// 魔法ブロック定義の収集・参照。
// 具体的な数値は持たない。各魔法は Items/ 以下の個別ファイルにある。
// Initialize() で Make〜() を呼んで登録するだけ。
// ============================================================
#pragma once
#include "ItemTypes.h"

namespace ItemDatabase
{
    void Initialize();   // 起動時に1回。各魔法ファイルから定義を集める

    ItemCategory GetCategory(ItemID id);

    // 大類型が違うものを渡した場合は nullptr
    const ProjectileItemDef* GetProjectile(ItemID id);
    const FunctionItemDef* GetFunction(ItemID id);
    const AreaItemDef* GetArea(ItemID id);
    const FrameItemDef* GetFrame(ItemID id);

    // 大類型を問わず共通部分だけ取る（形状・色・名前）
    const ItemCommon* GetCommon(ItemID id);

    // 攻撃型（Projectile / Area）かどうか
    bool IsAttackType(ItemID id);

    // 設置枠かどうか（パレットの振り分けに使う）
    bool IsFrame(ItemID id);

    // 機能型の修飾を SpellStats に適用する
    void ApplyModifier(SpellStats& stats, const ParamModifier& mod);
    void ApplyModifier(AreaStats& stats, const AreaModifier& mod);

    // 登録済み一覧（UI のパレット表示用）
    const std::vector<ItemID>& GetAllIDs();
}