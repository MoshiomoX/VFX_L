// ============================================================
// MinimapBake.h
// ミニマップ用の地形の絵（2026-10-19、ユーザー「右上に小さい地図を」）。
// 格子の高さ場（0.5m 刻み）と同じ細かさで、1 texel = 1 色のアルベド（線形）を焼く：
//   区域（平原 / 山頂 / 洞窟）の地の色 → 傾きで陰影（北西から光）→ 急な所は崖の色 → 塞がったマス（木・岩・箱）は暗く。
// 地形を建てた時（生成でも地図の読み込みでも）に 1 回だけ作り、UI/HUDMinimap が毎フレーム窓を切り出す。
// 色は TerrainBuild の Palette（地形の頂点色と同じ組）から取るので、面ごとの色味が地図にも出る
// ============================================================
#pragma once
#include <cstdint>
#include <vector>

class GridWorld;
namespace MapData { struct Map; }

namespace MinimapBake
{
    struct Image
    {
        int   w = 0, h = 0;                 // texel 数（= GridWorld::HeightW / HeightD）
        float originX = 0.0f, originZ = 0.0f;   // texel (0, 0) の南西の隅（世界座標）
        float texel = 0.5f;                 // 1 texel の一辺（m）
        std::vector<uint8_t> rgba;          // 線形の色 x 255。a = 255（フィールドの中は全部不透明）

        bool Valid() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * h * 4; }
    };

    // biome = TerrainGenerator::Biome の値（ヘッダを軽く保つため int）。
    // grid は後処理（通れない袋小路・置物の下の封鎖）が終わった物を渡す
    Image Build(const GridWorld& grid, const MapData::Map& map, int biome, uint32_t seed);
}
