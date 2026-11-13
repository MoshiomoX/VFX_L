// ============================================================
// Items/Frames.h
// 3x3 以外の設置枠（長方形の拡張枠と、乗った魔法に効く効果の枠）。
//
// 設計意図：
//   ・形は長方形だけ（1x1 / 1x2 / 1x3 / 1x4 / 2x2 / 3x3）。どう並べても隙間が残らない
//   ・稀有度は持たない。出やすさと値段は大きさと効果で決まる（FrameDefs::Finish）
//   ・効果のある枠は小さい = 置ける場所は少ないが、乗っている魔法が強くなる
//   ・効果はこの枠に一マスでも乗っている攻撃魔法に効く。同じ種類の枠は重ならない（BackpackLogic::GetEffectFrames）
//   ・吸血の枠は代価なし（置ける場所が少ないのが代わり）
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

    // 出やすさ（四択・商店の重み）と値段の倍率を大きさと効果から決める：大きいほど珍しく高い、効果付きはさらに。
    // effectWeight / effectPrice = 効果の強さの分（吸血は強いので大きめ）
    inline void Finish(FrameItemDef& def, float effectWeight = 0.55f, float effectPrice = 0.5f)
    {
        const float cells = (float)def.common.occupyCells.size();
        const bool effect = def.HasEffect();
        def.weight = 1.0f / (1.0f + 0.25f * cells) * (effect ? effectWeight : 1.0f);   // 1x1 0.8 / 2x2 0.5 / 3x3 0.31
        def.priceMul = 0.4f + 0.18f * cells + (effect ? effectPrice : 0.0f);
    }
}

// ---- ただの拡張枠（ItemID の名前は昔の形のまま。中身は長方形）----
inline FrameItemDef MakeFrame2x3()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::Frame2x3, "Frame 2x2", L"拡張枠 2x2",
        L"バックパックの魔法を置ける場所を 2x2 マス広げる。", ItemShape::Rect(2, 2),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    FrameDefs::Finish(def);
    return def;
}

inline FrameItemDef MakeFrameL()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameL, "Frame 1x2", L"拡張枠 1x2",
        L"バックパックの魔法を置ける場所を 1x2 マス広げる。", ItemShape::Rect(1, 2),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    FrameDefs::Finish(def);
    return def;
}

inline FrameItemDef MakeFrameCross()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameCross, "Frame 1x1", L"拡張枠 1x1",
        L"バックパックの魔法を置ける場所を 1 マス広げる。隙間を埋めるのに。", ItemShape::Rect(1, 1),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    FrameDefs::Finish(def);
    return def;
}

inline FrameItemDef MakeFrameLine4()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameLine4, "Frame Line 4", L"拡張枠 1x4",
        L"バックパックの魔法を置ける場所を一列に 4 マス広げる。", ItemShape::Rect(1, 4),
        { 0.55f, 0.75f, 0.95f, 1.0f }, Res::Icon::Frame3x3);
    FrameDefs::Finish(def);
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
    FrameDefs::Finish(def);
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
    FrameDefs::Finish(def);
    return def;
}

inline FrameItemDef MakeFrameHaste()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameHaste, "Frame Haste", L"速射の枠",
        L"この枠に乗っている魔法の発動間隔が短くなる。", ItemShape::Rect(1, 2),
        { 0.95f, 0.85f, 0.35f, 1.0f }, Res::Icon::FrameHaste);
    def.spellModifiers.push_back({ SpellParam::CastInterval, ModifyOp::Multiply, 0.85f });
    def.areaModifiers.push_back({ AreaParam::CastInterval, ModifyOp::Multiply, 0.85f });
    FrameDefs::Finish(def);
    return def;
}

inline FrameItemDef MakeFrameLifesteal()
{
    FrameItemDef def = FrameDefs::MakeFrame(ItemID::FrameLifesteal, "Frame Lifesteal", L"吸血の枠",
        L"この枠に乗っている魔法が敵に与えたダメージの一部で、HP が回復する。", ItemShape::Rect(2, 2),
        { 0.85f, 0.15f, 0.22f, 1.0f }, Res::Icon::FrameLifesteal);
    def.spellModifiers.push_back({ SpellParam::Lifesteal, ModifyOp::Add, 0.03f });
    def.areaModifiers.push_back({ AreaParam::Lifesteal, ModifyOp::Add, 0.03f });
    FrameDefs::Finish(def, 0.45f, 0.8f);   // 吸血は強いので少し珍しく高い
    return def;
}
