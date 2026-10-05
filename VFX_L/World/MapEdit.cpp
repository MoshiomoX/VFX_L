// ============================================================
// MapEdit.cpp
// ============================================================
#include "World/MapEdit.h"
#include "World/GridWorld.h"
#include "Component/ColliderComponent.h"   // Layer_Prop
#include <algorithm>
#include <cmath>

namespace
{
    using DirectX::SimpleMath::Vector3;
    constexpr float kCs = GridWorld::kCellSize;
    constexpr int   kSub = GridWorld::kHeightSub;
    constexpr float kTorchReach = 1.6f;   // 松明の見た目と灯りの位置（outTorches）を同じ物とみなす水平距離

    float OriginX(const MapData::Map& m) { return -0.5f * m.gw * kCs; }
    float OriginZ(const MapData::Map& m) { return -0.5f * m.gd * kCs; }

    template <class T> void EraseGroup(std::vector<T>& v, uint32_t group)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [group](const T& r) { return r.tag.group == group; }), v.end());
    }

    // group の衝突（箱・凸体）の足跡から、塞ぐマスの記録を作り直す。
    // 1 マスより小さい衝突（木の幹）は真ん中のマスだけ。大きい物は縁から 0.15m 入った範囲に掛かるマス
    void RebuildBlocks(MapData::Map& map, uint32_t group)
    {
        MapData::Tag tag;
        bool found = false;
        auto add = [&](const MapData::Tag& t, Vector3 lo, Vector3 hi)
            {
                tag = t;
                found = true;
                const float ox = OriginX(map), oz = OriginZ(map);
                int x0, z0, x1, z1;
                if (hi.x - lo.x < kCs && hi.z - lo.z < kCs)
                {
                    x0 = x1 = (int)std::floor(((lo.x + hi.x) * 0.5f - ox) / kCs);
                    z0 = z1 = (int)std::floor(((lo.z + hi.z) * 0.5f - oz) / kCs);
                }
                else
                {
                    constexpr float kInset = 0.15f;
                    x0 = (int)std::floor((lo.x + kInset - ox) / kCs); x1 = (int)std::floor((hi.x - kInset - ox) / kCs);
                    z0 = (int)std::floor((lo.z + kInset - oz) / kCs); z1 = (int)std::floor((hi.z - kInset - oz) / kCs);
                }
                x0 = (std::max)(x0, 0); z0 = (std::max)(z0, 0);
                x1 = (std::min)(x1, map.gw - 1); z1 = (std::min)(z1, map.gd - 1);
                if (x1 < x0 || z1 < z0) return;
                MapData::Block b;
                b.tag = t; b.x = x0; b.z = z0; b.w = x1 - x0 + 1; b.d = z1 - z0 + 1;
                map.blocks.push_back(b);
            };

        EraseGroup(map.blocks, group);
        // 衝突を走査している間に blocks へ足す（boxes / hulls は触らない）
        for (size_t i = 0; i < map.boxes.size(); ++i)
            if (map.boxes[i].tag.group == group) add(map.boxes[i].tag, map.boxes[i].lo, map.boxes[i].hi);
        for (size_t i = 0; i < map.hulls.size(); ++i)
        {
            const auto& h = map.hulls[i];
            if (h.tag.group != group) continue;
            Vector3 lo = h.v[0], hi = h.v[0];
            for (int k = 1; k < 8; ++k) { lo = Vector3::Min(lo, h.v[k]); hi = Vector3::Max(hi, h.v[k]); }
            add(h.tag, lo, hi);
        }
        (void)tag; (void)found;
    }
}

namespace MapEdit
{
    float GroundHeight(const MapData::Map& map, float x, float z)
    {
        const int hw = map.gw * kSub, hd = map.gd * kSub;
        if (map.heights.size() != (size_t)hw * hd) return 0.0f;
        auto at = [&](int hx, int hz)
            {
                if (hx < 0 || hx >= hw || hz < 0 || hz >= hd) return 0.0f;
                return map.heights[(size_t)hz * hw + hx];
            };
        const float s = kCs / kSub;
        const float fx = (x - OriginX(map)) / s - 0.5f;
        const float fz = (z - OriginZ(map)) / s - 0.5f;
        const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
        const float tx = fx - ix, tz = fz - iz;
        return (at(ix, iz) * (1 - tx) + at(ix + 1, iz) * tx) * (1 - tz)
            + (at(ix, iz + 1) * (1 - tx) + at(ix + 1, iz + 1) * tx) * tz;
    }

