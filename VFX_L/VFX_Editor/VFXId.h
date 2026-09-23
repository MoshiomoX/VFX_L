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