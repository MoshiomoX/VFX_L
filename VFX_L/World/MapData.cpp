// ============================================================
// MapData.cpp
// 地図のデータの保存・読み込み（小端の素のバイナリ）
// ============================================================
#include "World/MapData.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace
{
    constexpr uint32_t kMagic = 0x50414D56u;   // "VMAP"
    constexpr const char* kDir = "Assets/Data/MapData";

    // ---- 書き込み ----
    struct Writer
    {
        std::vector<uint8_t>& out;

        template <class T> void Pod(const T& v)
        {
            static_assert(std::is_trivially_copyable_v<T>, "POD だけ");
            const auto* p = reinterpret_cast<const uint8_t*>(&v);
            out.insert(out.end(), p, p + sizeof(T));
        }
        // 詰め物の無い POD の配列（uint8_t / float / int / Vector3）
        template <class T> void Array(const std::vector<T>& v)
        {
            static_assert(std::is_trivially_copyable_v<T>, "POD だけ");
            Pod((uint32_t)v.size());
            if (v.empty()) return;
            const auto* p = reinterpret_cast<const uint8_t*>(v.data());
            out.insert(out.end(), p, p + sizeof(T) * v.size());
        }
        void String(const std::string& s)
        {
            Pod((uint32_t)s.size());
            out.insert(out.end(), s.begin(), s.end());
        }
        void Tag(const MapData::Tag& t) { Pod(t.kind); Pod(t.group); }
    };

    // ---- 読み込み（足りなければ ok = false にして 0 を返す）----
    struct Reader
    {
        const std::vector<uint8_t>& in;
        size_t pos = 0;
        bool ok = true;

        bool Take(void* dst, size_t n)
        {
            if (!ok || pos + n > in.size()) { ok = false; std::memset(dst, 0, n); return false; }
            std::memcpy(dst, in.data() + pos, n);
            pos += n;
            return true;
        }
        template <class T> void Pod(T& v) { Take(&v, sizeof(T)); }
        template <class T> void Array(std::vector<T>& v)
        {
            uint32_t n = 0;
            Pod(n);
            if (!ok || pos + (size_t)n * sizeof(T) > in.size()) { ok = false; v.clear(); return; }
            v.resize(n);
            if (n > 0) Take(v.data(), (size_t)n * sizeof(T));
        }
        void String(std::string& s)
        {
            uint32_t n = 0;
            Pod(n);
            if (!ok || pos + n > in.size()) { ok = false; s.clear(); return; }
            s.assign(reinterpret_cast<const char*>(in.data() + pos), n);
            pos += n;
        }
        void Tag(MapData::Tag& t) { Pod(t.kind); Pod(t.group); }
        // 個数（1 個が最低 minBytes）。残りより多ければ壊れている
        uint32_t Count(size_t minBytes)
        {
            uint32_t n = 0;
            Pod(n);
            if (ok && (size_t)n * minBytes > in.size() - pos) ok = false;
            return ok ? n : 0;
        }
    };
}

namespace MapData
{
    int Map::ModelIndex(const std::string& path)
    {
        for (size_t i = 0; i < models.size(); ++i)
            if (models[i] == path) return (int)i;
        models.push_back(path);
        return (int)models.size() - 1;
    }

