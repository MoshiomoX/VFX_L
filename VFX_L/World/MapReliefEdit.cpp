// ============================================================
// MapReliefEdit.cpp
// MapTerrainEdit のうち、起伏の筆（地図エディタの 4 歩目の 3 段目）。
// 筆が書き換えるのは「台座で均す前の素の起伏」（Map::rawPlain / rawSummit）。
// 台地・坂の足元の台座と傾きの上限は Rederive がその上から掛け直すので、
// 部品の足元の平らな所は壊れず、雑魚が歩けない急な斜面もできない
// ============================================================
#include "World/MapTerrainEdit.h"
#include "World/TerrainBuild.h"

using namespace TerrainBuild;

namespace
{
    constexpr int kSub = GridWorld::kHeightSub;
    constexpr float kStep = kCs / kSub;            // ノードの間隔（0.5m）
    constexpr float kRawMin = -9.0f, kRawMax = 12.0f;   // 素の起伏の範囲（区域の基準から m）。洞窟の底（-10）より上に留める

    float OriginX(const MapData::Map& m) { return -0.5f * m.gw * kCs; }
    float OriginZ(const MapData::Map& m) { return -0.5f * m.gd * kCs; }
}

namespace MapTerrainEdit
{
    // 筆の中心のマスの区域の起伏だけ変える（平原の上で塗れば平原、山頂の上なら山頂。洞窟の底は平らなので何もしない）。
    // 形は (1 - (d/r)^2)^2（真ん中が一番強く、縁で 0）。amount = 真ん中での変化量（m）/ 混ぜる割合（0〜1）
    int BrushRelief(MapData::Map& map, float x, float z, float radius, BrushMode mode, float amount, float target)
    {
        if (!HasParts(map) || !map.relief || radius <= 0.0f) return 0;
        const int nx = map.gw * kSub + 1, nz = map.gd * kSub + 1;
        const float ox = OriginX(map), oz = OriginZ(map);
        const int cx = std::clamp((int)std::floor((x - ox) / kCs), 1, map.gw - 2);
        const int cz = std::clamp((int)std::floor((z - oz) / kCs), 1, map.gd - 2);
        const uint8_t zone = map.zone[(size_t)cz * map.gw + cx];
        if (zone == ReliefField::kMine) return 0;
        std::vector<float>& rawV = (zone == ReliefField::kSummit) ? map.rawSummit : map.rawPlain;
        float* raw = rawV.data();
        const float zoneBase = (zone == ReliefField::kSummit) ? map.summitH : 0.0f;
        // 筆で足した分は別に覚える（素の起伏を全体の設定・丘の部品から作り直す時に足し戻す。MapHillEdit の RebuildRaw）
        float* sculpt = nullptr;
        if (map.hasReliefParams)
        {
            std::vector<float>& sv = (zone == ReliefField::kSummit) ? map.sculptSummit : map.sculptPlain;
            if (sv.size() != rawV.size()) sv.assign(rawV.size(), 0.0f);
            sculpt = sv.data();
        }

        // 縁のノード（外周の崖の下）は触らない
        const int ix0 = (std::max)((int)std::floor((x - radius - ox) / kStep), 1);
        const int ix1 = (std::min)((int)std::ceil((x + radius - ox) / kStep), nx - 2);
        const int iz0 = (std::max)((int)std::floor((z - radius - oz) / kStep), 1);
        const int iz1 = (std::min)((int)std::ceil((z + radius - oz) / kStep), nz - 2);
        if (ix1 < ix0 || iz1 < iz0) return 0;

        // 均しは周りの値を読むので、書く前の値を写しておく
        std::vector<float> before;
        if (mode == BrushMode::Smooth)
        {
            before.resize((size_t)(ix1 - ix0 + 3) * (iz1 - iz0 + 3));
            for (int iz = iz0 - 1; iz <= iz1 + 1; ++iz)
                for (int ix = ix0 - 1; ix <= ix1 + 1; ++ix)
                    before[(size_t)(iz - iz0 + 1) * (ix1 - ix0 + 3) + (ix - ix0 + 1)] = raw[(size_t)iz * nx + ix];
        }
        auto old = [&](int ix, int iz) { return before[(size_t)(iz - iz0 + 1) * (ix1 - ix0 + 3) + (ix - ix0 + 1)]; };

        int changed = 0;
        for (int iz = iz0; iz <= iz1; ++iz)
            for (int ix = ix0; ix <= ix1; ++ix)
            {
                const float dx = ox + ix * kStep - x, dz = oz + iz * kStep - z;
                const float u2 = (dx * dx + dz * dz) / (radius * radius);
                if (u2 >= 1.0f) continue;
                const float w = (1.0f - u2) * (1.0f - u2);
                float& v = raw[(size_t)iz * nx + ix];
                float nv = v;
                switch (mode)
                {
                case BrushMode::Raise:   nv = v + amount * w; break;
                case BrushMode::Lower:   nv = v - amount * w; break;
                case BrushMode::Smooth:
                {
                    // 周り 3x3 の平均へ寄せる
                    float sum = 0.0f;
                    for (int kz = -1; kz <= 1; ++kz)
                        for (int kx = -1; kx <= 1; ++kx) sum += old(ix + kx, iz + kz);
                    nv = v + (sum / 9.0f - v) * std::clamp(amount, 0.0f, 1.0f) * w;
                    break;
                }
                case BrushMode::Flatten: nv = v + ((target - zoneBase) - v) * std::clamp(amount, 0.0f, 1.0f) * w; break;
                }
                nv = std::clamp(nv, kRawMin, kRawMax);
                if (nv != v)
                {
                    if (sculpt) sculpt[(size_t)iz * nx + ix] += nv - v;   // 筆の分（設定・丘の部品を変えても残す）
                    v = nv;
                    ++changed;
                }
            }
        return changed;
    }

    // 筆で塗り終わった後の作り直し。起伏・高さ場・通行・草のマス（Rederive）に加えて、
    // 地面を床の箱の上面より下げた時は箱も下げ直す（箱の上面が地面から出ると、見えない段に引っ掛かる）
    void FinishBrush(MapData::Map& map)
    {
        if (!HasParts(map)) return;
        Rederive(map);
        if (!map.relief || !map.hasZoneParams) return;
        const float plainMin = *std::min_element(map.reliefPlain.begin(), map.reliefPlain.end()) - 0.1f;
        const float summitMin = map.summitH + *std::min_element(map.reliefSummit.begin(), map.reliefSummit.end()) - 0.1f;
        if (plainMin < map.floorPlainTop || summitMin < map.floorSummitTop)
        {
            map.floorPlainTop = (std::min)(map.floorPlainTop, plainMin);
            map.floorSummitTop = (std::min)(map.floorSummitTop, summitMin);
            RegenZones(map, false);   // 床の箱を作り直す（洞の上の岩・松明は置き直さない）
        }
    }
}
