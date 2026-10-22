// ============================================================
// GameProgress.cpp
// ============================================================
#include "Core/GameProgress.h"
#include "ResourcePaths.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
    bool MapOk(int map) { return map >= 1 && map <= GameProgress::kMaps; }
    bool TierOk(int tier) { return tier >= 1 && tier <= GameProgress::kTiers; }
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
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(Res::Cfg::Progress).parent_path(), ec);
    std::ofstream out(Res::Cfg::Progress);
    if (!out.is_open()) return false;
    out << j.dump(4);
    return true;
}

bool GameProgress::Load()
{
    std::ifstream in(Res::Cfg::Progress);
    if (!in.is_open()) return false;   // 無ければ最初から（草原 Tier 1 だけ）
    try
    {
        json j;
        in >> j;
        if (j.contains("mapUnlocked") && j["mapUnlocked"].is_array())
            for (int m = 0; m < kMaps && m < (int)j["mapUnlocked"].size(); ++m)
                mapUnlocked[m] = j["mapUnlocked"][m].get<bool>();
        if (j.contains("tierCleared") && j["tierCleared"].is_array())
            for (int m = 0; m < kMaps && m < (int)j["tierCleared"].size(); ++m)
                for (int t = 0; t < kTiers && t < (int)j["tierCleared"][m].size(); ++t)
                    tierCleared[m][t] = j["tierCleared"][m][t].get<bool>();
        lastMap = std::clamp(j.value("lastMap", lastMap), 1, kMaps);
        lastTier = std::clamp(j.value("lastTier", lastTier), 1, kTiers);
        mapUnlocked[0] = true;   // 草原はいつでも
    }
    catch (const std::exception& e)
    {
        std::cout << "[Progress] parse error: " << e.what() << std::endl;
        return false;
    }
    return true;
}
