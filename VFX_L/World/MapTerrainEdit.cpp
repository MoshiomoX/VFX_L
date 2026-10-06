// ============================================================
// MapTerrainEdit.cpp
// ============================================================
#include "World/MapTerrainEdit.h"
#include "World/MapEdit.h"
#include "World/TerrainBuild.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <string>

using namespace TerrainBuild;

namespace
{
    constexpr int   kSub = GridWorld::kHeightSub;
    constexpr float kPadSink = 0.5f;      // 地面に載る箱の底を埋める深さ（TerrainGenerator と同じ）
    constexpr int   kTerraceApron = 5;    // 高台の坂の麓の空き地（TerrainGenerator::Config::terraceClear の既定）
    constexpr int   kSummitApron = 8;     // 山頂の坂の麓の空き地（TerrainGenerator の kSummitRunout）

    bool HasKind(const MapData::Map& m, uint32_t group, uint16_t kind)
    {
        for (const auto& p : m.rampParts) if (p.tag.group == group && p.tag.kind == kind) return true;
        return false;
    }

    template <class T> void EraseGroup(std::vector<T>& v, uint32_t group)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [group](const T& r) { return r.tag.group == group; }), v.end());
    }

    // 外周のマスは内側の隣と同じ区域（ReliefField::ZoneAtCell と同じ）
    uint8_t ZoneAt(const MapData::Map& m, int gx, int gz)
    {
        gx = std::clamp(gx, 1, m.gw - 2);
        gz = std::clamp(gz, 1, m.gd - 2);
        return m.zone[(size_t)gz * m.gw + gx];
    }
    float ZoneBase(const MapData::Map& m, uint8_t zone)
    {
        return zone == ReliefField::kSummit ? m.summitH : zone == ReliefField::kMine ? -m.mineD : 0.0f;
    }
    uint8_t ZoneOfRect(const MapData::Map& m, int x, int z, int w, int d)
    {
        return ZoneAt(m, x + w / 2, z + d / 2);
    }

    bool AlongX(uint8_t side) { return side == (uint8_t)Side::PosX || side == (uint8_t)Side::NegX; }

    MapData::Pad MakePad(const MapData::Map& m, const MapData::Tag& tag, uint8_t zone, const Rect& r, int rim, float L)
    {
        MapData::Pad p;
        p.tag = tag;
        p.zone = zone;
        p.ax0 = (r.x - rim) * kSub; p.ax1 = (r.x + r.w + rim) * kSub;
        p.az0 = (r.z - rim) * kSub; p.az1 = (r.z + r.d + rim) * kSub;
        p.m = (std::max)(m.padMargin, 1) * kSub;
        p.L = L;
        return p;
    }

    // 洞窟の周りを均す重み。区域の表だけで決まり、全体の距離変換が重いので、区域が変わるまで使い回す
    // （部品を動かす度に Refresh と Rederive で 1 回ずつ計算していた）
    const std::vector<float>& MineWeights(const MapData::Map& m)
    {
        struct Cache { std::vector<uint8_t> zone; int gw = 0, gd = 0, margin = -1; std::vector<float> w; };
        static Cache c;
        if (c.gw != m.gw || c.gd != m.gd || c.margin != m.padMargin || c.zone != m.zone)
        {
            ComputeMineWeights(m.zone, m.gw, m.gd, m.padMargin, c.w);
            c.zone = m.zone; c.gw = m.gw; c.gd = m.gd; c.margin = m.padMargin;
        }
        return c.w;
    }

    // 素の起伏と台座から、区域の面を全部作る（仕上げの傾きの制限は limit の時だけ）
    void Compose(const MapData::Map& m, std::vector<float>& plain, std::vector<float>& summit, bool limit)
    {
        const int nx = m.gw * kSub + 1, nz = m.gd * kSub + 1;
        const std::vector<float>& mineW = MineWeights(m);
        plain.assign(m.rawPlain.size(), 0.0f);
        summit.assign(m.rawSummit.size(), 0.0f);
        ComposeRelief(plain, m.rawPlain, m.pads, ReliefField::kPlain, &mineW, nx, nz, 0, 0, nx - 1, nz - 1);
        ComposeRelief(summit, m.rawSummit, m.pads, ReliefField::kSummit, nullptr, nx, nz, 0, 0, nx - 1, nz - 1);
        if (!limit) return;
        const float step = kCs / kSub;
        LimitReliefSlopes(plain, m.pads, ReliefField::kPlain, &mineW, nx, nz, step, m.reliefMaxSlopeDeg);
        LimitReliefSlopes(summit, m.pads, ReliefField::kSummit, nullptr, nx, nz, step, m.reliefMaxSlopeDeg);
        ApplyRimRise(plain, nx, nz, step, m.rimRiseWidth, m.rimRiseHeight);   // 縁の碗（生成と同じ）
        ApplyRimRise(summit, nx, nz, step, m.rimRiseWidth, m.rimRiseHeight);
    }

    // 矩形（マス）のノードの起伏の平均（TerrainGenerator の avgRelief と同じ範囲）
    float AvgRelief(const MapData::Map& m, const std::vector<float>& arr, const Rect& r)
    {
        const int nx = m.gw * kSub + 1, nz = m.gd * kSub + 1;
        double sum = 0.0;
        int n = 0;
        for (int iz = (std::max)(r.z * kSub, 0); iz <= (std::min)((r.z + r.d) * kSub, nz - 1); ++iz)
            for (int ix = (std::max)(r.x * kSub, 0); ix <= (std::min)((r.x + r.w) * kSub, nx - 1); ++ix)
            {
                sum += arr[(size_t)iz * nx + ix];
                ++n;
            }
        return n > 0 ? (float)(sum / n) : 0.0f;
    }

}

