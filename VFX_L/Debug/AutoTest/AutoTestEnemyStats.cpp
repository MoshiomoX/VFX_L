// ============================================================
// AutoTestEnemyStats.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=enemystats
// 敵の数値の書き出し（2026-10-30、ユーザー「マップ毎・時間帯毎の各敵の数値」）。
// 3 マップ × 何面目か（1〜3）× 経過時間（0〜9 分 + 最終ウェーブ 10:00 / 10:30 / 11:00 / 12:00）毎に、
// 湧く敵の種類毎の 割合・HP・速さ・接触ダメージ・技のダメージ・経験値 を、ゲームと同じ式（MobSpawner::ApplyStage・
// DifficultyCurve・KindRamp・BomberCB）で計算して exe の隣の enemystats.json へ書く。Tier 1〜3 それぞれ（ApplyStage に渡す）。
// 実際に敵は湧かせない。終わったら `enemystats done`
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "Enemy/Spawn/MobSpawner.h"
#include "Enemy/Spawn/StageDirector.h"
#include "Enemy/Elite/EliteAttacks.h"
#include "Enemy/Boss/BossBrain.h"
#include "World/StageConfig.h"

#include <nlohmann/json.hpp>
#include <Windows.h>

namespace
{
    using json = nlohmann::json;