    // 構造体は項目毎に書く（詰め物のバイトをファイルへ入れない = 同じ地図は必ず同じバイト列になる）
    void Serialize(const Map& m, std::vector<uint8_t>& out)
    {
        out.clear();
        Writer w{ out };
        w.Pod(kMagic);
        w.Pod(kVersion);

        w.Pod(m.seed); w.Pod((int32_t)m.biome); w.Pod((int32_t)m.gw); w.Pod((int32_t)m.gd);
        w.Pod((uint8_t)m.relief); w.Pod((int32_t)m.reliefSubdiv); w.Pod((uint8_t)m.cave);
        w.Pod(m.summitH); w.Pod(m.mineD); w.Pod(m.roofTopY);

        w.Array(m.zone); w.Array(m.caveRing);
        w.Array(m.reliefPlain); w.Array(m.reliefSummit);
        w.Array(m.walkable); w.Array(m.heights); w.Array(m.grassMask);

        w.Pod((uint32_t)m.models.size());
        for (const auto& s : m.models) w.String(s);

        w.Pod((uint32_t)m.boxes.size());
        for (const auto& b : m.boxes) { w.Tag(b.tag); w.Pod(b.lo); w.Pod(b.hi); w.Pod(b.layer); }
        w.Pod((uint32_t)m.hulls.size());
        for (const auto& h : m.hulls) { w.Tag(h.tag); for (const auto& v : h.v) w.Pod(v); w.Pod(h.layer); }
        w.Pod((uint32_t)m.visuals.size());
        for (const auto& v : m.visuals)
        {
            w.Tag(v.tag);
            for (const auto& p : v.v) w.Pod(p);
            w.Pod(v.top); w.Pod(v.side); w.Pod((int32_t)v.topLayer); w.Pod((int32_t)v.sideLayer);
        }
        w.Pod((uint32_t)m.props.size());
        for (const auto& p : m.props) { w.Tag(p.tag); w.Pod((int32_t)p.model); w.Pod(p.pos); w.Pod(p.yawDeg); w.Pod(p.scale); }
        w.Pod((uint32_t)m.blocks.size());
        for (const auto& b : m.blocks) { w.Tag(b.tag); w.Pod((int32_t)b.x); w.Pod((int32_t)b.z); w.Pod((int32_t)b.w); w.Pod((int32_t)b.d); }
        w.Array(m.torches);

        w.Array(m.summitCells); w.Array(m.mineCells);
        w.Pod((uint8_t)m.hasMineDeep); w.Pod(m.mineDeep);
        w.Pod((uint32_t)m.summitRamps.size());
        for (const auto& r : m.summitRamps) { w.Pod(r.top); w.Pod(r.down); }
        w.Pod((uint32_t)m.mineRamps.size());
        for (const auto& r : m.mineRamps) { w.Pod(r.top); w.Pod(r.down); }
        w.Pod(m.nextGroup);

        // 版 2 から
        w.Pod((uint32_t)m.placements.size());
        for (const auto& p : m.placements) { w.Pod(p.type); w.Pod(p.pos); w.Pod(p.yawDeg); }

        // 版 3 から
        w.Pod((uint32_t)m.volumes.size());
        for (const auto& v : m.volumes) { w.Tag(v.tag); w.Pod(v.center); w.Pod(v.half); w.Pod((uint8_t)v.solid); w.Pod((uint8_t)v.blockMobs); }

        // 版 4 から
        w.Array(m.rawPlain); w.Array(m.rawSummit);
        w.Pod((uint32_t)m.blockParts.size());
        for (const auto& p : m.blockParts)
        {
            w.Tag(p.tag); w.Pod((int32_t)p.x); w.Pod((int32_t)p.z); w.Pod((int32_t)p.w); w.Pod((int32_t)p.d);
            w.Pod(p.bottom); w.Pod(p.top); w.Pod(p.base); w.Pod(p.topColor); w.Pod(p.sideColor);
            w.Pod((uint8_t)p.raise); w.Pod((uint8_t)p.onGround);
        }
        w.Pod((uint32_t)m.rampParts.size());
        for (const auto& p : m.rampParts)
        {
            w.Tag(p.tag); w.Pod((int32_t)p.x); w.Pod((int32_t)p.z); w.Pod((int32_t)p.w); w.Pod((int32_t)p.d);
            w.Pod(p.side); w.Pod(p.base); w.Pod(p.top); w.Pod(p.topColor); w.Pod(p.sideColor);
            w.Pod((uint8_t)p.grassy); w.Pod((uint8_t)p.onGround); w.Pod((int32_t)p.owner); w.Pod((int32_t)p.offset);
        }
        w.Pod((uint32_t)m.pads.size());
        for (const auto& p : m.pads)
        {
            w.Tag(p.tag); w.Pod(p.zone); w.Pod((int32_t)p.ax0); w.Pod((int32_t)p.ax1); w.Pod((int32_t)p.az0);
            w.Pod((int32_t)p.az1); w.Pod((int32_t)p.m); w.Pod(p.L);
        }
        w.Pod((int32_t)m.padMargin); w.Pod(m.reliefMaxSlopeDeg); w.Pod(m.rampSlopeDeg);
    }

