#pragma once
enum class SceneType
{
    NONE,
    TEST,
    TITLE,
    GAME,
    GPU_PARTICLE_TEST,
    GY,
    COLLISION_TEST,
    VFX_EDITOR,
    PROJECTILE_EDITOR,
    RESULT,
    // 追加していく
};

// デバッグ表示用の名前（ImGui / ログ）
inline const char* SceneTypeName(SceneType type)
{
    switch (type)
    {
    case SceneType::NONE:              return "None";
    case SceneType::TEST:              return "Test";
    case SceneType::TITLE:             return "Title";
    case SceneType::GAME:              return "Game";
    case SceneType::GPU_PARTICLE_TEST: return "GPU Particle Test";
    case SceneType::GY:                return "GY";
    case SceneType::COLLISION_TEST:    return "Game Test";
    case SceneType::VFX_EDITOR:        return "VFX Editor";
    case SceneType::PROJECTILE_EDITOR: return "Projectile Editor";
    case SceneType::RESULT:            return "Result";
    }
    return "Unknown";
}
