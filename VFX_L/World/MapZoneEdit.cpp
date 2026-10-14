// ============================================================
// MapZoneEdit.cpp
// MapTerrainEdit のうち、区域（平原 / 山頂 / 洞窟）の塗り替え（地図エディタの 4 歩目の 2 段目）。
// 区域の表（Map::zone）を書き換えた後、区域の形から決まる物を全部作り直す。
// 作り方は生成（TerrainGenerator）と同じ関数（TerrainBuild の TerrainZones.cpp）
// ============================================================
#include "World/MapTerrainEdit.h"
#include "World/MapEdit.h"
#include "World/TerrainBuild.h"

using namespace TerrainBuild;

namespace
{
    bool InRects(const std::vector<Rect>& rects, int x, int z)
    {
        for (const Rect& r : rects)
            if (x >= r.x && x < r.x + r.w && z >= r.z && z < r.z + r.d) return true;
        return false;
    }

    // 区域から決まる記録か（床の箱、洞の岩の壁・屋根）
    bool IsZoneKind(uint16_t kind) { return kind == MapData::kFloor || kind == MapData::kCaveRoof; }

    template <class T> void EraseZoneKinds(std::vector<T>& v)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [](const T& r) { return IsZoneKind(r.tag.kind); }), v.end());
    }

    // 洞窟の下り坂から、洞の口（上端の外 1 列、平原側）と降り口（下端の先 1 列、底側）
    void MineRampRects(const MapData::Map& map, std::vector<Rect>& ramps, std::vector<Rect>& mouths, std::vector<Rect>& landings)
    {
        for (const auto& p : map.rampParts)
        {
            if (p.tag.kind != MapData::kMineRamp) continue;
            const Rect r = { p.x, p.z, p.w, p.d };
            ramps.push_back(r);
            mouths.push_back(LandingRect(r, (Side)(p.side ^ 1)));   // 下る向きの反対 = 高い端の先
            landings.push_back(LandingRect(r, (Side)p.side));
        }
    }

    // 坂の上端の辺の中央と下る向き（TerrainGenerator::Layout::Ramp。箱・Boss の門の置き場所用）
    MapData::Ramp RampInfo(const GridWorld& origin, const MapData::RampPart& p)
    {
        const int dx = (p.side == 0) ? 1 : (p.side == 1) ? -1 : 0;
        const int dz = (p.side == 2) ? 1 : (p.side == 3) ? -1 : 0;
        const Rect r = { p.x, p.z, p.w, p.d };
        const Vector3 c = RectMin(origin, r) + Vector3(r.w * kCs * 0.5f, 0.0f, r.d * kCs * 0.5f);
        const float half = ((dx != 0) ? r.w : r.d) * kCs * 0.5f;
        MapData::Ramp info;
        info.down = Vector3((float)dx, 0.0f, (float)dz);
        info.top = c - info.down * half;
        info.top.y = p.top;
        return info;
    }
}

namespace MapTerrainEdit
{
    bool CanEditZones(const MapData::Map& map)
    {
        return HasParts(map) && map.hasZoneParams;
    }

    int PaintZone(MapData::Map& map, int cx, int cz, int radius, uint8_t zone)
    {
        if (zone > 2) return 0;
        int changed = 0;
        const float r2 = ((float)radius + 0.5f) * ((float)radius + 0.5f);
        for (int z = cz - radius; z <= cz + radius; ++z)
            for (int x = cx - radius; x <= cx + radius; ++x)
            {
                if (x < 1 || z < 1 || x >= map.gw - 1 || z >= map.gd - 1) continue;   // 外周の崖のマスは塗らない
                const float dx = (float)(x - cx), dz = (float)(z - cz);
                if (dx * dx + dz * dz > r2) continue;
                uint8_t& c = map.zone[(size_t)z * map.gw + x];
                if (c == zone) continue;
                c = zone;
                ++changed;
            }
        return changed;
    }

