// ============================================================
// GameSettings.cpp
// ============================================================
#include "Core/GameSettings.h"
#include "ResourcePaths.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

GameSettings& GameSettings::Get()
{
    static GameSettings s;
    return s;
}

bool GameSettings::Save() const
{
    json j;
    j["factionOutline"] = factionOutline;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(Res::Cfg::Settings).parent_path(), ec);
    std::ofstream out(Res::Cfg::Settings);
    if (!out.is_open()) return false;
    out << j.dump(4);
    return true;
}

bool GameSettings::Load()
{
    std::ifstream in(Res::Cfg::Settings);
    if (!in.is_open()) return false;   // 無ければコードの既定値
    try
    {
        json j;
        in >> j;
        // 10-07 版のファイルは "outline"（アウトライン全体の ON / OFF）。切っていた人は敵味方の枠を切りたかったはずなので引き継ぐ
        factionOutline = j.value("outline", factionOutline);
        factionOutline = j.value("factionOutline", factionOutline);
    }
    catch (const std::exception& e)
    {
        std::cout << "[Settings] parse error: " << e.what() << std::endl;
        return false;
    }
    return true;
}
