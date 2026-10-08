// ============================================================
// Items/IceLance.h
// アイスランス：基本魔法（2026-10-08、ユーザー：「氷の魔法。水晶のような氷のエフェクトモデルがある」）。
//
// 設計意図（ユーザーが選んだ「直射で当たった 1 体を凍らせる」）：
//   ・弾は直進（profile "IceLance"）。当たった 1 体を freezeTime 秒凍らせる（動かない・殴らない・溜めた突撃兵は溜めが消える）。
//     エリート・ボスは半分、解けてから 3 秒は凍らない（凍らせ続けられないように。BomberCB::freezeBigMul / freezeImmunity）
//   ・威力は単体の基本魔法の中で低め（毎秒約 13、追尾弾 18）。代わりに「目の前の 1 体を止める」役
//   ・凍った敵の足元に氷の塊（特効モデル bingci_02.FBX、SwarmIceVS）、体は氷色
//   ・形は強さで決める（2026-10-04 の決まり）：追尾弾と同じくらい → 縦 2 マスの槍、影響マスは形の上下左右
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include "ResourcePaths.h"

inline ProjectileItemDef MakeIceLance()
{
    ProjectileItemDef def;

    // ---- 共通 ----
    def.common.id = ItemID::IceLance;
    def.common.name = "Ice Lance";
    def.common.displayName = L"アイスランス";
    def.common.description = L"氷の槍をまっすぐ放つ。当たった敵はしばらく凍りつき、動くことも攻撃することもできない。";
    def.common.iconPath = Res::Icon::IceLance;
    def.common.category = ItemCategory::Projectile;
    def.common.occupyCells = ItemShape::ColLine(2);
    def.common.influenceCells = ItemShape::Around4(def.common.occupyCells);   // 誘発の届く範囲（形の上下左右）
    def.common.color = { 0.55f, 0.85f, 1.00f, 1.0f };   // 氷の水色

    // ---- どう撃つか ----
    def.baseStats.id = ItemID::IceLance;
    def.baseStats.projectileCount = 1;
    def.baseStats.spreadAngle = 0.0f;
    def.baseStats.castCount = 1;
    def.baseStats.castDelay = 0.12f;
    def.baseStats.castInterval = 0.75f;
    def.baseStats.manaCost = 9.0f;

    // ---- 何を撃つか ----
    def.profile = "IceLance";   // Assets/Data/ProjectileData/IceLance.json（直進、freezeTime 1.2 秒）

    return def;
}
