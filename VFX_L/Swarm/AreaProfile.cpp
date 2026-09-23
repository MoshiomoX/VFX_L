// ============================================================
// AreaProfile.cpp
// ============================================================
#include "Swarm/AreaProfile.h"
#include "Swarm/SwarmVFXTable.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace DirectX::SimpleMath;
using json = nlohmann::json;

namespace
{
    std::vector<AreaProfile> g_Areas;   // [0] = 無し

    void EnsureBuiltin()
    {
        if (!g_Areas.empty()) return;
        AreaProfile none;
        none.name = "None";
        none.damage = 0.0f;
        g_Areas.push_back(none);
    }

    // 区切りと大小文字を無視してファイル名の部分だけ比べる
    std::string FileKey(const std::string& path)
    {
        std::string s = path;
        const size_t slash = s.find_last_of("/\\");
        if (slash != std::string::npos) s = s.substr(slash + 1);
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }
}

// ============================================================
// AreaProfile
// ============================================================
uint32_t AreaProfile::Flags(bool atCaster) const
{
    uint32_t f = 0;
    if (atCaster && followCaster) f |= Swarm::kAreaFollowPlayer;
    if (stun) f |= Swarm::kAreaStun;
    return f;
}

Swarm::Area AreaProfile::MakeArea(const Vector3& center, bool atCaster) const
{
    Swarm::Area a;
    a.center = center;
    a.radius = radius;
    a.damage = damage;
    a.timeLeft = duration;
    a.tickInterval = EffectiveTickInterval();
    a.tickTimer = 0.0f;
    a.halfHeight = halfHeight;
    a.flags = Flags(atCaster);
    a.vfxType = 0;   // CPU から出す範囲の見た目は CPU が再生する
    return a;
}

json AreaProfile::ToJson() const
{
    json j;
    j["name"] = name;
    j["kind"] = static_cast<int>(kind);
    j["radius"] = radius;
    j["halfHeight"] = halfHeight;
    j["damage"] = damage;
    j["duration"] = duration;
    j["tickInterval"] = tickInterval;
    j["followCaster"] = followCaster;
    j["stun"] = stun;
    j["vfx"] = vfxFile;

    json pv;
    pv["atTarget"] = previewAtTarget;
    pv["interval"] = previewInterval;
    j["preview"] = pv;
    return j;
}

void AreaProfile::FromJson(const json& j)
{
    name = j.value("name", name);
    const int k = j.value("kind", 0);
    kind = (k == 1) ? Kind::Lasting : Kind::OneShot;
    radius = j.value("radius", radius);
    halfHeight = j.value("halfHeight", halfHeight);
    damage = j.value("damage", damage);
    duration = j.value("duration", duration);
    tickInterval = j.value("tickInterval", tickInterval);
    followCaster = j.value("followCaster", followCaster);
    stun = j.value("stun", stun);
    vfxFile = j.value("vfx", vfxFile);

    if (j.contains("preview"))
    {
        const json& pv = j["preview"];
        previewAtTarget = pv.value("atTarget", previewAtTarget);
        previewInterval = pv.value("interval", previewInterval);
    }
}

// ============================================================
// AreaProfileDB
// ============================================================
void AreaProfileDB::LoadAll()
{
    g_Areas.clear();
    EnsureBuiltin();

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kDir, ec)) return;

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(kDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());   // 番号を起動ごとに変えない

    for (const auto& path : files)
    {
        if (g_Areas.size() >= Swarm::kMaxAreaDefs) break;

        std::ifstream in(path);
        if (!in.is_open()) continue;

        json j;
        try { in >> j; }
        catch (const json::exception& e)
        {
            std::cout << "[AreaProfile] parse error: " << path.string() << " : " << e.what() << std::endl;
            continue;
        }

        AreaProfile p;
        p.name = path.stem().string();
        p.FromJson(j);
        g_Areas.push_back(p);
    }

    std::cout << "[AreaProfile] loaded " << (g_Areas.size() - 1) << " profile(s) from " << kDir << std::endl;
}

int AreaProfileDB::Count()
{
    EnsureBuiltin();
    return (int)g_Areas.size();
}

AreaProfile& AreaProfileDB::At(int index)
{
    EnsureBuiltin();
    if (index < 0 || index >= (int)g_Areas.size()) index = 0;
    return g_Areas[index];
}

int AreaProfileDB::IndexOf(const std::string& name)
{
    EnsureBuiltin();
    if (name.empty()) return 0;
    for (int i = 1; i < (int)g_Areas.size(); ++i)
        if (g_Areas[i].name == name) return i;
    return 0;
}

int AreaProfileDB::Add(const AreaProfile& p)
{
    EnsureBuiltin();
    if (g_Areas.size() >= Swarm::kMaxAreaDefs) return -1;
    g_Areas.push_back(p);
    return (int)g_Areas.size() - 1;
}

bool AreaProfileDB::Save(int index)
{
    EnsureBuiltin();
    if (index <= 0 || index >= (int)g_Areas.size()) return false;

    const AreaProfile& p = g_Areas[index];
    if (p.name.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);

    const std::string path = std::string(kDir) + p.name + ".json";
    std::ofstream out(path);
    if (!out.is_open())
    {
        std::cout << "[AreaProfile] save failed: " << path << std::endl;
        return false;
    }
    out << p.ToJson().dump(4);
    std::cout << "[AreaProfile] saved: " << path << std::endl;
    return true;
}

VFXId AreaProfileDB::FindVFXId(const std::string& vfxFile)
{
    if (vfxFile.empty()) return VFXId::None;
    const std::string key = FileKey(vfxFile);
    for (int i = 0; i < VFXDatabase::Count(); ++i)
    {
        const VFXId id = VFXDatabase::At(i);
        const char* path = VFXDatabase::GetPath(id);
        if (path && FileKey(path) == key) return id;
    }
    return VFXId::None;
}

std::vector<Swarm::AreaDef> AreaProfileDB::BuildDefs(const SwarmVFXTable& vfxTable)
{
    EnsureBuiltin();
    std::vector<Swarm::AreaDef> out;
    out.reserve(g_Areas.size());

    for (const auto& p : g_Areas)
    {
        Swarm::AreaDef d;
        d.radius = p.radius;
        d.halfHeight = p.halfHeight;
        d.damage = p.damage;
        d.duration = p.duration;
        d.tickInterval = p.EffectiveTickInterval();
        d.flags = p.Flags(false);   // 命中で出る範囲は玩家に付いて動かない
        d.vfxType = vfxTable.IndexOf(FindVFXId(p.vfxFile));
        out.push_back(d);
    }
    return out;
}
