// ============================================================
// GameProgress.cpp
// ============================================================
#include "Core/GameProgress.h"
#include "ResourcePaths.h"

#include <nlohmann/json.hpp>
#include <Windows.h>

using json = nlohmann::json;

namespace
{
    bool MapOk(int map) { return map >= 1 && map <= GameProgress::kMaps; }
    bool TierOk(int tier) { return tier >= 1 && tier <= GameProgress::kTiers; }

    // 書きかけで落ちても前のファイルが残るよう、<パス>.tmp に書き切ってから置き換える
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

GameProgress& GameProgress::Get()
{
    static GameProgress s;
    return s;
}

bool GameProgress::IsMapUnlocked(int map) const
{
    return MapOk(map) && mapUnlocked[map - 1];
}

bool GameProgress::IsTierCleared(int map, int tier) const
{
    return MapOk(map) && TierOk(tier) && tierCleared[map - 1][tier - 1];
}

bool GameProgress::IsTierOpen(int map, int tier) const
{
    if (!IsMapUnlocked(map) || !TierOk(tier) || tier > MaxTier(map)) return false;
    return tier == 1 || tierCleared[map - 1][tier - 2];
}

bool GameProgress::OnStageEntered(int stage)
{
    if (!MapOk(stage) || mapUnlocked[stage - 1]) return false;
    mapUnlocked[stage - 1] = true;
    std::cout << "[Progress] map " << stage << " unlocked" << std::endl;
    return true;
}

bool GameProgress::OnTierCleared(int map, int tier)
{
    if (!MapOk(map) || !TierOk(tier)) return false;
    mapUnlocked[map - 1] = true;   // 念のため（デバッグで VFXL_STAGE から入った時など）
    if (tierCleared[map - 1][tier - 1]) return false;
    tierCleared[map - 1][tier - 1] = true;
    std::cout << "[Progress] map " << map << " tier " << tier << " cleared" << std::endl;
    return tier < MaxTier(map);   // 次の Tier が開いた
}

void GameProgress::UnlockAll()
{
    for (int m = 0; m < kMaps; ++m)
    {
        mapUnlocked[m] = true;
        for (int t = 0; t < kTiers; ++t) tierCleared[m][t] = true;
    }
}

void GameProgress::ResetAll()
{
    *this = GameProgress{};
}

bool GameProgress::Save() const
{
    json j;
    j["mapUnlocked"] = json::array();
    j["tierCleared"] = json::array();
    for (int m = 0; m < kMaps; ++m)
    {
        j["mapUnlocked"].push_back(mapUnlocked[m]);
        json tiers = json::array();
        for (int t = 0; t < kTiers; ++t) tiers.push_back(tierCleared[m][t]);
        j["tierCleared"].push_back(tiers);
    }
    j["lastMap"] = lastMap;
    j["lastTier"] = lastTier;
    const std::filesystem::path path(Res::Cfg::Progress);
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return WriteFileReplacing(path, j.dump(4));
}

bool GameProgress::Load()
{
    std::ifstream in(Res::Cfg::Progress);
    if (!in.is_open()) return false;   // 無ければ最初から（草原 Tier 1 だけ）
    // 写しへ読んでから一度に入れ替える（途中で型が違って例外になっても、半分だけ書き換わった状態を残さない）
    GameProgress p = *this;
    try
    {
        json j;
        in >> j;
        if (j.contains("mapUnlocked") && j["mapUnlocked"].is_array())
            for (int m = 0; m < kMaps && m < (int)j["mapUnlocked"].size(); ++m)
                p.mapUnlocked[m] = j["mapUnlocked"][m].get<bool>();
        if (j.contains("tierCleared") && j["tierCleared"].is_array())
            for (int m = 0; m < kMaps && m < (int)j["tierCleared"].size(); ++m)
                for (int t = 0; t < kTiers && t < (int)j["tierCleared"][m].size(); ++t)
                    p.tierCleared[m][t] = j["tierCleared"][m][t].get<bool>();
        p.lastMap = std::clamp(j.value("lastMap", p.lastMap), 1, kMaps);
        p.lastTier = std::clamp(j.value("lastTier", p.lastTier), 1, kTiers);
    }
    catch (const std::exception& e)
    {
        std::cout << "[Progress] parse error: " << e.what() << std::endl;
        mapUnlocked[0] = true;   // 読めなくても草原だけは選べる
        return false;
    }
    *this = p;
    mapUnlocked[0] = true;   // 草原はいつでも
    return true;
}