    bool Deserialize(const std::vector<uint8_t>& in, Map& m)
    {
        m.Clear();
        Reader r{ in };
        uint32_t magic = 0, version = 0;
        r.Pod(magic); r.Pod(version);
        if (!r.ok || magic != kMagic || version < 1 || version > kVersion) return false;

        int32_t i32 = 0;
        uint8_t u8 = 0;
        r.Pod(m.seed); r.Pod(i32); m.biome = i32; r.Pod(i32); m.gw = i32; r.Pod(i32); m.gd = i32;
        r.Pod(u8); m.relief = u8 != 0; r.Pod(i32); m.reliefSubdiv = i32; r.Pod(u8); m.cave = u8 != 0;
        r.Pod(m.summitH); r.Pod(m.mineD); r.Pod(m.roofTopY);

        r.Array(m.zone); r.Array(m.caveRing);
        r.Array(m.reliefPlain); r.Array(m.reliefSummit);
        r.Array(m.walkable); r.Array(m.heights); r.Array(m.grassMask);

        m.models.resize(r.Count(4));
        for (auto& s : m.models) r.String(s);

        m.boxes.resize(r.Count(34));
        for (auto& b : m.boxes) { r.Tag(b.tag); r.Pod(b.lo); r.Pod(b.hi); r.Pod(b.layer); }
        m.hulls.resize(r.Count(106));
        for (auto& h : m.hulls) { r.Tag(h.tag); for (auto& v : h.v) r.Pod(v); r.Pod(h.layer); }
        m.visuals.resize(r.Count(142));
        for (auto& v : m.visuals)
        {
            r.Tag(v.tag);
            for (auto& p : v.v) r.Pod(p);
            r.Pod(v.top); r.Pod(v.side); r.Pod(i32); v.topLayer = i32; r.Pod(i32); v.sideLayer = i32;
        }
        m.props.resize(r.Count(30));
        for (auto& p : m.props) { r.Tag(p.tag); r.Pod(i32); p.model = i32; r.Pod(p.pos); r.Pod(p.yawDeg); r.Pod(p.scale); }
        m.blocks.resize(r.Count(22));
        for (auto& b : m.blocks)
        {
            r.Tag(b.tag);
            r.Pod(i32); b.x = i32; r.Pod(i32); b.z = i32; r.Pod(i32); b.w = i32; r.Pod(i32); b.d = i32;
        }
        r.Array(m.torches);

        r.Array(m.summitCells); r.Array(m.mineCells);
        r.Pod(u8); m.hasMineDeep = u8 != 0; r.Pod(m.mineDeep);
        m.summitRamps.resize(r.Count(24));
        for (auto& rp : m.summitRamps) { r.Pod(rp.top); r.Pod(rp.down); }
        m.mineRamps.resize(r.Count(24));
        for (auto& rp : m.mineRamps) { r.Pod(rp.top); r.Pod(rp.down); }
        r.Pod(m.nextGroup);
        if (version >= 2)
        {
            m.placements.resize(r.Count(18));
            for (auto& p : m.placements) { r.Pod(p.type); r.Pod(p.pos); r.Pod(p.yawDeg); }
        }
        if (version >= 3)
        {
            m.volumes.resize(r.Count(32));
            for (auto& v : m.volumes) { r.Tag(v.tag); r.Pod(v.center); r.Pod(v.half); r.Pod(u8); v.solid = u8 != 0; r.Pod(u8); v.blockMobs = u8 != 0; }
        }
        if (version >= 4)
        {
            r.Array(m.rawPlain); r.Array(m.rawSummit);
            m.blockParts.resize(r.Count(64));
            for (auto& p : m.blockParts)
            {
                r.Tag(p.tag); r.Pod(i32); p.x = i32; r.Pod(i32); p.z = i32; r.Pod(i32); p.w = i32; r.Pod(i32); p.d = i32;
                r.Pod(p.bottom); r.Pod(p.top); r.Pod(p.base); r.Pod(p.topColor); r.Pod(p.sideColor);
                r.Pod(u8); p.raise = u8 != 0; r.Pod(u8); p.onGround = u8 != 0;
            }
            m.rampParts.resize(r.Count(73));
            for (auto& p : m.rampParts)
            {
                r.Tag(p.tag); r.Pod(i32); p.x = i32; r.Pod(i32); p.z = i32; r.Pod(i32); p.w = i32; r.Pod(i32); p.d = i32;
                r.Pod(p.side); r.Pod(p.base); r.Pod(p.top); r.Pod(p.topColor); r.Pod(p.sideColor);
                r.Pod(u8); p.grassy = u8 != 0; r.Pod(u8); p.onGround = u8 != 0; r.Pod(i32); p.owner = i32; r.Pod(i32); p.offset = i32;
            }
            m.pads.resize(r.Count(31));
            for (auto& p : m.pads)
            {
                r.Tag(p.tag); r.Pod(p.zone); r.Pod(i32); p.ax0 = i32; r.Pod(i32); p.ax1 = i32; r.Pod(i32); p.az0 = i32;
                r.Pod(i32); p.az1 = i32; r.Pod(i32); p.m = i32; r.Pod(p.L);
            }
            r.Pod(i32); m.padMargin = i32; r.Pod(m.reliefMaxSlopeDeg); r.Pod(m.rampSlopeDeg);
        }
        if (!r.ok) { m.Clear(); return false; }

        // 大きさの辻褄（壊れたファイルで配列の外を読まない）
        const size_t cells = (size_t)m.gw * m.gd;
        constexpr int kSub = 4;   // GridWorld::kHeightSub
        const size_t nodes = (size_t)(m.gw * kSub + 1) * (m.gd * kSub + 1);
        const bool sizesOk = m.gw > 0 && m.gd > 0
            && m.zone.size() == cells && m.caveRing.size() == cells && m.walkable.size() == cells
            && m.grassMask.size() == cells && m.heights.size() == cells * kSub * kSub
            && m.reliefPlain.size() == nodes && m.reliefSummit.size() == nodes;
        bool modelsOk = true;
        for (const auto& p : m.props) modelsOk = modelsOk && p.model >= 0 && p.model < (int)m.models.size();
        if (!sizesOk || !modelsOk) { m.Clear(); return false; }
        return true;
    }