namespace MapTerrainEdit::detail
{
    // 記録の中身の合計（順番に依らない比べ方）
    static uint64_t Fnv(const void* data, size_t n, uint64_t h)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
    std::string& LastPerf() { static std::string s; return s; }   // TEMP-TEST: Rederive の内訳（ms）

    uint64_t RecordSum(const MapData::Map& m)
    {
        constexpr uint64_t kSeed = 1469598103934665603ull;
        uint64_t sum = 0;
        auto tag = [](const MapData::Tag& t, uint64_t h) { h = Fnv(&t.kind, sizeof(t.kind), h); return Fnv(&t.group, sizeof(t.group), h); };
        for (const auto& b : m.boxes)
        {
            uint64_t h = tag(b.tag, kSeed);
            h = Fnv(&b.lo, sizeof(b.lo), h); h = Fnv(&b.hi, sizeof(b.hi), h); h = Fnv(&b.layer, sizeof(b.layer), h);
            sum += h;
        }
        for (const auto& b : m.hulls)
        {
            uint64_t h = tag(b.tag, kSeed + 1);
            h = Fnv(b.v, sizeof(b.v), h); h = Fnv(&b.layer, sizeof(b.layer), h);
            sum += h;
        }
        for (const auto& b : m.visuals)
        {
            uint64_t h = tag(b.tag, kSeed + 2);
            h = Fnv(b.v, sizeof(b.v), h); h = Fnv(&b.top, sizeof(b.top), h); h = Fnv(&b.side, sizeof(b.side), h);
            h = Fnv(&b.topLayer, sizeof(int), h); h = Fnv(&b.sideLayer, sizeof(int), h);
            sum += h;
        }
        for (const auto& b : m.blocks)
        {
            uint64_t h = tag(b.tag, kSeed + 3);
            const int v[4] = { b.x, b.z, b.w, b.d };
            sum += Fnv(v, sizeof(v), h);
        }
        return sum;
    }
}

namespace MapTerrainEdit
{
    bool HasParts(const MapData::Map& map)
    {
        const size_t nodes = (size_t)(map.gw * kSub + 1) * (map.gd * kSub + 1);
        return map.gw > 0 && map.rawPlain.size() == nodes && map.rawSummit.size() == nodes
            && map.reliefPlain.size() == nodes;
    }

