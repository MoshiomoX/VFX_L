// ============================================================
// ItemDataFile.cpp
// ============================================================
#include "Item/ItemDataFile.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    std::vector<CellOffset> CellsFromJson(const json& a)
    {
        std::vector<CellOffset> out;
        if (!a.is_array()) return out;
        for (const auto& c : a)
            if (c.is_array() && c.size() >= 2 && c[0].is_number_integer() && c[1].is_number_integer())
                out.push_back({ c[0].get<int>(), c[1].get<int>() });
        return out;
    }

    json CellsToJson(const std::vector<CellOffset>& cells)
    {
        json a = json::array();
        for (const auto& c : cells)
            a.push_back(json::array({ c.row, c.col }));
        return a;
    }
}

std::string ItemDataFile::PathOf(const char* itemName)
{
    std::string key;
    for (const char* p = itemName ? itemName : ""; *p; ++p)
        if (*p != ' ') key += *p;
    if (key.empty()) key = "Unnamed";
    return std::string(kDir) + key + ".json";
}

bool ItemDataFile::Exists(const char* itemName)
{
    std::error_code ec;
    return std::filesystem::exists(PathOf(itemName), ec);
}

bool ItemDataFile::LoadShape(const char* itemName,
    std::vector<CellOffset>& occupy, std::vector<CellOffset>& influence)
{
    const std::string path = PathOf(itemName);
    std::ifstream in(path);
    if (!in.is_open()) return false;

    json j;
    try { in >> j; }
    catch (const json::exception& e)
    {
        std::cout << "[ItemData] parse error: " << path << " : " << e.what() << std::endl;
        return false;
    }

    std::vector<CellOffset> occ = CellsFromJson(j.value("occupy", json::array()));
    if (occ.empty())
    {
        std::cout << "[ItemData] no occupy cell, ignored: " << path << std::endl;
        return false;
    }

    occupy = std::move(occ);
    influence = CellsFromJson(j.value("influence", json::array()));
    return true;
}

// ============================================================
// 保存
// マス 1 個が 4 行に割れると読めないので、配列は 1 行で書く
// ============================================================
bool ItemDataFile::SaveShape(const char* itemName,
    const std::vector<CellOffset>& occupy, const std::vector<CellOffset>& influence)
{
    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);

    const std::string path = PathOf(itemName);
    std::ofstream out(path);
    if (!out.is_open())
    {
        std::cout << "[ItemData] save failed: " << path << std::endl;
        return false;
    }

    out << "{\n"
        << "    \"name\": " << json(itemName ? itemName : "").dump() << ",\n"
        << "    \"occupy\": " << CellsToJson(occupy).dump() << ",\n"
        << "    \"influence\": " << CellsToJson(influence).dump() << "\n"
        << "}\n";

    std::cout << "[ItemData] saved: " << path << std::endl;
    return true;
}