    std::string PathFor(const std::string& name)
    {
        return std::string(kDir) + "/" + name + ".vmap";
    }

    bool Save(const std::string& name, const Map& map)
    {
        std::vector<uint8_t> bytes;
        Serialize(map, bytes);
        std::error_code ec;
        std::filesystem::create_directories(kDir, ec);
        std::ofstream f(PathFor(name), std::ios::binary | std::ios::trunc);
        if (!f) { std::cout << "[MapData] cannot write " << PathFor(name) << std::endl; return false; }
        f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
        std::cout << "[MapData] saved " << PathFor(name) << " (" << bytes.size() / 1024 << " KB)" << std::endl;
        return f.good();
    }

    std::vector<std::string> List()
    {
        std::vector<std::string> out;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(kDir, ec))
            if (e.is_regular_file() && e.path().extension() == ".vmap")
                out.push_back(e.path().stem().string());
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string& PlayOverride()
    {
        static std::string name;
        return name;
    }

    bool Load(const std::string& name, Map& map)
    {
        std::ifstream f(PathFor(name), std::ios::binary | std::ios::ate);
        if (!f) { std::cout << "[MapData] not found: " << PathFor(name) << std::endl; return false; }
        std::vector<uint8_t> bytes((size_t)f.tellg());
        f.seekg(0);
        f.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)bytes.size());
        if (!Deserialize(bytes, map))
        {
            std::cout << "[MapData] broken or old version: " << PathFor(name) << std::endl;
            return false;
        }
        return true;
    }
}