    MapData::BlockPart* Block(MapData::Map& map, uint32_t group, int ordinal)
    {
        for (auto& p : map.blockParts)
            if (p.tag.group == group && ordinal-- == 0) return &p;
        return nullptr;
    }
    MapData::RampPart* Ramp(MapData::Map& map, uint32_t group, int ordinal)
    {
        for (auto& p : map.rampParts)
            if (p.tag.group == group && ordinal-- == 0) return &p;
        return nullptr;
    }
    int BlockCount(const MapData::Map& map, uint32_t group)
    {
        int n = 0;
        for (const auto& p : map.blockParts) if (p.tag.group == group) ++n;
        return n;
    }
    int RampCount(const MapData::Map& map, uint32_t group)
    {
        int n = 0;
        for (const auto& p : map.rampParts) if (p.tag.group == group) ++n;
        return n;
    }

    uint32_t GroupAt(const MapData::Map& map, float x, float z)
    {
        const int gx = (int)std::floor((x + 0.5f * map.gw * kCs) / kCs), gz = (int)std::floor((z + 0.5f * map.gd * kCs) / kCs);
        uint32_t best = 0;
        float bestTop = -1.0e9f;
        for (const auto& p : map.blockParts)
            if (gx >= p.x && gx < p.x + p.w && gz >= p.z && gz < p.z + p.d && p.top > bestTop) { bestTop = p.top; best = p.tag.group; }
        if (best) return best;
        for (const auto& p : map.rampParts)
            if (gx >= p.x && gx < p.x + p.w && gz >= p.z && gz < p.z + p.d) return p.tag.group;
        return 0;
    }