    namespace
    {
        void BuildWalkable(const MapData::Map& map, std::vector<uint8_t>& out)
        {
            out.assign((size_t)map.gw * map.gd, 1);
            for (const auto& b : map.blocks)
                for (int z = (std::max)(b.z, 0); z < (std::min)(b.z + b.d, map.gd); ++z)
                    for (int x = (std::max)(b.x, 0); x < (std::min)(b.x + b.w, map.gw); ++x)
                        out[(size_t)z * map.gw + x] = 0;
        }
    }

    void RebuildWalkable(MapData::Map& map)
    {
        BuildWalkable(map, map.walkable);
    }

    bool WalkableMatchesBlocks(const MapData::Map& map)
    {
        std::vector<uint8_t> w;
        BuildWalkable(map, w);
        return w == map.walkable;
    }

    int FindProp(const MapData::Map& map, uint32_t group)
    {
        for (int i = 0; i < (int)map.props.size(); ++i)
            if (map.props[i].tag.group == group) return i;
        return -1;
    }

    bool IsSimpleProp(const MapData::Map& map, uint32_t group)
    {
        int props = 0, boxes = 0, hulls = 0, visuals = 0;
        uint16_t kind = MapData::kManual;
        for (const auto& p : map.props) if (p.tag.group == group) { ++props; kind = p.tag.kind; }
        for (const auto& b : map.boxes) if (b.tag.group == group) ++boxes;
        for (const auto& h : map.hulls) if (h.tag.group == group) ++hulls;
        for (const auto& v : map.visuals) if (v.tag.group == group) ++visuals;
        const bool kindOk = kind == MapData::kTree || kind == MapData::kRock || kind == MapData::kBush || kind == MapData::kManual;
        return props == 1 && boxes <= 1 && hulls == 0 && visuals == 0 && kindOk;
    }

    Collision CollisionOf(const MapData::Map& map, uint32_t group)
    {
        for (const auto& b : map.boxes)
        {
            if (b.tag.group != group) continue;
            // 幹の箱は 0.6m 角
            return (b.hi.x - b.lo.x < 0.7f && b.hi.z - b.lo.z < 0.7f) ? Collision::Trunk : Collision::Footprint;
        }
        return Collision::None;
    }

    void SetPropCollision(MapData::Map& map, uint32_t group, Collision mode, const Vector3& lo, const Vector3& hi)
    {
        const int pi = FindProp(map, group);
        if (pi < 0) return;
        const MapData::Prop p = map.props[(size_t)pi];

        EraseGroup(map.boxes, group);
        if (mode != Collision::None)
        {
            // TerrainGenerator の placeBlocker と同じ作り
            const float u = p.scale;
            const float bottom = p.pos.y + lo.y * u;
            const float height = (hi.y - lo.y) * u;
            MapData::Box b;
            b.tag = p.tag;
            b.layer = Layer_Prop;
            if (mode == Collision::Trunk)
            {
                const float trunk = (std::min)(height, 3.0f);
                b.lo = { p.pos.x - 0.3f, bottom, p.pos.z - 0.3f };
                b.hi = { p.pos.x + 0.3f, bottom + trunk, p.pos.z + 0.3f };
            }
            else
            {
                const float yaw = DirectX::XMConvertToRadians(p.yawDeg);
                const float cs = std::fabs(std::cos(yaw)), sn = std::fabs(std::sin(yaw));
                const float ex = (hi.x - lo.x) * 0.5f * u, ez = (hi.z - lo.z) * 0.5f * u;
                const float hx = (cs * ex + sn * ez) * 0.85f, hz = (sn * ex + cs * ez) * 0.85f;
                b.lo = { p.pos.x - hx, bottom, p.pos.z - hz };
                b.hi = { p.pos.x + hx, bottom + height, p.pos.z + hz };
            }
            map.boxes.push_back(b);
        }
        RebuildBlocks(map, group);
        RebuildWalkable(map);
    }

    void MoveGroup(MapData::Map& map, uint32_t group, const Vector3& delta)
    {
        for (auto& p : map.props)
        {
            if (p.tag.group != group) continue;
            if (p.tag.kind == MapData::kTorch)   // 灯りの位置も一緒に
                for (auto& t : map.torches)
                {
                    const float dx = t.x - p.pos.x, dz = t.z - p.pos.z;
                    if (dx * dx + dz * dz < kTorchReach * kTorchReach && std::fabs(t.y - p.pos.y) < 3.0f) t += delta;
                }
            p.pos += delta;
        }
        for (auto& b : map.boxes) if (b.tag.group == group) { b.lo += delta; b.hi += delta; }
        for (auto& h : map.hulls) if (h.tag.group == group) for (auto& v : h.v) v += delta;
        for (auto& v : map.visuals) if (v.tag.group == group) for (auto& q : v.v) q += delta;
        RebuildBlocks(map, group);
        RebuildWalkable(map);
    }

