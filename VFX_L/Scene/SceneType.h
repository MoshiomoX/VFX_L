#pragma once
enum class SceneType
{
    NONE,
    TITLE,
    COLLISION_TEST,
    VFX_EDITOR,
    PROJECTILE_EDITOR,
    LEVEL_EDITOR,
    RESULT,
    SPELL_LAB,     // 魔法の実験場（戦闘シーンの平地版。2026-10-06）
    // 追加していく
};

// デバッグ表示用の名前（ImGui / ログ）
inline const char* SceneTypeName(SceneType type)
{
    switch (type)
    {
    case SceneType::NONE:              return "None";
    case SceneType::TITLE:             return "Title";
    case SceneType::COLLISION_TEST:    return "Game Test";
    case SceneType::VFX_EDITOR:        return "VFX Editor";
    case SceneType::PROJECTILE_EDITOR: return "Projectile Editor";
    case SceneType::LEVEL_EDITOR:      return "Level Editor";
    case SceneType::RESULT:            return "Result";
    case SceneType::SPELL_LAB:         return "Spell Lab";
    }
    return "Unknown";
}