    // ============================================================
    // 起伏・高さ場・通行・草のマスの作り直し
    // ============================================================
    void Rederive(MapData::Map& map)
    {
        if (!HasParts(map)) return;
        GridWorld g;
        g.Init(map.gw, map.gd);

        const auto tp0 = std::chrono::steady_clock::now();
        if (map.relief) Compose(map, map.reliefPlain, map.reliefSummit, true);
        const auto tp1 = std::chrono::steady_clock::now();

        // ---- 高さ場：起伏の面 → 箱の上面・坂（高い方）。洞の岩の壁のマスは元の値のまま（生成が隣のマスから埋めた値）----
        ReliefField rf;
        rf.Init(g, map.zone, map.summitH, map.mineD);
        rf.plain = map.reliefPlain;
        rf.summit = map.reliefSummit;
        const int hw = map.gw * kSub, hd = map.gd * kSub;
        for (int hz = 0; hz < hd; ++hz)
            for (int hx = 0; hx < hw; ++hx)
            {
                const size_t c = (size_t)(hz / kSub) * map.gw + (hx / kSub);
                if (map.caveRing[c]) { g.SetHeightExact(hx, hz, 0.0f); continue; }   // 下でまとめて埋める
                const DirectX::SimpleMath::Vector3 p = g.HeightCellToWorld(hx, hz);
                g.SetHeightExact(hx, hz, rf.SurfaceIn(rf.ZoneAtCell(hx / kSub, hz / kSub), p.x, p.z));
            }
        const auto tp2 = std::chrono::steady_clock::now();
        Emitter none(nullptr);   // 何も記録しない（高さ場だけ書く）
        auto raiseParts = [&]()
            {
                for (const auto& p : map.blockParts) if (p.raise) EmitBlockPart(none, &g, g, p);
                for (const auto& p : map.rampParts) EmitRampPart(none, &g, g, p);
            };
        raiseParts();
        MapEdit::RebuildWalkable(map);   // 下の洞の岩の壁の埋めが「歩けるマス」を見る

        // 洞の岩の壁のマス：一番近い「歩ける（塞いでいない）マス」の高さで埋める（TerrainGenerator と同じ探し方。歩けないので誰も立たないが、
        // 隣の歩けるマスの双線形の高さに混ざる）。元の値を取っておく方式だと、上に部品を置いて消した時に戻らない
        {
            auto open = [&](int cx, int cz)   // 生成が埋めた時点で歩けたマス（外周の崖・洞の岩の壁・縁の碗の斜面などは塞いである）
                {
                    return cx >= 0 && cz >= 0 && cx < map.gw && cz < map.gd && map.walkable[(size_t)cz * map.gw + cx]
                        && !map.caveRing[(size_t)cz * map.gw + cx];
                };
            std::vector<std::pair<int, float>> fill;
            for (int z = 0; z < map.gd; ++z)
                for (int x = 0; x < map.gw; ++x)
                {
                    if (!map.caveRing[(size_t)z * map.gw + x]) continue;
                    for (int sz = 0; sz < kSub; ++sz)
                        for (int sx = 0; sx < kSub; ++sx)
                        {
                            const int hx = x * kSub + sx, hz = z * kSub + sz;
                            int best = INT_MAX;
                            float bh = 0.0f;
                            for (int r = 1; r <= 2 * kSub && best > r * r; ++r)
                                for (int dz = -r; dz <= r; ++dz)
                                    for (int dx = -r; dx <= r; ++dx)
                                    {
                                        if ((std::max)(std::abs(dx), std::abs(dz)) != r) continue;
                                        const int nx = hx + dx, nz = hz + dz;
                                        if (nx < 0 || nz < 0 || !open(nx / kSub, nz / kSub)) continue;
                                        const int d2 = dx * dx + dz * dz;
                                        if (d2 < best) { best = d2; bh = g.HeightAt(nx, nz); }
                                    }
                            fill.push_back({ hz * hw + hx, bh });
                        }
                }
            for (const auto& f : fill) g.SetHeightExact(f.first % hw, f.first / hw, f.second);
            if (!fill.empty()) raiseParts();   // 岩の壁の上に置いた部品（高い方が勝つ）
        }
        const auto tp3 = std::chrono::steady_clock::now();
        map.heights = g.Heights();

        // ---- 草を生やすマス：洞窟・洞の岩の壁・土の坂・登れない台地以外 ----
        map.grassMask.assign((size_t)map.gw * map.gd, 1);
        for (int z = 0; z < map.gd; ++z)
            for (int x = 0; x < map.gw; ++x)
                if (ZoneAt(map, x, z) == ReliefField::kMine || map.caveRing[(size_t)z * map.gw + x])
                    map.grassMask[(size_t)z * map.gw + x] = 0;
        auto clear = [&](int x0, int z0, int w, int d)
            {
                for (int z = (std::max)(z0, 0); z < (std::min)(z0 + d, map.gd); ++z)
                    for (int x = (std::max)(x0, 0); x < (std::min)(x0 + w, map.gw); ++x)
                        map.grassMask[(size_t)z * map.gw + x] = 0;
            };
        for (const auto& p : map.rampParts) if (!p.grassy) clear(p.x, p.z, p.w, p.d);
        for (const auto& p : map.blockParts) if (!p.raise) clear(p.x, p.z, p.w, p.d);
        {
            const auto tp4 = std::chrono::steady_clock::now();
            auto ms = [](auto a, auto b) { return std::chrono::duration<float, std::milli>(b - a).count(); };
            char buf[160];
            snprintf(buf, sizeof(buf), "compose %.0f base %.0f raise+ring %.0f walk+grass %.0f ms", ms(tp0, tp1), ms(tp1, tp2), ms(tp2, tp3), ms(tp3, tp4));
            detail::LastPerf() = buf;
        }
    }

    void RegenRecords(MapData::Map& map, uint32_t group)
    {
        EraseGroup(map.boxes, group);
        EraseGroup(map.hulls, group);
        EraseGroup(map.visuals, group);
        EraseGroup(map.blocks, group);
        GridWorld origin;
        origin.Init(map.gw, map.gd);
        // 部品の表を回しながら記録へ足すので、部品は先に写しておく
        const std::vector<MapData::BlockPart> blocks = map.blockParts;
        const std::vector<MapData::RampPart> ramps = map.rampParts;
        Emitter rec(&map);
        for (const auto& p : blocks)
            if (p.tag.group == group) { rec.SetTag(p.tag); EmitBlockPart(rec, nullptr, origin, p); }
        for (const auto& p : ramps)
            if (p.tag.group == group) { rec.SetTag(p.tag); EmitRampPart(rec, nullptr, origin, p); }
    }