    void RegenZones(MapData::Map& map, bool regenProps)
    {
        if (!CanEditZones(map)) return;
        const int gw = map.gw, gd = map.gd;
        GridWorld origin;
        origin.Init(gw, gd);

        std::vector<Rect> mineRamps, mouths, landings;
        MineRampRects(map, mineRamps, mouths, landings);

        // ---- 記録の札：今ある物を引き継ぐ（無ければ新しく振る）----
        MapData::Tag floorTag, roofTag;
        bool hasFloor = false, hasRoof = false;
        for (const auto& b : map.boxes)
        {
            if (b.tag.kind == MapData::kFloor && !hasFloor) { floorTag = b.tag; hasFloor = true; }
            if (b.tag.kind == MapData::kCaveRoof && !hasRoof) { roofTag = b.tag; hasRoof = true; }
        }
        if (!hasFloor) { floorTag.kind = (uint16_t)MapData::kFloor; floorTag.group = map.nextGroup++; }
        if (!hasRoof) { roofTag.kind = (uint16_t)MapData::kCaveRoof; roofTag.group = map.nextGroup++; }

        EraseZoneKinds(map.boxes);
        EraseZoneKinds(map.visuals);
        EraseZoneKinds(map.blocks);

        // 洞窟のマスが 1 つでもあれば屋根を掛ける（生成の時に屋根を掛けた地図だけ = roofTopY が入っている）
        const bool anyMine = std::find(map.zone.begin(), map.zone.end(), (uint8_t)ReliefField::kMine) != map.zone.end();
        const bool cave = anyMine && map.roofTopY > 0.0f;
        map.cave = cave;

        std::vector<uint8_t> mouthMask;
        if (cave) ComputeCaveRing(map.zone, mouths, gw, gd, map.caveRing, mouthMask);
        else map.caveRing.assign((size_t)gw * gd, 0);

        Emitter rec(&map);
        rec.SetTag(floorTag);
        EmitFloorBoxes(rec, origin, map.zone, -map.mineD - 1.0f, map.floorPlainTop, map.floorSummitTop, map.mineD);
        if (cave)
        {
            rec.SetTag(roofTag);
            EmitCaveRoof(rec, nullptr, origin, map.zone, map.caveRing, mouthMask, map.roofBottomY, map.roofTopY,
                map.roofCollTop, PaletteFor((TerrainGenerator::Biome)map.biome).cliffHigh);
        }
        MapEdit::RebuildWalkable(map);   // 下の「歩ける底」「山頂の歩けるマス」が見る

        auto zoneAt = [&](int x, int z) { return map.zone[(size_t)z * gw + x]; };
        auto walkable = [&](int x, int z) { return map.walkable[(size_t)z * gw + x] != 0; };

        // ---- 洞の上の岩・洞の中の松明：置き直す（岩の乱数は地図の seed から。生成の時とは違う並びになる）----
        if (regenProps)
        {
            auto& props = map.props;
            props.erase(std::remove_if(props.begin(), props.end(), [](const MapData::Prop& p)
                {
                    return p.tag.kind == MapData::kRoofRock || (p.tag.kind == MapData::kTorch && p.pos.y < -1.0f);
                }), props.end());
            map.torches.erase(std::remove_if(map.torches.begin(), map.torches.end(),
                [](const Vector3& t) { return t.y < -1.0f; }), map.torches.end());
            if (cave)
            {
                std::mt19937 rng(map.seed * 2654435761u + 97u);
                if (map.roofBoulders)
                    EmitRoofBoulders(rec, origin, map.zone, map.caveRing, map.roofTopY, map.roofRockMin, map.roofRockMax,
                        map.roofRockModels, rng);
                else
                    EmitRoofRocks(rec, origin, map.zone, map.caveRing, map.roofTopY, map.roofRockMin, map.roofRockMax,
                        map.roofRockModels, rng);
                if (!map.torchModel.empty())
                    EmitCaveTorches(rec, origin,
                        [&](int x, int z)   // 歩ける底（坂・降り口・岩は除く）
                        {
                            return zoneAt(x, z) == ReliefField::kMine && walkable(x, z)
                                && !InRects(mineRamps, x, z) && !InRects(landings, x, z);
                        },
                        map.caveRing, map.mineD, map.caveTorchSpacing, map.rimRock, map.rimSink, map.torchModel, map.torches);
            }
        }

        // ---- 三層の結果（箱・Boss の門の置き場所）----
        map.summitRamps.clear();
        map.mineRamps.clear();
        for (const auto& p : map.rampParts)
        {
            if (p.tag.kind == MapData::kSummitRamp) map.summitRamps.push_back(RampInfo(origin, p));
            if (p.tag.kind == MapData::kMineRamp) map.mineRamps.push_back(RampInfo(origin, p));
        }
        ComputeLayoutCells(origin,
            [&](int x, int z) { return zoneAt(x, z) == ReliefField::kSummit && walkable(x, z); },
            [&](int x, int z) { return zoneAt(x, z) == ReliefField::kMine && !InRects(mineRamps, x, z) && walkable(x, z); },
            landings, map.mineD, map.summitCells, map.mineCells, map.hasMineDeep, map.mineDeep);

        Rederive(map);
    }

