// ============================================================
// VFXDatabase.cpp
// ============================================================
#include "VFX_Editor/VFXId.h"
#include "ResourcePaths.h"

namespace
{
    struct Entry { VFXId id; const char* path; };

    // ※VFXId の順と揃える必要は無い（線形探索）。None は登録しない
    const Entry kTable[] = {
        { VFXId::Fireball, Res::VFX::Fireball },
        // { VFXId::IceShard, "Assets/Data/VFXData/iceshard.json" },
    };
    constexpr int kCount = (int)(sizeof(kTable) / sizeof(kTable[0]));
}

namespace VFXDatabase
{
    const char* GetPath(VFXId id)
    {
        for (const auto& e : kTable)
            if (e.id == id) return e.path;
        return nullptr;
    }

    int Count() { return kCount; }
    VFXId At(int index)
    {
        return (index >= 0 && index < kCount) ? kTable[index].id : VFXId::None;
    }
}