    // 台座の作り方は TerrainGenerator と同じ：箱は周り 1 マスまで、台地の坂道は坂 + 降り口、高台の坂は坂 + 麓の空き地
    void RegenPads(MapData::Map& map, uint32_t group)
    {
        EraseGroup(map.pads, group);
        if (!map.relief) return;
        for (const auto& p : map.blockParts)
        {
            if (p.tag.group != group || !p.onGround) continue;
            const uint8_t zone = ZoneOfRect(map, p.x, p.z, p.w, p.d);
            if (zone == ReliefField::kMine) continue;   // 洞窟の底は平ら
            map.pads.push_back(MakePad(map, p.tag, zone, { p.x, p.z, p.w, p.d }, 1, p.base - ZoneBase(map, zone)));
        }
        for (const auto& p : map.rampParts)
        {
            if (p.tag.group != group || !p.onGround) continue;
            const Rect r = { p.x, p.z, p.w, p.d };
            const uint8_t zone = ZoneOfRect(map, p.x, p.z, p.w, p.d);
            if (zone == ReliefField::kMine) continue;
            const float L = p.base - ZoneBase(map, zone);
            const int across = AlongX(p.side) ? p.d : p.w;
            if (p.tag.kind == MapData::kSummitRamp)
            {
                // 山頂の長い坂：坂と麓（先 8 マス）を麓の高さに、坂の上端の山頂側 2 マス（通り道）を山頂の基準の高さに
                map.pads.push_back(MakePad(map, p.tag, ReliefField::kPlain, r, 1, p.base));
                map.pads.push_back(MakePad(map, p.tag, ReliefField::kPlain,
                    RampRect(r, (Side)p.side, -1, across + 2, kSummitApron), 0, p.base));
                map.pads.push_back(MakePad(map, p.tag, ReliefField::kSummit,
                    RampRect(r, (Side)(p.side ^ 1), 0, across, 2), 1, 0.0f));
            }
            else if (p.grassy)   // 高台の長い坂：麓の先も均す（滑り降りた先が平ら）
            {
                map.pads.push_back(MakePad(map, p.tag, zone, r, 1, L));
                map.pads.push_back(MakePad(map, p.tag, zone, RampRect(r, (Side)p.side, -1, across + 2, kTerraceApron), 0, L));
            }
            else
            {
                map.pads.push_back(MakePad(map, p.tag, zone, r, 0, L));
                map.pads.push_back(MakePad(map, p.tag, zone, LandingRect(r, (Side)p.side), 0, L));
            }
        }
    }

    // ============================================================
    // 部品を変えた後のまとめ
    // ============================================================
    void Refresh(MapData::Map& map, uint32_t group) { Refresh(map, group, true); }

