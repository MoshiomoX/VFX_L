// ============================================================
// GameSettings.cpp
// ============================================================
#include "Core/GameSettings.h"
#include "ResourcePaths.h"

#include <nlohmann/json.hpp>
#include <Windows.h>

using json = nlohmann::json;

namespace
{
    // 書きかけで落ちても前のファイルが残るよう、<パス>.tmp に書き切ってから置き換える（GameProgress.cpp と同じ）
    bool WriteFileReplacing(const std::filesystem::path& path, const std::string& text)
    {
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream out(tmp);
            if (!out.is_open()) return false;
            out << text;
            out.flush();
            if (!out) return false;
        }
        if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }
}

GameSettings& GameSettings::Get()
{
    static GameSettings s;
    return s;
}

bool GameSettings::Save() const
{
    json j;
    j["factionOutline"] = factionOutline;
    j["damageNumbers"] = damageNumbers;
    j["showFps"] = showFps;
    j["tutorialDone"] = tutorialDone;
    const std::filesystem::path path(Res::Cfg::Settings);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return WriteFileReplacing(path, j.dump(4));
}

bool GameSettings::Load()
{
    std::ifstream in(Res::Cfg::Settings);
    if (!in.is_open()) return false;   // 無ければコードの既定値
    // 写しへ読んでから一度に入れ替える（途中で型が違って例外になっても、半分だけ書き換わった状態を残さない）
    GameSettings s = *this;
    try
    {
        json j;
        in >> j;
        // 10-07 版のファイルは "outline"（アウトライン全体の ON / OFF）。切っていた人は敵味方の枠を切りたかったはずなので引き継ぐ
        s.factionOutline = j.value("outline", s.factionOutline);
        s.factionOutline = j.value("factionOutline", s.factionOutline);
        s.damageNumbers = j.value("damageNumbers", s.damageNumbers);
        s.showFps = j.value("showFps", s.showFps);
        s.tutorialDone = j.value("tutorialDone", s.tutorialDone);
    }
    catch (const std::exception& e)
    {
        std::cout << "[Settings] parse error: " << e.what() << std::endl;
        return false;
    }
    *this = s;
    return true;
}
