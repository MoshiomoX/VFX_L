// ============================================================
// ItemInfo.h
// アイテムの説明（名前・種別・一言・能力値の行）を組み立てる。
// レベルアップのカードと、バックパック / 魔法書の tooltip が同じ中身を使う。
//
// 数値は手で書かない：
//   飛行物 … アイテムの定義（どう撃つか）+ 投射物プロファイル（弾そのもの）
//   範囲   … アイテムの定義 + 範囲プロファイル
//   置いてある物は、隣のルーンの修飾を掛けた後の値と元の値を並べる
// 基礎値の組み立てと修飾の掛け方は BackpackAggregateSystem と共用なので、
// 画面の数字と実際の戦闘の数字はずれない。
//
// 描画はしない（文字列と色を返すだけ）。描くのは UI 側
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include <SimpleMath.h>
#include <string>
#include <vector>

struct BackpackComponent;

namespace ItemInfo
{
    // 画面用の名前（displayName が空なら内部名）
    std::wstring DisplayName(ItemID id);
    const wchar_t* CategoryLabel(ItemCategory c);

    // 基礎値（アイテムの定義にプロファイルの値を写したもの）。修飾は掛けていない
    SpellStats BaseSpellStats(const ProjectileItemDef& def);
    AreaStats  BaseAreaStats(const AreaItemDef& def);

    // 能力アップ「魔法威力」（PlayerStatsComponent::spellPower）を掛ける。修飾（ルーン）の後に掛ける。
    // 飛行物は弾の威力と、命中・着弾で出す範囲の威力（areaDamageMul）の両方
    void ApplySpellPower(SpellStats& s, float power);
    void ApplySpellPower(AreaStats& a, float power);

    // 能力値の 1 行
    struct Line
    {
        std::wstring label;
        std::wstring value;
        std::wstring baseValue;   // 修飾で変わった時の元の値（空 = 変わっていない）
        int trend = 0;            // +1 良くなった / -1 悪くなった / 0 どちらでもない
    };

    struct Sheet
    {
        ItemID id = ItemID::Fireball;
        std::wstring title;
        std::wstring category;
        DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };
        std::wstring description;
        std::vector<std::wstring> traits;   // 飛び方・命中時の爆発など（文で出す物）
        std::vector<Line> stats;
        std::wstring footer;                // 「強化: 分裂のルーン」など
    };

    // アイテムそのもの（レベルアップのカード・魔法書の中の物）
    Sheet Describe(ItemID id);

    // バックパックに置いてある物（items の index）。隣のルーンの修飾と魔法威力（spellPower）を反映する
    Sheet DescribePlaced(const BackpackComponent& bp, int itemIndex, float spellPower = 1.0f);
}
