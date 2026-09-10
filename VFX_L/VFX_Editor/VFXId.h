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