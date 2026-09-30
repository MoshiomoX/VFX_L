// ============================================================
// StageConfig.h
// 面（ステージ）の設定（2026-09-30）。戦闘場面は 1 つ（CollisionTestScene）で、
// どの面かで 地形の見た目（TerrainGenerator::Biome）・照明と空と霧・草・難度の下駄 が変わる。
//   第 1 面 草原（今までの野原）/ 第 2 面 砂漠 / 第 3 面 遺跡（地牢）
// 面の流れ: Boss を倒す → リザルト「次のステージへ」→ 背包・魔法書・等級を持ったまま次の面（RunResult.h の g_RunCarry）。
// 調試: 環境変数 VFXL_STAGE=2 / 3 でその面から始める
// ============================================================
#pragma once
#include "World/TerrainGenerator.h"
#include "Graphics/Light/SceneLighting.h"
#include <SimpleMath.h>

struct StageDef
{
    int index = 1;                          // 1 始まり
    const wchar_t* name = L"";              // HUD・リザルト用
    TerrainGenerator::Biome biome = TerrainGenerator::Biome::Grassland;
    SceneLighting::Preset light;

    // 草（GrassRenderer）。砂漠は疎らな枯れ草、遺跡は無し
    bool  grass = true;
    float grassSpacing = 0.25f;
    DirectX::SimpleMath::Vector3 grassRoot = { 0.55f, 0.60f, 0.50f };
    DirectX::SimpleMath::Vector3 grassTip = { 1.25f, 1.20f, 0.85f };
    float grassHeightMin = 0.28f, grassHeightMax = 0.55f;   // 葉の高さ m

    // 難度の下駄：MobSpawner の倍率 (1 + 0.12 × 分) に足す（第 2 面 +0.6 = 第 1 面の 5 分相当から始まる）
    float difficultyBonus = 0.0f;

    // 遺跡の松明（外周の壁に付く）に点光源を付ける数（玩家に近い順。全体の上限 64 を圧迫しないように）
    int torchLights = 12;
};

namespace StageConfig
{
    inline constexpr int kStageCount = 3;

    // 1..kStageCount。範囲外は端に丸める
    const StageDef& Get(int stage);
}
