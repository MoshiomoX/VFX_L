// ============================================================
// MapHillEdit.cpp
// MapTerrainEdit のうち、起伏の中身の編集（2026-10-05、ユーザー：丘・起伏も部品と同じ扱いにしたい）:
//   全体の設定（丘の高さ・大きさ、細かいうねり、山頂の倍率、傾きの上限）
//   丘の部品（1 個の盛り上がり / 窪み。生成が撒く土饅頭もこれ）
// 素の起伏 = ノイズの丘（設定）+ 丘の部品 → 傾きを抑える → + 起伏の筆で足した分。
// 作り方は生成（TerrainGenerator）と同じ関数（TerrainBuild の BuildReliefNoise / AddHillsAndLimit）
// ============================================================
#include "World/MapTerrainEdit.h"
#include "World/TerrainBuild.h"

using namespace TerrainBuild;

namespace
{
    constexpr int kSub = GridWorld::kHeightSub;

    // ノイズの丘は全体（16 万ノード × 勾配ノイズ 4 回）で重いので、設定が変わるまで使い回す
    // （丘の部品を 1 つ動かす度に計算し直さない）
    struct NoiseCache
    {
        bool valid = false;
        uint32_t seed = 0;
        int gw = 0, gd = 0;
        MapData::ReliefParams rp;
        std::vector<float> plain, summit;
    };
    NoiseCache& Cache() { static NoiseCache c; return c; }

    bool SameParams(const MapData::ReliefParams& a, const MapData::ReliefParams& b)
    {
        return a.hillHeight == b.hillHeight && a.hillScale == b.hillScale && a.detailHeight == b.detailHeight
            && a.detailScale == b.detailScale && a.summitMul == b.summitMul;
    }

    // ノイズ + 丘の部品 → 傾きを抑える（筆の分は入れない）
    void BuildBase(const MapData::Map& map, std::vector<float>& plain, std::vector<float>& summit)
    {
        NoiseCache& c = Cache();
        if (!c.valid || c.seed != map.seed || c.gw != map.gw || c.gd != map.gd || !SameParams(c.rp, map.reliefParams))
        {
            BuildReliefNoise(map.reliefParams, map.seed, map.gw, map.gd, c.plain, c.summit);
            c.valid = true;
            c.seed = map.seed; c.gw = map.gw; c.gd = map.gd; c.rp = map.reliefParams;
        }
        plain = c.plain;
        summit = c.summit;
        AddHillsAndLimit(map.hills, map.reliefParams.summitMul, map.reliefMaxSlopeDeg, map.gw, map.gd, plain, summit);
    }
}

namespace MapTerrainEdit
{
    bool CanEditHills(const MapData::Map& map)
    {
        return HasParts(map) && map.relief && map.hasReliefParams;
    }

    // 素の起伏を、全体の設定・丘の部品・筆の分から作り直す。この後 Rederive（+ ReseatAll）を呼ぶ
    void RebuildRaw(MapData::Map& map)
    {
        if (!CanEditHills(map)) return;
        BuildBase(map, map.rawPlain, map.rawSummit);
        const size_t n = map.rawPlain.size();
        if (map.sculptPlain.size() == n)
            for (size_t i = 0; i < n; ++i) map.rawPlain[i] += map.sculptPlain[i];
        if (map.sculptSummit.size() == n)
            for (size_t i = 0; i < n; ++i) map.rawSummit[i] += map.sculptSummit[i];
    }

    int FindHill(const MapData::Map& map, uint32_t group)
    {
        for (int i = 0; i < (int)map.hills.size(); ++i)
            if (map.hills[(size_t)i].tag.group == group) return i;
        return -1;
    }

    uint32_t HillAt(const MapData::Map& map, float x, float z)
    {
        uint32_t best = 0;
        float bestR = 1.0e9f;
        for (const auto& h : map.hills)
        {
            const float dx = x - h.x, dz = z - h.z;
            if (dx * dx + dz * dz < h.radius * h.radius && h.radius < bestR) { bestR = h.radius; best = h.tag.group; }
        }
        return best;
    }

    uint32_t AddHill(MapData::Map& map, float x, float z, float radius, float height)
    {
        if (!CanEditHills(map)) return 0;
        MapData::Hill h;
        h.tag.kind = (uint16_t)MapData::kHill;
        h.tag.group = map.nextGroup++;
        h.x = x; h.z = z;
        h.radius = (std::max)(radius, 1.0f);
        h.height = height;
        map.hills.push_back(h);
        RebuildRaw(map);
        return h.tag.group;
    }

    void DeleteHill(MapData::Map& map, uint32_t group)
    {
        const int i = FindHill(map, group);
        if (i < 0) return;
        map.hills.erase(map.hills.begin() + i);
        RebuildRaw(map);
    }

    // 全部の地形の部品の足元を、今の地面へ合わせ直す（起伏を大きく変えた後。台地が浮いたり埋まったりしないように）。
    // 記録・台座を作り直して、最後に 1 回だけ Rederive
    void ReseatAll(MapData::Map& map)
    {
        if (!HasParts(map)) return;
        std::vector<uint32_t> groups;
        for (const auto& p : map.blockParts) groups.push_back(p.tag.group);
        for (const auto& p : map.rampParts) groups.push_back(p.tag.group);
        std::sort(groups.begin(), groups.end());
        groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
        for (uint32_t g : groups) Refresh(map, g, false);
        if (CanEditZones(map)) RegenZones(map, false);   // 最後に Rederive（洞窟の坂の高さは変わらないが、まとめて 1 回）
        else Rederive(map);
    }
}

// 生成した直後の地図で：全体の設定と丘の部品から作り直した素の起伏が、生成の物と 1 ビットも違わないか
bool MapTerrainEdit::detail::RawMatches(const MapData::Map& map)
{
    if (!CanEditHills(map)) return false;
    std::vector<float> plain, summit;
    BuildBase(map, plain, summit);
    const size_t bytes = plain.size() * sizeof(float);
    return plain.size() == map.rawPlain.size() && summit.size() == map.rawSummit.size()
        && std::memcmp(plain.data(), map.rawPlain.data(), bytes) == 0
        && std::memcmp(summit.data(), map.rawSummit.data(), bytes) == 0;
}