    void DeleteGroup(MapData::Map& map, uint32_t group)
    {
        for (const auto& p : map.props)
        {
            if (p.tag.group != group || p.tag.kind != MapData::kTorch) continue;
            map.torches.erase(std::remove_if(map.torches.begin(), map.torches.end(), [&](const Vector3& t)
                {
                    const float dx = t.x - p.pos.x, dz = t.z - p.pos.z;
                    return dx * dx + dz * dz < kTorchReach * kTorchReach && std::fabs(t.y - p.pos.y) < 3.0f;
                }), map.torches.end());
        }
        EraseGroup(map.props, group);
        EraseGroup(map.boxes, group);
        EraseGroup(map.hulls, group);
        EraseGroup(map.visuals, group);
        EraseGroup(map.blocks, group);
        EraseGroup(map.volumes, group);
        RebuildWalkable(map);
    }

    // ============================================================
    // 手で置いた見えない体積
    // ============================================================
    int FindVolume(const MapData::Map& map, uint32_t group)
    {
        for (int i = 0; i < (int)map.volumes.size(); ++i)
            if (map.volumes[i].tag.group == group) return i;
        return -1;
    }

    void ApplyVolume(MapData::Map& map, uint32_t group)
    {
        const int vi = FindVolume(map, group);
        if (vi < 0) return;
        const MapData::Volume v = map.volumes[(size_t)vi];
        const Vector3 lo = v.center - v.half, hi = v.center + v.half;

        EraseGroup(map.boxes, group);
        EraseGroup(map.blocks, group);
        if (v.solid)
        {
            MapData::Box b;
            b.tag = v.tag; b.lo = lo; b.hi = hi; b.layer = Layer_Prop;
            map.boxes.push_back(b);
        }
        if (v.blockMobs)
        {
            // 縁から 0.15m 入った範囲に掛かるマス（マスの端に少し触れただけでは塞がない）。1 マスより小さければ真ん中のマス
            constexpr float kInset = 0.15f;
            const float ox = OriginX(map), oz = OriginZ(map);
            int x0 = (int)std::floor((lo.x + kInset - ox) / kCs), x1 = (int)std::floor((hi.x - kInset - ox) / kCs);
            int z0 = (int)std::floor((lo.z + kInset - oz) / kCs), z1 = (int)std::floor((hi.z - kInset - oz) / kCs);
            if (x1 < x0) x0 = x1 = (int)std::floor((v.center.x - ox) / kCs);
            if (z1 < z0) z0 = z1 = (int)std::floor((v.center.z - oz) / kCs);
            x0 = (std::max)(x0, 0); z0 = (std::max)(z0, 0);
            x1 = (std::min)(x1, map.gw - 1); z1 = (std::min)(z1, map.gd - 1);
            if (x1 >= x0 && z1 >= z0)
            {
                MapData::Block b;
                b.tag = v.tag; b.x = x0; b.z = z0; b.w = x1 - x0 + 1; b.d = z1 - z0 + 1;
                map.blocks.push_back(b);
            }
        }
        RebuildWalkable(map);
    }

    uint32_t AddVolume(MapData::Map& map, const Vector3& center, const Vector3& half, bool solid, bool blockMobs)
    {
        MapData::Volume v;
        v.tag.kind = (uint16_t)MapData::kManual;
        v.tag.group = map.nextGroup++;
        v.center = center;
        v.half = half;
        v.solid = solid;
        v.blockMobs = blockMobs;
        map.volumes.push_back(v);
        ApplyVolume(map, v.tag.group);
        return v.tag.group;
    }

    uint32_t AddProp(MapData::Map& map, const std::string& model, const Vector3& pos, float yawDeg, float scale,
        MapData::Kind kind)
    {
        MapData::Prop p;
        p.tag.kind = (uint16_t)kind;
        p.tag.group = map.nextGroup++;
        p.model = map.ModelIndex(model);
        p.pos = pos;
        p.yawDeg = yawDeg;
        p.scale = scale;
        map.props.push_back(p);
        return p.tag.group;
    }
}