    void Refresh(MapData::Map& map, uint32_t group, bool derive)
    {
        if (!HasParts(map)) return;
        MapData::BlockPart* body = Block(map, group, 0);

        // ---- 足元の合わせ直し：この group の台座を除いた起伏の、本体の足跡の平均 ----
        EraseGroup(map.pads, group);
        if (body && body->onGround)
        {
            const uint8_t zone = ZoneOfRect(map, body->x, body->z, body->w, body->d);
            float ground = ZoneBase(map, zone);
            if (map.relief && zone != ReliefField::kMine)
            {
                // 足跡のノードだけ合成する（全体を合成すると部品を 1 マス動かす度に数十 ms 余計に掛かる）
                const Rect r = { body->x, body->z, body->w, body->d };
                const int nx = map.gw * kSub + 1, nz = map.gd * kSub + 1;
                const bool onSummit = zone == ReliefField::kSummit;
                std::vector<float> arr((size_t)nx * nz, 0.0f);
                ComposeRelief(arr, onSummit ? map.rawSummit : map.rawPlain, map.pads, zone, onSummit ? nullptr : &MineWeights(map),
                    nx, nz, r.x * kSub, r.z * kSub, (r.x + r.w) * kSub, (r.z + r.d) * kSub);
                ground += AvgRelief(map, arr, r);
            }
            const float delta = ground - body->base;
            for (auto& p : map.blockParts)
                if (p.tag.group == group) { p.bottom += delta; p.top += delta; p.base += delta; }
            for (auto& p : map.rampParts)
                if (p.tag.group == group) { p.base += delta; p.top += delta; }
        }

        // ---- 山頂の長い坂（箱に付いていない草の坂）：麓の高さ = 自分の台座を除いた平原の起伏の、麓の空き地の平均 ----
        if (!body && HasKind(map, group, MapData::kSummitRamp))
        {
            std::vector<float> plain, summit;
            if (map.relief) Compose(map, plain, summit, false);
            for (auto& p : map.rampParts)
            {
                if (p.tag.group != group || p.tag.kind != MapData::kSummitRamp) continue;
                const int across = AlongX(p.side) ? p.d : p.w;
                const Rect apron = RampRect({ p.x, p.z, p.w, p.d }, (Side)p.side, -1, across + 2, kSummitApron);
                p.base = map.relief ? AvgRelief(map, plain, apron) : 0.0f;
                p.top = map.summitH;
            }
        }

        // ---- 箱に付いた坂：足跡と高さを箱から作り直す（箱の大きさ・高さを変えた時）----
        for (auto& p : map.rampParts)
        {
            if (p.tag.group != group || p.owner < 0) continue;
            const MapData::BlockPart* owner = Block(map, group, p.owner);
            if (!owner) continue;
            const Rect o = { owner->x, owner->z, owner->w, owner->d };
            const bool alongX = AlongX(p.side);
            const int sideLen = alongX ? o.d : o.w;
            const int width = std::clamp(alongX ? p.d : p.w, 1, sideLen);
            const int len = (std::max)(alongX ? p.w : p.d, 1);
            p.offset = std::clamp(p.offset, 0, sideLen - width);
            const Rect r = RampRect(o, (Side)p.side, p.offset, width, len);
            p.x = r.x; p.z = r.z; p.w = r.w; p.d = r.d;
            p.top = owner->top;
            p.base = owner->base;
        }

        RegenRecords(map, group);
        RegenPads(map, group);
        if (!derive) return;   // まとめて作り直す時（ReseatAll）は呼ぶ側が最後に 1 回
        // 洞窟の下り坂を変えたら、洞の口・岩の壁・屋根・一番奥も変わる（RegenZones の最後に Rederive）
        if (HasKind(map, group, MapData::kMineRamp) && CanEditZones(map)) RegenZones(map);
        else Rederive(map);
    }

    bool MoveGroup(MapData::Map& map, uint32_t group, int dx, int dz, bool refresh)
    {
        if (!HasParts(map) || (dx == 0 && dz == 0)) return false;
        auto inside = [&](int x, int z, int w, int d)   // 外周の崖と、その内側 1 マスは使わない
            { return x + dx >= 2 && z + dz >= 2 && x + dx + w <= map.gw - 2 && z + dz + d <= map.gd - 2; };
        for (const auto& p : map.blockParts) if (p.tag.group == group && !inside(p.x, p.z, p.w, p.d)) return false;
        for (const auto& p : map.rampParts) if (p.tag.group == group && !inside(p.x, p.z, p.w, p.d)) return false;
        for (auto& p : map.blockParts) if (p.tag.group == group) { p.x += dx; p.z += dz; }
        for (auto& p : map.rampParts) if (p.tag.group == group) { p.x += dx; p.z += dz; }
        if (refresh) Refresh(map, group);
        return true;
    }

    void DeleteGroup(MapData::Map& map, uint32_t group)
    {
        const bool zoneRamp = HasKind(map, group, MapData::kMineRamp) || HasKind(map, group, MapData::kSummitRamp);
        EraseGroup(map.blockParts, group);
        EraseGroup(map.rampParts, group);
        EraseGroup(map.pads, group);
        MapEdit::DeleteGroup(map, group);   // 衝突・見た目・塞ぐマスの記録
        if (zoneRamp && CanEditZones(map)) RegenZones(map);   // 洞の口・三層の結果（坂の表）も作り直す
        else Rederive(map);
    }

