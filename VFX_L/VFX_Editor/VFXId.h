// ============================================================
// VFXId.h
// 特効の ID と、ID → JSON パスの対応表。
//
// 道具（ItemDatabase）は VFXId だけを持ち、パスを知らない。
//   同じ特効を複数の道具が使い回せるし、
//   CPU 経路（ProjectileVFXSystem）と GPU 経路（SwarmVFXTable）が
//   同じ表を引くので、二重管理にならない。
// ============================================================
#pragma once
#include <cstdint>

enum class VFXId : uint32_t
{
    None = 0,
    Fireball,
    DeathBurn,      // 燃焼消滅（Mesh 発射 + 溶解の縁。MeshVFXSystem::StartBurn）
    Explosion,      // 爆発。弾の命中で GPU が出す範囲（hitArea）の粒子。GPU 側は登録済みの VFX しか出せない
    ArcBolt,        // アークボルトの弾（電撃）
    HomingBolt,     // ホーミングボルトの弾（紫の渦）
    Meteor,         // メテオの弾（大きな火球 + 岩）
    MeteorBlast,    // メテオの着弾（Explosion の大きい版 + 岩の破片）
    FireCircle,     // 火の輪（範囲・術者に追従）
    ArcBoltHit,     // アークボルトの命中（範囲 ArcSpark：威力 0 の見た目だけ。Sprite = electric-impact）
    HomingBoltHit,  // ホーミングボルトの命中（範囲 VoidPop：同上。Sprite = void-implosion）
    ExpOrbTrail,    // 吸い寄せられている経験値オーブの尾（SwarmOrbEmitCS。道具は使わない）
    GoldenArrow,    // 黄金の矢の弾（gonjian.FBX の矢が弾と一緒に飛ぶ + 金の軌跡）
    GoldenArrowHit, // 黄金の矢の命中（範囲 GoldenArrowHit：威力 0 の見た目だけ。金の火花）
    FireballHit,    // 火球の命中・消滅（範囲 FireballHit：威力 0 の見た目だけ。小さな炎の閃き。2026-09-30 に火球の爆発を廃止）
    StoneShot,      // 石弾の弾（Rock_2.fbx の岩が弾と一緒に飛ぶ + 土煙）
    StoneShotHit,   // 石弾の命中（範囲 StoneShotHit：威力 0 の見た目だけ。土煙 + 岩の欠片）
    Poison,         // 毒の弾（緑の毒液の塊 + 滴。Lob 型で山なりに飛ぶ）
    PoisonPool,     // 毒の池（範囲 PoisonPool：落ちた所の飛沫 + 泡 + 毒霧 + 緑の光。持続 4 秒）
    Beam,           // 魔導光線（溜めの光球 + Beam entry の光線 + 点光源。CPU の AreaVFXPlayer が再生し、終点は WeaponSystem が毎フレーム入れる）
    // ---- 追加はここに。VFXDatabase.cpp の表にも1行足す ----
    Count
};

namespace VFXDatabase
{
    // ID → JSON パス。無ければ nullptr
    const char* GetPath(VFXId id);

    // 登録済みの全 ID（None を除く）
    int Count();
    VFXId At(int index);
}
