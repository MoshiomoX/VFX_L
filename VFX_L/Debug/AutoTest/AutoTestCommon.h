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

// 9×9 全面に 3×3 枠を敷く（多マスの魔法や上級魔法 + 前提を置く自動テスト用）
inline void FillAllFrames(BackpackComponent& bp)
{
    bp.frames.clear();
    BackpackLogic::RebuildFrameOccupancy(bp);
    for (int r = 0; r < BackpackComponent::GRID; ++r)
        for (int c = 0; c < BackpackComponent::GRID; ++c)
            BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
}

// 猛毒の沼一式（2026-10-17：上級魔法 + 前提の泥沼・胞子）を決まった並びで置く。枠は 9×9 に敷いてあること。
//   猛毒の沼 (4,4) 凸字 → (3,4)(4,3)(4,4)(4,5)
//   泥沼     (2,3) 横 2 → (2,3)(2,4)：影響マス（形の上下左右）の下 = (3,4) = 沼の首
//   胞子     (5,3) 縦 2 → (5,3)(6,3)：影響マスの上 = (4,3) = 沼の胴
// 戻り値 = 猛毒の沼の index（置けなければ -1）
inline int PlacePoisonSet(BackpackComponent& bp)
{
    const int poison = BackpackLogic::Place(bp, ItemID::Poison, 4, 4, 0);
    BackpackLogic::Place(bp, ItemID::Swamp, 2, 3, 0);
    BackpackLogic::Place(bp, ItemID::Spore, 5, 3, 0);
    bp.dirty = true;
    return poison;
}
