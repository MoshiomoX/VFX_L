// ============================================================
// ItemInfoSheet.h
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する。
// 説明カードの種類ごとの中身を Sheet に足す（ItemInfo.cpp の Describe / DescribePlaced だけが使う内部の窓口）。
// v = 修飾後、b = 元の値（修飾が無ければ同じ物を渡す）
// ============================================================
#pragma once
#include "Item/ItemInfo.h"

struct BackpackComponent;

namespace ItemInfoSheet
{
    // 足元に落ちる物の文はこれで始める。誘発で撃つ上級魔法は「敵の足元」ではなく前提の弾が消えた場所なので、
    // ItemInfo.cpp の FixTriggeredFlight が「連鎖した場所…」に差し替える
    inline constexpr const wchar_t* kAtFeet = L"敵の足元";

    void FillProjectile(ItemInfo::Sheet& s, const SpellStats& v, const SpellStats& b);
    void FillArea(ItemInfo::Sheet& s, const AreaStats& v, const AreaStats& b);
    void FillFunction(ItemInfo::Sheet& s, const FunctionItemDef& def);
    void FillFrame(ItemInfo::Sheet& s, const FrameItemDef& def);
    void FillStat(ItemInfo::Sheet& s, const StatItemDef& def);
    void FillSummon(ItemInfo::Sheet& s, const SummonItemDef& def);

    // 貯蔵されている攻撃魔法の説明に足す行（集約と同じ判定）
    void AddStoredTrait(ItemInfo::Sheet& s, const BackpackComponent& bp, int itemIndex);
}
