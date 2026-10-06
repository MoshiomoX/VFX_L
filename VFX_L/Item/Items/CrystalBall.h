// ============================================================
// Items/CrystalBall.h
// 水晶玉：召喚物（2026-10-06、ユーザー：「魔法を貯蔵する単位」）。
//
// 設計意図：
//   ・影響マスは左右 2 マスだけ。そこに置いた魔法（基本・上級を問わず、最大 2 つ）を貯蔵する
//   ・貯蔵された魔法は杖から撃てなくなり、光球（最大 3 個、2 秒毎に 1 個、各 8 秒。プレイヤーの横に出て
//     その場でとてもゆっくり昇る。付いて来ない）が自分の位置から、その魔法の発動間隔で撃つ。消費 MP は 1.5 倍
//   ・上級魔法も貯蔵できるが、発動にはやはり隣接する基本魔法が要る（誘発された時に光球の位置から撃つ。10-06 ユーザー）
//   ・貯蔵された魔法は自分の隣のルーンの修飾をそのまま受ける。水晶玉そのものはルーンの影響を受けない
//     （召喚物専用の道具が後で付く予定。ユーザー 10-06）
//   ・光球の色 = 貯蔵した魔法の色の混色（VFXData/SpellOrb.json を染める）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline SummonItemDef MakeCrystalBall()
{
    SummonItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::CrystalBall;
    def.common.name = "CrystalBall";
    def.common.displayName = L"水晶玉";
    def.common.description = L"左右に隣接する魔法（2 つまで）を貯蔵する召喚物。貯蔵した魔法は杖からは撃てなくなり、周りを回る光球が代わりに撃つ。";
    def.common.category = ItemCategory::Summon;
    def.common.occupyCells = ItemShape::Single();
    def.common.influenceCells = ItemShape::Sides();   // 左右 2 マス = 貯蔵できるのは 2 つまで
    def.common.color = { 0.75f, 0.60f, 1.00f, 1.0f };   // 薄紫
    def.common.iconPath = Res::Icon::CrystalBall;

    // ---- 光球 ----
    def.maxOrbs = 3;
    def.orbInterval = 2.0f;
    def.orbLife = 8.0f;
    def.manaMul = 1.5f;
    def.orbitRadius = 1.1f;
    def.orbitHeight = 1.4f;
    def.riseSpeed = 0.1f;   // その場でとてもゆっくり昇る（付いて来ない。10-06 ユーザー）
    def.vfxFile = "SpellOrb.json";

    return def;
}