    class AutoTestEnemyStats final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
    };

    std::string Utf8(const wchar_t* w)
    {
        if (!w || !*w) return std::string();
        const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s((size_t)(std::max)(0, n - 1), '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    float R1(float v) { return std::round(v * 10.0f) / 10.0f; }
    float R3(float v) { return std::round(v * 1000.0f) / 1000.0f; }

    // 種類の静的な情報（ダメージは倍率 1 の時。接触 < 0 = 殴らない）
    struct Special { const char* name; float damage; };
    struct KindDef
    {
        const char* key;
        const char* name;
        uint32_t kind;
        float contact;
        std::vector<Special> specials;
        float expMul;
        const char* note;
        const char* parent;   // 他の敵が出す物（湧きの割合を持たない）
    };
}

void AutoTestEnemyStats::Run()
{
    if (m_AutoStep != 0 || m_AutoTime < 1.0f) return;
    m_AutoStep = 1;
    m_Mobs.Director().enabled = false;

    const Swarm::BomberCB& b = m_Swarm.GetBomberParams();
    const float c = m_Mobs.baseContactDamage;   // 倍率 1 の接触（15）
    const float blast = m_Mobs.baseBlastDamage; // 倍率 1 の自爆兵の爆発（30）
    const float orbExp = Swarm::OrbCB{}.amount;  // 経験値オーブ 1 個（実行中に変える所は無い）
    namespace K = Swarm;

    // Request の抽選と同じ順（自爆兵 → … → 鎖の番人、残りが雑魚）
    const std::vector<KindDef> kinds = {
        { "mob",        "雑魚",         K::kEnemyKindMob,        c, {}, 1.0f, "", nullptr },
        { "bomber",     "自爆兵",       K::kEnemyKindBomber,     -1.0f, { { "爆発", blast } }, 1.0f, "触れると点火、1 秒後に爆発", nullptr },
        { "splitter",   "スプリッター", K::kEnemyKindSplitter,   c * b.splitterDamageMul, {}, b.splitterExpMul, "死ぬと分裂体 3 体", nullptr },
        { "brute",      "重装兵",       K::kEnemyKindBrute,      c * b.bruteDamageMul, {}, b.bruteExpMul, "", nullptr },
        { "charger",    "突撃兵",       K::kEnemyKindCharger,    c * b.chargerDamageMul, { { "突進", c * b.chargerDashDamageMul } }, b.chargerExpMul, "6〜12m で溜めて突進", nullptr },
        { "shield",     "盾兵",         K::kEnemyKindShield,     c * b.shieldDamageMul, {}, b.shieldExpMul, "1 発毎に 5 減らす装甲（最低 2 割は通る）", nullptr },
        { "spikeBall",  "トゲ玉",       K::kEnemyKindSpikeBall,  c * b.spikeBallDamageMul, {}, b.spikeBallExpMul, "当たると弾き飛ばす", nullptr },
        { "burrower",   "砂潜り",       K::kEnemyKindBurrower,   -1.0f, { { "噛みつき", c * b.burrowDamageMul } }, b.burrowExpMul, "潜っている間は弾が当たらない", nullptr },
        { "sandMage",   "砂嵐の術師",   K::kEnemyKindSandMage,   -1.0f, { { "砂旋", c * b.tornadoDamageMul } }, b.sandMageExpMul, "砂旋を放つ（吹き飛ばし）", nullptr },
        { "dashBomber", "突撃爆弾兵",   K::kEnemyKindDashBomber, -1.0f, { { "爆発", blast * b.dashBomberDamageMul } }, b.dashBomberExpMul, "9m で点火して突っ込む", nullptr },
        { "armor",      "呪いの鎧",     K::kEnemyKindArmor,      c * b.armorDamageMul, {}, b.armorExpMul, "一度崩れて蘇る", nullptr },
        { "gargoyle",   "石像ガーゴイル", K::kEnemyKindGargoyle, c * b.gargoyleDamageMul, { { "急降下", c * b.gargoyleDiveDamageMul } }, b.gargoyleExpMul, "眠っている間は撃てない", nullptr },
        { "necro",      "死霊術師",     K::kEnemyKindNecro,      -1.0f, {}, b.necroExpMul, "骸骨を 3 体起こす", nullptr },
        { "warden",     "鎖の番人",     K::kEnemyKindWarden,     c * b.wardenDamageMul, { { "鎖", c * b.wardenDamageMul } }, b.wardenExpMul, "鎖で引き寄せる", nullptr },
        // 他の敵が出す物
        { "splitling",    "分裂体",     K::kEnemyKindSplitling,  c * b.splitlingDamageMul, {}, b.splitlingExpMul, "", "splitter" },
        { "armorRevived", "蘇った鎧",   K::kEnemyKindArmorRevived, c * b.armorDamageMul, {}, b.armorRevivedExpMul, "", "armor" },
        { "skeleton",     "骸骨",       K::kEnemyKindSkeleton,   c * b.skeletonDamageMul, {}, b.skeletonExpMul, "", "necro" },
        // 最終ウェーブだけ
        { "ghost",      "白幽霊",       K::kEnemyKindGhost,      c * b.ghostDamageMul, {}, 1.0f, "壁を素通り", "final" },
        { "darkGhost",  "黒幽霊",       K::kEnemyKindDarkGhost,  c * b.darkGhostDamageMul, {}, b.darkGhostExpMul, "時間切れ 1 分後から", "final" },
    };
    constexpr int kSpawnable = 14;   // 上の表の先頭 14 種が湧きの抽選に入る

    // 素の HP・速さ（KindStats は今の HP 倍率を掛けて返す）
    std::vector<float> baseHp(kinds.size()), baseSpeed(kinds.size());
    const float nowMul = (std::max)(1.0e-4f, m_Mobs.GetHpMul());
    for (size_t i = 0; i < kinds.size(); ++i)
    {
        float hp = 0.0f, sp = 0.0f;
        m_Mobs.KindStats(kinds[i].kind, hp, sp);
        baseHp[i] = hp / nowMul;
        baseSpeed[i] = sp;
    }

    json root;
    root["note"] = "maps[].positions[] は 何面目か × Tier（ApplyStage と同じ倍率）";
    {
        json t = json::array();
        for (int tier = 1; tier <= StageConfig::kMaxTier; ++tier)
        {
            const auto& tm = StageConfig::TierMul(tier);
            t.push_back({ { "tier", tier }, { "hp", tm.hpMul }, { "damage", tm.damageMul }, { "exp", tm.expMul } });
        }
        root["tiers"] = t;
    }
    root["base"] = {
        { "contact", c }, { "blast", blast }, { "orbExp", orbExp },
        { "maxSpawnPerSecond", m_Mobs.maxSpawnPerSecond }, { "spawnCap", m_Mobs.Director().spawnCap },
        { "surgeMul", m_Stage.waves.surgeMul }, { "baseMul", m_Stage.waves.baseMul }, { "lullMul", m_Stage.waves.lullMul },
        { "eliteHp", m_Stage.eliteHp }, { "bossHp", m_Stage.bossHp }, { "affixHpMul", m_StageElites.affixHpMul },
        { "eliteContact", c * b.fixedEliteDamageMul }, { "bossContact", c * b.bossDamageMul },
        { "stageTime", m_Stage.stageTime }, { "finalStepTime", m_Stage.finalStepTime }, { "finalStepMul", m_Stage.finalStepMul },
        { "finalSpeedMul", m_Stage.finalSpeedMul },
    };
    {
        json ks = json::array();
        for (size_t i = 0; i < kinds.size(); ++i)
        {
            const KindDef& k = kinds[i];
            json sp = json::array();
            for (const Special& s : k.specials) sp.push_back({ { "name", s.name }, { "damage", R1(s.damage) } });
            ks.push_back({ { "key", k.key }, { "name", k.name }, { "hp", R1(baseHp[i]) }, { "speed", R3(baseSpeed[i]) },
                { "contact", k.contact < 0.0f ? -1.0f : R1(k.contact) }, { "specials", sp }, { "exp", R1(orbExp * k.expMul) },
                { "note", k.note }, { "parent", k.parent ? k.parent : "" } });
        }
        root["kinds"] = ks;
    }

    // 時間（秒）：0〜9 分は普段、10:00 から最終ウェーブ
    const float stageTime = m_Stage.stageTime;
    std::vector<float> times;
    for (int m = 0; m < 10; ++m) times.push_back(60.0f * (float)m);
    for (float over : { 0.0f, 30.0f, 60.0f, 120.0f }) times.push_back(stageTime + over);

    json maps = json::array();
    char line[320];
    for (int map = 1; map <= StageConfig::kStageCount; ++map)
    {
        const StageDef& def = StageConfig::Get(map);
        m_StageElites.Configure(map);
        const EliteType typeA = m_StageElites.TypeA(), typeB = m_StageElites.TypeB();
        json jm = { { "map", map }, { "name", Utf8(def.name) } };
        jm["elites"] = json::array();
        for (EliteType t : { typeA, typeB })
        {
            const auto& tt = m_StageElites.tune[(int)t];
            jm["elites"].push_back({ { "name", Utf8(EliteTypeName(t)) }, { "hpMul", tt.hpMul }, { "speed", tt.speed },
                { "skill", tt.damage }, { "ranged", t == typeB } });
        }

        json poses = json::array();
        for (int combo = 0; combo < 9; ++combo)   // 何面目か 1〜3 × Tier 1〜3
        {
            const int pos = 1 + combo / 3, tier = 1 + combo % 3;
            const float tierExp = m_Mobs.ApplyStage(def, pos, tier);
            json jp = { { "pos", pos }, { "tier", tier }, { "boss", Utf8(BossDisplayName(def.BossAt(pos))) },
                { "tableOffsetMin", m_Mobs.stageTimeOffsetMin }, { "stageHpMul", R3(m_Mobs.stageHpMul) },
                { "stageDamageMul", R3(m_Mobs.stageDamageMul) }, { "stageSpawnMul", R3(m_Mobs.stageSpawnMul) } };
            json rows = json::array();
            for (float t : times)
            {
                const bool isFinal = t >= stageTime;
                const float over = t - stageTime;
                const int level = isFinal ? 1 + (int)(over / (std::max)(m_Stage.finalStepTime, 1.0f)) : 0;
                const float finalStat = isFinal ? std::pow(m_Stage.finalStepMul, (float)(level - 1)) : 1.0f;
                const DifficultyCurve::Sample s = m_Mobs.curve.Evaluate(t / 60.0f + m_Mobs.stageTimeOffsetMin);
                const float hpMul = s.hpMul * m_Mobs.stageHpMul * finalStat;
                const float dmgMul = s.damageMul * m_Mobs.stageDamageMul * finalStat;
                const float speedMul = isFinal ? m_Stage.finalSpeedMul : 1.0f;
                float spawnNormal, spawnSurge;
                if (isFinal)
                {
                    const float ramp = (m_Stage.finalRampTime > 0.0f) ? (std::min)(1.0f, over / m_Stage.finalRampTime) : 1.0f;
                    spawnNormal = spawnSurge = (m_Stage.finalSpawnRateStart + (m_Stage.finalSpawnRate - m_Stage.finalSpawnRateStart) * ramp) * m_Mobs.stageSpawnMul;
                }
                else
                {
                    spawnNormal = (std::min)(s.spawnRate * m_Mobs.stageSpawnMul * m_Stage.waves.baseMul, m_Mobs.maxSpawnPerSecond);
                    spawnSurge = (std::min)(s.spawnRate * m_Mobs.stageSpawnMul * m_Stage.waves.surgeMul, m_Mobs.maxSpawnPerSecond);
                }

                // 割合（Request と同じく 1 回の乱数を前から区切る。合計が 1 を超えたら後ろが削られる）
                const float raw[kSpawnable - 1] = {
                    m_Mobs.GetBomberRatio(),
                    MobSpawner::KindRamp{ m_Mobs.splitterStart, m_Mobs.splitterRatioStart, m_Mobs.splitterRatioEnd, m_Mobs.splitterRampEnd }.At(t),
                    MobSpawner::KindRamp{ m_Mobs.bruteStart, m_Mobs.bruteRatioStart, m_Mobs.bruteRatioEnd, m_Mobs.bruteRampEnd }.At(t),
                    m_Mobs.chargerMix.At(t), m_Mobs.shieldMix.At(t), m_Mobs.spikeBallMix.At(t), m_Mobs.burrowerMix.At(t),
                    m_Mobs.sandMageMix.At(t), m_Mobs.dashBomberMix.At(t),
                    m_Mobs.armorMix.At(t), m_Mobs.gargoyleMix.At(t), m_Mobs.necroMix.At(t), m_Mobs.wardenMix.At(t),
                };
                float share[kSpawnable] = {};
                float rawSum = 0.0f;
                for (int i = 0; i < kSpawnable - 1; ++i) rawSum += raw[i];
                const float scale = MobSpawner::SpecialScale(rawSum, m_Mobs.maxSpecialShare);   // MobSpawner::Request と同じ
                float acc = 0.0f;
                for (int i = 0; i < kSpawnable - 1; ++i)
                {
                    share[i + 1] = raw[i] * scale;
                    acc += share[i + 1];
                }
                share[0] = (std::max)(0.0f, 1.0f - acc);

                json ks = json::object();
                auto put = [&](size_t i, float shareOrRate, bool isRate)
                {
                    const KindDef& k = kinds[i];
                    json sp = json::array();
                    for (const Special& x : k.specials) sp.push_back(R1(x.damage * dmgMul));
                    json e = { { "hp", R1(baseHp[i] * hpMul) }, { "speed", R3(baseSpeed[i] * speedMul) },
                        { "contact", k.contact < 0.0f ? -1.0f : R1(k.contact * dmgMul) }, { "specials", sp },
                        { "exp", R1(orbExp * k.expMul * tierExp) } };
                    e[isRate ? "rate" : "share"] = R3(shareOrRate);
                    ks[k.key] = e;
                };
                for (int i = 0; i < kSpawnable; ++i)
                    if (share[i] > 0.0f) put((size_t)i, share[i], false);
                // 親が湧いている時だけ子も載せる
                for (size_t i = kSpawnable; i < kinds.size(); ++i)
                {
                    const KindDef& k = kinds[i];
                    if (std::strcmp(k.parent, "final") == 0) continue;
                    if (ks.contains(k.parent)) put(i, 0.0f, false);
                }
                if (isFinal)
                {
                    const float ghostRate = m_Stage.finalGhostRate * std::pow(m_Stage.finalGhostStepMul, (float)(level - 1));
                    const float darkRate = (over >= m_Stage.finalDarkGhostStart)
                        ? m_Stage.finalDarkGhostRate * std::pow(m_Stage.finalDarkGhostStepMul, (float)(level - 1)) : 0.0f;
                    for (size_t i = 0; i < kinds.size(); ++i)
                    {
                        if (std::strcmp(kinds[i].key, "ghost") == 0) put(i, ghostRate, true);
                        else if (std::strcmp(kinds[i].key, "darkGhost") == 0 && darkRate > 0.0f) put(i, darkRate, true);
                    }
                }

                const int sec = (int)std::lround(t);
                char label[32];
                snprintf(label, sizeof(label), "%d:%02d", sec / 60, sec % 60);
                json row = { { "t", sec }, { "label", label }, { "final", isFinal }, { "finalLevel", level },
                    { "tableSpawn", R3(s.spawnRate) }, { "tableHp", R3(s.hpMul) }, { "tableDamage", R3(s.damageMul) },
                    { "hpMul", R3(hpMul) }, { "damageMul", R3(dmgMul) },
                    { "spawnNormal", R3(spawnNormal) }, { "spawnSurge", R3(spawnSurge) },
                    { "bossHp", R1(m_Stage.bossHp * hpMul) }, { "bossContact", R1(c * b.bossDamageMul * dmgMul) },
                    { "kinds", ks } };
                rows.push_back(row);
                snprintf(line, sizeof(line), "enemystats map %d pos %d tier %d %s hpMul %.2f dmgMul %.2f spawn %.2f / surge %.2f mob %.2f kinds %d",
                    map, pos, tier, label, hpMul, dmgMul, spawnNormal, spawnSurge, share[0], (int)ks.size());
                AutoTestLog(line);
            }
            jp["rows"] = rows;

            // エリート（3:00 = A 詞缀 0、6:00 = B 詞缀 1、8:00 = B 詞缀 2）
            json el = json::array();
            const int affixes[3] = { 0, 1, 2 };
            for (size_t k = 0; k < m_Stage.eliteTimes.size() && k < 3; ++k)
            {
                const float t = m_Stage.eliteTimes[k];
                const DifficultyCurve::Sample s = m_Mobs.curve.Evaluate(t / 60.0f + m_Mobs.stageTimeOffsetMin);
                const float hpMul = s.hpMul * m_Mobs.stageHpMul;
                const float dmgMul = s.damageMul * m_Mobs.stageDamageMul;
                const EliteType type = (k == 0) ? typeA : typeB;
                const auto& tt = m_StageElites.tune[(int)type];
                el.push_back({ { "t", (int)t }, { "name", Utf8(EliteTypeName(type)) }, { "affixes", affixes[k] },
                    { "hp", R1(m_Stage.eliteHp * hpMul * tt.hpMul * (1.0f + m_StageElites.affixHpMul * (float)affixes[k])) },
                    { "contact", R1(c * b.fixedEliteDamageMul * dmgMul) }, { "skill", R1(tt.damage * dmgMul) }, { "speed", tt.speed } });
            }
            jp["elites"] = el;
            poses.push_back(jp);
        }
        jm["positions"] = poses;
        maps.push_back(jm);
    }
    root["maps"] = maps;

    // 元に戻す（このテストは書き出して終わるが、念の為）
    m_StageElites.Configure(m_StageIndex);
    m_Mobs.ApplyStage(StageConfig::Get(m_StageIndex), 1, 1);

    std::ofstream ofs("enemystats.json");
    ofs << root.dump(1);
    ofs.close();
    snprintf(line, sizeof(line), "enemystats wrote enemystats.json kinds %d maps %d", (int)kinds.size(), (int)maps.size());
    AutoTestLog(line);
    AutoTestLog("enemystats done");
    m_AutoStep = 2;
}

REGISTER_BATTLE_AUTOTEST("enemystats", AutoTestEnemyStats)