    uint32_t AddZoneRamp(MapData::Map& map, bool summit, int x, int z, int side, int width, int length)
    {
        if (!CanEditZones(map) || side < 0 || side > 3) return 0;
        const Palette& P = PaletteFor((TerrainGenerator::Biome)map.biome);
        const bool alongX = side < 2;
        MapData::RampPart r;
        r.tag.kind = (uint16_t)(summit ? MapData::kSummitRamp : MapData::kMineRamp);
        r.tag.group = map.nextGroup++;
        r.side = (uint8_t)side;
        r.w = alongX ? (std::max)(length, 1) : (std::max)(width, 1);
        r.d = alongX ? (std::max)(width, 1) : (std::max)(length, 1);
        r.x = std::clamp(x, 2, map.gw - 2 - r.w);
        r.z = std::clamp(z, 2, map.gd - 2 - r.d);
        r.topColor = summit ? P.groundLight : P.rampTop;
        r.sideColor = P.cliff;
        r.grassy = summit;
        r.onGround = summit;                 // 山頂の坂は麓を均す。洞窟の坂は坑の中（平ら）
        r.base = summit ? 0.0f : -map.mineD; // 山頂の坂の麓は Refresh が地面に合わせる
        r.top = summit ? map.summitH : 0.0f;
        r.owner = -1;
        map.rampParts.push_back(r);
        Refresh(map, r.tag.group);
        return r.tag.group;
    }
}

// 区域から作り直した物（床・洞の壁 / 屋根の記録、岩の壁のマス、三層の結果）が今の記録と同じか。
// 岩・松明は置き直さない（乱数の並びが生成と違うので比べない）
bool MapTerrainEdit::detail::ZonesMatch(const MapData::Map& map)
{
    if (!CanEditZones(map)) return false;
    MapData::Map copy = map;
    RegenZones(copy, false);
    auto sameRamps = [](const std::vector<MapData::Ramp>& a, const std::vector<MapData::Ramp>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].top != b[i].top || a[i].down != b[i].down) return false;
            return true;
        };
    return copy.caveRing == map.caveRing && copy.cave == map.cave
        && copy.boxes.size() == map.boxes.size() && copy.visuals.size() == map.visuals.size()
        && copy.blocks.size() == map.blocks.size() && RecordSum(copy) == RecordSum(map)
        && copy.summitCells == map.summitCells && copy.mineCells == map.mineCells
        && copy.hasMineDeep == map.hasMineDeep && (!map.hasMineDeep || copy.mineDeep == map.mineDeep)
        && sameRamps(copy.summitRamps, map.summitRamps) && sameRamps(copy.mineRamps, map.mineRamps);
}
