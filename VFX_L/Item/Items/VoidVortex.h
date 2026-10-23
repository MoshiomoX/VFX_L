// ============================================================
// Items/VoidVortex.h
// 渦：基本魔法（2026-10-22。ユーザーが旧ホーミングボルトの紫の小さな渦の見た目を使い戻したいと言い、
//   「渦そのものが吸い込む魔法」案を選んだ）。紫の渦をゆっくりまっすぐ飛ばし、当たった所（届かなければ飛び切った所）で
//   2 秒間渦が開く（範囲 VoidVortexPull）。渦は tick ごとに周りの敵を中心へ引き寄せ（AreaProfile::pull、
//   突風の吹き飛ばしと同じ GPU の通り道で向きだけ逆）、少し遅くする。渦そのものは傷つけない（威力 0。
//   同日ユーザー：「引き寄せている間はダメージを出さない」、上級魔法の前提にする予定）。
//   役目は「群れを一か所に集める」こと：メテオ・猛毒の沼・アイススパイクなどの範囲魔法がまとめて当たる。
//   渦は局所の風源にもなる（草が渦の方へなびき、落ち葉が吸い込まれる。SwarmSystem::vortexWind）
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeVoidVortex()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::VoidVortex;
    def.common.name = "VoidVortex";
    def.common.displayName = L"渦";
    def.common.description = L"紫の渦をゆっくり放つ。当たった所で渦が開き、周りの敵を中心へ引き寄せる。";
    def.common.iconPath = Res::Icon::VoidVortex;
    def.common.category = ItemCategory::Projectile;
    // 形は強さで決める（2026-10-04 の決まり）：威力は弾の 6 だけ（渦は 0）だが、
    // 群れを集める役目があるので 3 マス（ストーンショットと逆向きの L 字）
    def.common.occupyCells = { { 0, 1 }, { 1, 0 }, { 1, 1 } };
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右）
    def.common.color = { 0.55f, 0.30f, 0.95f, 1.0f };   // 深い紫

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::VoidVortex;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 1.5f;
    def.baseStats.manaCost = 12.0f;

    // ---- 何を撃つか ----
    def.profile = "VoidVortex";   // Assets/Data/ProjectileData/VoidVortex.json（直進・遅い、hitArea = VoidVortexPull）

    return def;
}
