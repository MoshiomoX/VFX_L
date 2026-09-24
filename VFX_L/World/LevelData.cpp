// ============================================================
// LevelData.cpp
// ============================================================
#include "World/LevelData.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

using json = nlohmann::json;
using DirectX::SimpleMath::Vector3;
namespace fs = std::filesystem;

namespace
{
    json ToJson(const Vector3& v) { return json::array({ v.x, v.y, v.z }); }

    Vector3 Vec3From(const json& j, const Vector3& fallback)
    {
        if (!j.is_array() || j.size() != 3) return fallback;
        return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
    }
}

bool LevelIO::Save(const LevelData& level)
{
    if (level.name.empty()) return false;

    std::error_code ec;
    fs::create_directories(kDir, ec);

    json objs = json::array();
    for (const auto& o : level.objects)
    {
        objs.push_back({
            { "model", o.model },
            { "pos",   ToJson(o.position) },
            { "rot",   ToJson(o.rotation) },
            { "scale", o.scale },
        });
    }
    const json root = { { "name", level.name }, { "objects", objs } };

    const std::string path = std::string(kDir) + level.name + ".json";
    std::ofstream out(path);
    if (!out.is_open())
    {
        std::cout << "[LevelIO] save failed: " << path << std::endl;
        return false;
    }
    out << root.dump(2);
    std::cout << "[LevelIO] saved: " << path << " (" << level.objects.size() << " objects)" << std::endl;
    return true;
}

bool LevelIO::Load(const std::string& name, LevelData& out)
{
    const std::string path = std::string(kDir) + name + ".json";
    std::ifstream in(path);
    if (!in.is_open())
    {
        std::cout << "[LevelIO] not found: " << path << std::endl;
        return false;
    }

    json root;
    try { in >> root; }
    catch (const json::exception& e)
    {
        std::cout << "[LevelIO] parse error: " << path << " : " << e.what() << std::endl;
        return false;
    }

    LevelData level;
    level.name = root.value("name", name);
    if (root.contains("objects") && root["objects"].is_array())
    {
        for (const auto& j : root["objects"])
        {
            LevelObject o;
            o.model = j.value("model", std::string());
            if (o.model.empty()) continue;   // モデル無しは置けない
            if (j.contains("pos")) o.position = Vec3From(j["pos"], o.position);
            if (j.contains("rot")) o.rotation = Vec3From(j["rot"], o.rotation);
            o.scale = j.value("scale", 1.0f);
            level.objects.push_back(std::move(o));
        }
    }

    out = std::move(level);
    std::cout << "[LevelIO] loaded: " << path << " (" << out.objects.size() << " objects)" << std::endl;
    return true;
}

std::vector<std::string> LevelIO::List()
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(kDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json")
            names.push_back(e.path().stem().string());
    std::sort(names.begin(), names.end());
    return names;
}
