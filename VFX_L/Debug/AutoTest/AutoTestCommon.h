// ============================================================
// AutoTestCommon.h
// TEMP-TEST: 自動テストのファイルが共通で使う include と小さな補助
// ============================================================
#pragma once
#include "Debug/AutoTest/BattleAutoTest.h"
#include "Audio/AudioSystem.h"
#include "Graphics/Renderer/TerrainSurface.h"

#include "Component/TransformComponent.h"
#include "Component/ClothChainComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/SkinnedAnimComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Component/InteractableComponent.h"
#include "Component/ModelComponent.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Model/Model.h"
#include <filesystem>
#include "Graphics/Model/SkinnedModel.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerFactory.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "ECS/View.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Item/ItemTypes.h"
#include "Item/ItemInfo.h"
#include "UI/LevelUpSystem.h"
#include "Swarm/AreaProfile.h"
#include "World/TerrainGenerator.h"
#include "VFX_Editor/VFXId.h"
#include "Debug/DebugManager.h"
#include "Debug/FrameProfiler.h"
#include "Core/Application.h"
#include "imgui.h"
#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <fstream>
#include <random>
#include <unordered_set>

// 移動の向き（カメラから見て）の名前。ログ用
inline const char* MoveDirName(MoveDirID d)
{
    switch (d)
    {
    case MoveDirID::Forward: return "Forward";
    case MoveDirID::Right:   return "Right";
    case MoveDirID::Back:    return "Back";
    case MoveDirID::Left:    return "Left";
    default:                 return "-";
    }
}

// 自動テストでバックパックを組み直す前に、置いてある魔法を全部外す（開始時の魔法も。手元へ戻るだけ）
inline void ClearBackpackItems(BackpackComponent& bp)
{
    while (!bp.items.empty())
        BackpackLogic::Remove(bp, (int)bp.items.size() - 1);
    bp.dirty = true;
}
