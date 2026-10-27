// ============================================================
// Items/Frames.h
// 3x3 以外の設置枠（2026-10-27、ユーザーの試遊の感想「設置枠が 3x3 しか無くてつまらない」「3x3 だけだと強すぎる」
// 「設置枠が置いた魔法に効くように：範囲強化・単体強化など」→ 形を増やし、効果のある枠を足した）。
//
// 設計意図：
//   ・ただの拡張枠は形で差を付ける（2x3 / L / 十字 / 一列）。3x3 は札では珍しくした（Frame3x3.h の weight）
//   ・効果のある枠は小さい（3〜4 マス）= 置ける場所は少ないが、乗っている魔法が強くなる
//   ・効果はこの枠に一マスでも乗っている攻撃魔法に効く。同じ種類の枠は重ならない（BackpackLogic::GetEffectFrames）
//   ・吸血は 2026-10-25 のルーン（上下左右・威力 x0.85）だったが、10-27 ユーザー「吸血のルーンを削除して、
//     バックパックの効果にする」→ 吸血の枠（代価なし、置ける場所が少ないのが代わり）
// ============================================================
#pragma once
#include "ResourcePaths.h"

namespace FrameDefs
{
    inline FrameItemDef MakeFrame(ItemID id, const char* name, const wchar_t* displayName, const wchar_t* description,
        std::vector<CellOffset> cells, DirectX::SimpleMath::Vector4 color, const wchar_t* icon)
    {
        FrameItemDef def;
        def.common.id = id;
        def.common.name = name;
        def.common.displayName = displayName;
        def.common.description = description;
        def.common.category = ItemCategory::Frame;
        def.common.occupyCells = std::move(cells);
        def.common.influenceCells = {};
        def.common.color = color;
        def.common.iconPath = icon;
        return def;
    }

    // L 字 4 マス（縦 3 + 下の右 1）
    inline std::vector<CellOffset> L4() { return { {0, 0}, {1, 0}, {2, 0}, {2, 1} }; }
    // L 字 3 マス（角）
    inline std::vector<CellOffset> L3() { return { {0, 0}, {1, 0}, {1, 1} }; }
    // T 字 4 マス（上 3 + 真ん中の下 1）
    inline std::vector<CellOffset> T4() { return { {0, 0}, {0, 1}, {0, 2}, {1, 1} }; }
    // 十字 5 マス（アンカーは左上の角 = 上の腕の左隣。中心は (1,1)）
    inline std::vector<CellOffset> Plus5() { return { {0, 1}, {1, 0}, {1, 1}, {1, 2}, {2, 1} }; }
}

// ---- ただの拡張枠 ----
inline FrameItemDef MakeFrame2x3()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::Frame2x3, "Frame 2x3", L"拡張枠 2x3",
        L"バックパックの魔法を置ける場所を 2x3 マス広げる。", ItemShape::Rect(2, 3),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    def.weight = 0.8f;
    def.priceMul = 1.2f;
    return def;
}

inline FrameItemDef MakeFrameL()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameL, "Frame L", L"拡張枠 L 字",
        L"バックパックの魔法を置ける場所を L 字に 4 マス広げる。", FrameDefs::L4(),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    return def;
}

inline FrameItemDef MakeFrameCross()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameCross, "Frame Cross", L"拡張枠 十字",
        L"バックパックの魔法を置ける場所を十字に 5 マス広げる。", FrameDefs::Plus5(),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    def.priceMul = 1.1f;
    return def;
}

inline FrameItemDef MakeFrameLine4()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameLine4, "Frame Line 4", L"拡張枠 一列",
        L"バックパックの魔法を置ける場所を一列に 4 マス広げる。", ItemShape::Rect(1, 4),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    return def;
}

// ---- 効果のある枠（小さい代わりに、乗っている魔法を強くする）----
inline FrameItemDef MakeFrameArea()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameArea, "Frame Area", L"範囲の枠",
        L"この枠に乗っている魔法の当たり判定と範囲が広がる。", ItemShape::Rect(2, 2),
        { 0.35f, 0.85f, 0.75f, 1.0f }, Res::Icon::FrameArea);
    def.spellModifiers.push_back({ SpellParam::Radius, ModifyOp::Multiply, 1.2f });
    def.areaModifiers.push_back({ AreaParam::Radius, ModifyOp::Multiply, 1.2f });
    def.weight = 0.9f;
    def.priceMul = 1.5f;
    return def;
}

inline FrameItemDef MakeFrameFocus()
{
    // 単体への威力：弾が当たった時の威力（落ちた所・当たった所に出す範囲は別 = AreaDamage は触らない）と、
    // 一瞬で届く稲妻（アークボルト。chainOnly = 溜め 0 の光線だけ）。毒沼・突風・光線は単体の魔法ではないので変わらない
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameFocus, "Frame Focus", L"一撃の枠",
        L"この枠に乗っている魔法が、当たった敵 1 体に与える威力が上がる。", ItemShape::Rect(1, 3),
        { 0.95f, 0.55f, 0.30f, 1.0f }, Res::Icon::FrameFocus);
    def.spellModifiers.push_back({ SpellParam::Damage, ModifyOp::Multiply, 1.25f });
    def.areaModifiers.push_back({ AreaParam::DamagePerTick, ModifyOp::Multiply, 1.25f, true });
    def.weight = 0.9f;
    def.priceMul = 1.5f;
    return def;
}

inline FrameItemDef MakeFrameHaste()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameHaste, "Frame Haste", L"速射の枠",
        L"この枠に乗っている魔法の発動間隔が短くなる。", FrameDefs::L3(),
        { 0.95f, 0.85f, 0.35f, 1.0f }, Res::Icon::FrameHaste);
    def.spellModifiers.push_back({ SpellParam::CastInterval, ModifyOp::Multiply, 0.85f });
    def.areaModifiers.push_back({ AreaParam::CastInterval, ModifyOp::Multiply, 0.85f });
    def.weight = 0.9f;
    def.priceMul = 1.5f;
    return def;
}

inline FrameItemDef MakeFrameLifesteal()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameLifesteal, "Frame Lifesteal", L"吸血の枠",
        L"この枠に乗っている魔法が敵に与えたダメージの一部で、HP が回復する。", FrameDefs::T4(),
        { 0.85f, 0.15f, 0.22f, 1.0f }, Res::Icon::FrameLifesteal);
    def.spellModifiers.push_back({ SpellParam::Lifesteal, ModifyOp::Add, 0.03f });
    def.areaModifiers.push_back({ AreaParam::Lifesteal, ModifyOp::Add, 0.03f });
    def.weight = 0.7f;
    def.priceMul = 1.5f;
    return def;
}