    uint32_t AddPlateau(MapData::Map& map, int x, int z, int w, int d, float height)
    {
        if (!HasParts(map)) return 0;
        w = std::clamp(w, 1, map.gw - 4);
        d = std::clamp(d, 1, map.gd - 4);
        const Palette& P = PaletteFor((TerrainGenerator::Biome)map.biome);
        MapData::BlockPart p;
        p.tag.kind = (uint16_t)MapData::kPlateau;
        p.tag.group = map.nextGroup++;
        p.x = std::clamp(x, 2, map.gw - 2 - w);
        p.z = std::clamp(z, 2, map.gd - 2 - d);
        p.w = w; p.d = d;
        p.base = 0.0f;                       // Refresh が地面に合わせ直す
        p.bottom = -kPadSink;
        p.top = (std::max)(height, 0.5f);
        p.topColor = P.plateauTop;
        p.sideColor = P.cliff;
        map.blockParts.push_back(p);
        Refresh(map, p.tag.group);
        return p.tag.group;
    }

    bool AddRamp(MapData::Map& map, uint32_t group, int ownerOrdinal, int side, int offset, int width)
    {
        const MapData::BlockPart* owner = Block(map, group, ownerOrdinal);
        if (!owner || side < 0 || side > 3) return false;
        const Palette& P = PaletteFor((TerrainGenerator::Biome)map.biome);
        const float tanRamp = std::tan(DirectX::XMConvertToRadians(std::clamp(map.rampSlopeDeg, 5.0f, 40.0f)));
        const int len = (std::max)(1, (int)std::ceil((owner->top - owner->base) / (kCs * tanRamp)));
        const bool alongX = AlongX((uint8_t)side);
        MapData::RampPart r;
        r.tag = owner->tag;
        r.side = (uint8_t)side;
        r.w = alongX ? len : (std::max)(width, 1);
        r.d = alongX ? (std::max)(width, 1) : len;
        r.topColor = P.rampTop;
        r.sideColor = P.cliff;
        r.grassy = false;
        r.onGround = owner->onGround;
        r.owner = ownerOrdinal;
        r.offset = offset;
        map.rampParts.push_back(r);
        Refresh(map, group);   // 足跡・高さは箱から決まる
        return true;
    }

    // ============================================================
    // 確認：生成器の結果と、部品から作り直した結果を比べる
    // ============================================================
    Check Verify(const MapData::Map& map)
    {
        Check c;
        if (!HasParts(map)) return c;
        MapData::Map copy = map;
        std::vector<uint32_t> groups;
        for (const auto& p : copy.blockParts) groups.push_back(p.tag.group);
        for (const auto& p : copy.rampParts) groups.push_back(p.tag.group);
        std::sort(groups.begin(), groups.end());
        groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
        for (uint32_t g : groups) RegenRecords(copy, g);
        Rederive(copy);

        auto same = [](const std::vector<float>& a, const std::vector<float>& b)
            { return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0); };
        c.reliefSame = same(map.reliefPlain, copy.reliefPlain) && same(map.reliefSummit, copy.reliefSummit);
        for (size_t i = 0; i < map.heights.size() && i < copy.heights.size(); ++i)
        {
            const float d = std::fabs(map.heights[i] - copy.heights[i]);
            c.heightMaxDiff = (std::max)(c.heightMaxDiff, d);
            if (d > 0.001f)
            {
                if (c.heightDiffCells < 6)
                {
                    const int hw = map.gw * kSub;
                    char buf[96];
                    snprintf(buf, sizeof(buf), " [h %d,%d gen %.2f new %.2f]", (int)(i % hw), (int)(i / hw), map.heights[i], copy.heights[i]);
                    c.detail += buf;
                }
                ++c.heightDiffCells;
            }
        }
        c.walkableSame = map.walkable == copy.walkable;
        c.grassSame = map.grassMask == copy.grassMask;
        c.recordsSame = map.boxes.size() == copy.boxes.size() && map.hulls.size() == copy.hulls.size()
            && map.visuals.size() == copy.visuals.size() && map.blocks.size() == copy.blocks.size()
            && detail::RecordSum(map) == detail::RecordSum(copy);
        c.zonesSame = detail::ZonesMatch(map);
        c.rawSame = detail::RawMatches(map);
        return c;
    }
}
