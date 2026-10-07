// ============================================================
// StageConfig.cpp
// 色は全部線形 HDR（SceneLighting と同じ）。sRGB で見せたい色は 2.2 乗した値
// ============================================================
#include "World/StageConfig.h"

namespace
{
    StageDef MakeGrassland()
    {
        StageDef s;
        s.index = 1;
        s.name = L"草原";
        s.biome = TerrainGenerator::Biome::Grassland;
        // 照明・草は SceneLighting / GrassRenderer の既定値のまま（晴れた昼の野原）
        return s;
    }

    StageDef MakeDesert()
    {
        StageDef s;
        s.index = 2;
        s.name = L"砂漠";
        s.biome = TerrainGenerator::Biome::Desert;
        // 砂塵で霞んだ砂漠（2026-10-05：日差しが強すぎた → 太陽を弱め、空は砂色に濁らせ、靄を近くから濃く）
        s.light.sunPitch = 55.0f;
        s.light.sunYaw = 20.0f;
        s.light.lightColor = { 1.0f, 0.90f, 0.74f };
        s.light.lightIntensity = 0.60f;
        s.light.ambientSky = { 0.40f, 0.37f, 0.31f };
        s.light.ambientGround = { 0.24f, 0.18f, 0.11f };
        s.light.skyZenith = { 0.40f, 0.42f, 0.46f };
        s.light.skyHorizon = { 0.60f, 0.52f, 0.40f };
        s.light.skyBelow = { 0.40f, 0.33f, 0.24f };
        s.light.sunGlow = 0.25f;
        s.light.fogUseHorizon = true;
        s.light.fogStart = 10.0f;
        s.light.fogEnd = 85.0f;
        s.light.fogMax = 0.93f;
        // 疎らな枯れ草（床の砂色に掛かる）
        s.grass = true;
        s.grassSpacing = 0.7f;
        s.grassRoot = { 0.55f, 0.50f, 0.35f };
        s.grassTip = { 1.05f, 0.95f, 0.60f };
        s.grassHeightMin = 0.12f;
        s.grassHeightMax = 0.26f;
        s.difficultyBonus = 0.6f;
        s.splitter = { 120.0f, 0.05f, 0.12f, 480.0f };   // スプリッターは第 1 面の主力。ここは少し混ざるだけ
        return s;
    }

    StageDef MakeDungeon()
    {
        StageDef s;
        s.index = 3;
        s.name = L"遺跡";
        s.biome = TerrainGenerator::Biome::Dungeon;
        // 空は無い（黒に近い天井）。弱い青い月明かりを主光にして、松明の点光源が主役
        s.light.sunPitch = 70.0f;
        s.light.sunYaw = 30.0f;
        s.light.lightColor = { 0.60f, 0.66f, 0.90f };
        s.light.lightIntensity = 0.45f;
        s.light.ambientSky = { 0.11f, 0.11f, 0.14f };
        s.light.ambientGround = { 0.05f, 0.05f, 0.05f };
        s.light.skyZenith = { 0.004f, 0.004f, 0.008f };
        s.light.skyHorizon = { 0.010f, 0.010f, 0.016f };
        s.light.skyBelow = { 0.004f, 0.004f, 0.006f };
        s.light.sunGlow = 0.0f;
        s.light.fogUseHorizon = false;
        s.light.fogColor = { 0.012f, 0.012f, 0.015f };
        s.light.fogStart = 12.0f;
        s.light.fogEnd = 70.0f;
        s.light.fogMax = 0.92f;
        s.grass = false;
        s.difficultyBonus = 1.2f;
        s.splitter = { 120.0f, 0.05f, 0.12f, 480.0f };
        s.torchLights = 12;
        return s;
    }

    const StageDef& Table(int i)
    {
        static const StageDef kStages[StageConfig::kStageCount] = { MakeGrassland(), MakeDesert(), MakeDungeon() };
        return kStages[i];
    }
}

namespace StageConfig
{
    const StageDef& Get(int stage)
    {
        return Table(std::clamp(stage, 1, kStageCount) - 1);
    }
}
