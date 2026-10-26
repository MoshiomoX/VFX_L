// ============================================================
// TerrainBiomeProfile.cpp
// マップ毎の地形の生成規則（2026-10-26 ユーザー「三つのマップを完全に分ける」→「地形は各マップ自分の生成規則で」）。
// 以前は三つのマップとも同じ段々の台地（PokéBonk 風）で、色・置物・外周だけが違った。
// ここで TerrainGenerator::Config の数値をマップ毎に上書きする（シーンが biome を決めた直後に呼ぶ：
// CollisionTestSceneInit / EnterTrainingField / BuildSelectWorlds）。Terrain パネルの Regenerate はこの値から動かせる。
//
// 第 1 段階（10-26）= 今ある部品の数値だけで分ける：
//   草原 = 既定のまま（段々の台地 30%・2.5m・草の坂・水たまり）
//   砂漠 = 台地を減らして高く（大きなメサ）、砂丘の起伏を大きく長く、オアシスの水たまりは少なく、岩を多く
//   遺跡 = 角張った小さい台地（セルのノイズ）、起伏はほぼ無し、柱・像（木の枠）と崩れた壁（岩の枠）を多く
// 第 2 段階（未着手）：砂漠の岩柱・峡谷の壁、遺跡の壁で仕切った部屋・廊下
// ============================================================
#include "World/TerrainGenerator.h"

namespace TerrainGenerator
{
    void ApplyBiomeProfile(Config& cfg)
    {
        switch (cfg.biome)
        {
        case Biome::Desert:
            // メサ：少なく・大きく・高く（3.5m = ジャンプでは届かない、坂だけ）
            cfg.steps = true;
            cfg.stepHeight = 3.5f;
            cfg.stepCoverage = 0.14f;
            cfg.stepScale = 48.0f;
            cfg.stepNoise = 1;             // 域の歪み fBm：蛇行する輪郭（風で削れた岩）
            cfg.stepMaxLevel = 2;
            cfg.stepMinCells = 14;
            cfg.stepRampWidth = 4;
            cfg.stepRampSlopeDeg = 16.0f;
            cfg.stepRimLevels = 2;
            cfg.stepRimBand = 3;
            cfg.pondCoverage = 0.02f;      // オアシス（少ない）
            cfg.pondDepth = 0.6f;
            // 砂丘：大きく長い起伏（TerrainGenerator 側で砂漠 ×1.25 が掛かる）
            cfg.hillHeight = 3.0f;
            cfg.hillScale = 72.0f;
            cfg.hillDetailHeight = 0.6f;
            cfg.hillDetailScale = 26.0f;
            cfg.bumpCount = 14.0f;
            cfg.bumpRadiusMin = 5.0f;
            cfg.bumpRadiusMax = 9.0f;
            cfg.bumpHeightMin = 0.8f;
            cfg.bumpHeightMax = 1.8f;
            cfg.reliefMaxSlopeDeg = 32.0f;
            // 置物：木（椰子・枯れ木）は少なく、岩は多く
            cfg.treeCount = 18;
            cfg.rockCount = 26;
            cfg.bushCount = 50;
            // 岩柱と峡谷の壁（第 2 段。10-26）
            cfg.pillarCount = 12;
            cfg.canyonCount = 3;
            break;
        case Biome::Dungeon:
            // 遺跡：角張った小さめの段（セルのノイズ = 島状の台地）、石畳なので起伏はほぼ無し
            cfg.steps = true;
            cfg.stepHeight = 2.5f;
            cfg.stepCoverage = 0.22f;
            cfg.stepScale = 22.0f;
            cfg.stepNoise = 2;             // セル（Worley）：輪郭が一番きれい
            cfg.stepMaxLevel = 2;
            cfg.stepMinCells = 6;
            cfg.stepRampWidth = 3;
            cfg.stepRampSlopeDeg = 20.0f;
            cfg.pondCoverage = 0.015f;     // 溜まった雨水
            cfg.pondDepth = 0.35f;
            cfg.hillHeight = 1.0f;         // （遺跡 ×0.5 が掛かる）
            cfg.hillScale = 40.0f;
            cfg.hillDetailHeight = 0.3f;
            cfg.bumpCount = 10.0f;
            cfg.bumpHeightMin = 0.4f;
            cfg.bumpHeightMax = 0.8f;
            // 置物：柱・像（木の枠）と崩れた壁・本棚（岩の枠）で通路を作る
            cfg.treeCount = 44;
            cfg.rockCount = 32;
            cfg.bushCount = 110;
            // 壁で仕切った部屋と廊下（第 2 段。10-26）
            cfg.ruinRoomCount = 9;
            cfg.ruinRoomMinCells = 5;
            cfg.ruinRoomMaxCells = 9;
            cfg.ruinInnerWallHeight = 3.5f;
            break;
        default:
            break;   // 草原 = Config の既定
        }
    }
}
