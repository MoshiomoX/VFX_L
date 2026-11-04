// ============================================================
// StageConfig.cpp
// 色は全部線形 HDR（SceneLighting と同じ）。sRGB で見せたい色は 2.2 乗した値
// ============================================================
#include "World/StageConfig.h"

namespace
{
    StageDef MakeGrassland()
    {
        StageDef s;
        s.index = 1;
        s.name = L"草原";
        s.boss2 = BossType::Werewolf;   // 2 面目は月下の狼人（2026-10-26。飛びかかり / 爪の三連 / 遠吠え）
        s.boss3 = BossType::Stag;   // 3 面目は雷鳴の雄鹿（2026-10-26、方块の動物の試作）
        s.biome = TerrainGenerator::Biome::Grassland;
        // （10-27 に草原だけ入れた値。11-01 から PosDifficulty の共通の値になった）
        // 何面目かの倍率（2026-10-27）。以前の共通の値（HP 2.5 / 2.8、ダメージ 1.6 / 1.6）だと、表の進み（+10 / +20 分）と掛けて
        // 実際の HP は 1 面 1.0→1.75 | 2 面 4.4→6.25 | 3 面 7.0→9.1、ダメージは 1.0→1.25 | 2.0→2.4 | 2.4→2.8：1→2 で跳ね、2→3 はほぼ平ら（ダメージは同じ）。
        // 面の境で「前の面の終わり × 一定」になるように：HP x1.6 / ダメージ x1.25 →
        // HP 1.0→1.75 | 2.8→4.0 | 6.4→8.3、ダメージ 1.0→1.25 | 1.56→1.88 | 2.34→2.73
        // 照明・草は SceneLighting / GrassRenderer の既定値のまま（晴れた昼の野原）
        return s;
    }

    StageDef MakeDesert()
    {
        StageDef s;
        s.index = 2;
        s.name = L"砂漠";
        s.biome = TerrainGenerator::Biome::Desert;
        s.boss = BossType::SandGuardian;   // 砂の守護者（2026-10-24）
        s.boss2 = BossType::Scorpion;      // 2 面目は大サソリ（2026-10-26。毒針 / 鋏 / 潜砂 / 召喚）
        s.boss3 = BossType::Djinn;         // 3 面目は砂嵐の魔神（2026-10-29。浮いて砂嵐召喚 / 炎の息 / 砂渡り → 咆哮で降りて砂の拳 / 衝撃波 / 流沙）
        // 砂塵で霞んだ砂漠（2026-10-05：日差しが強すぎた → 太陽を弱め、空は砂色に濁らせ、靄を近くから濃く）
        s.light.sunPitch = 55.0f;
        s.light.sunYaw = 20.0f;
        s.light.lightColor = { 1.0f, 0.90f, 0.74f };
        s.light.lightIntensity = 0.60f;
        s.light.ambientSky = { 0.40f, 0.37f, 0.31f };
        s.light.ambientGround = { 0.24f, 0.18f, 0.11f };
        s.light.skyZenith = { 0.40f, 0.42f, 0.46f };
        s.light.skyHorizon = { 0.60f, 0.52f, 0.40f };
        s.light.skyBelow = { 0.40f, 0.33f, 0.24f };
        s.light.sunGlow = 0.25f;
        s.light.fogUseHorizon = true;
        s.light.fogStart = 10.0f;
        s.light.fogEnd = 85.0f;
        s.light.fogMax = 0.93f;
        // 草はオアシス（水たまりの周り 7m）だけ（2026-11-02 晩。昼に「砂漠に草があるのは変」で一度消した → 池の周りの緑なら自然）。
        // 草の色 = 地面の色 × 根元 / 先。砂漠の地面は砂の黄色なので、オリーブの緑に見えるよう根元・先とも緑へ大きく振る。
        // 草がある所では草の延焼・枯れ草の帯・草の下の地面の暗さも効く
        s.grass = true;
        s.oasisGrass = 7.0f;
        s.grassSpacing = 0.2f;
        s.grassRoot = { 0.30f, 0.55f, 0.25f };
        s.grassTip = { 0.75f, 1.30f, 0.55f };
        s.grassHeightMin = 0.28f;
        s.grassHeightMax = 0.55f;
        s.difficultyHpMul = 1.15f; s.difficultyDamageMul = 1.10f;   // マップ自体が少し硬い（開始マップとして選んだ時にも効く。2026-10-21。「何面目か」の倍率は PosDifficulty）
        s.splitter = { 120.0f, 0.05f, 0.12f, 480.0f };   // スプリッターは第 1 面の主力。ここは少し混ざるだけ
        s.charger = { 60.0f, 0.08f, 0.18f, 480.0f };      // 砂漠の主力 = 突撃兵（開けた砂地を突っ込んでくる。2026-10-08）
        s.spikeBall = { 45.0f, 0.06f, 0.14f, 480.0f };   // トゲ玉（2026-10-17）：45 秒から 6% → 8 分で 14%
        s.burrower = { 90.0f, 0.05f, 0.12f, 480.0f };    // 砂潜り（2026-10-17）：90 秒から 5% → 8 分で 12%
        s.sandMage = { 120.0f, 0.04f, 0.10f, 480.0f };   // 砂嵐の術師（2026-10-17）：2 分から 4% → 8 分で 10%
        return s;
    }

    StageDef MakeDungeon()
    {
        StageDef s;
        s.index = 3;
        s.name = L"遺跡";
        s.biome = TerrainGenerator::Biome::Dungeon;
        s.boss = BossType::CryptKnight;   // 1 面目は墓守の騎士（2026-10-26。双鎖 / 重撃 / 衝撃波 / 鎧の召喚、1 回蘇る）
        s.boss2 = BossType::StoneBeast;   // 2 面目は石の守護獣（2026-10-29。飛んで急降下 / 石化の息 / 翼の突風 → 咆哮で降りて尾の薙ぎ払い / 突進 / 投石雨）
        s.boss3 = BossType::BoneKing;     // 3 面目は骸の王（2026-10-29。呪いの玉 / 大起尸 / 影へ消える / 呪いの床）
        // 空は無い（黒に近い天井）。弱い青い月明かりを主光にして、松明の点光源が主役
        s.light.sunPitch = 70.0f;
        s.light.sunYaw = 30.0f;
        s.light.lightColor = { 0.60f, 0.66f, 0.90f };
        s.light.lightIntensity = 0.45f;
        s.light.ambientSky = { 0.11f, 0.11f, 0.14f };
        s.light.ambientGround = { 0.05f, 0.05f, 0.05f };
        s.light.skyZenith = { 0.004f, 0.004f, 0.008f };
        s.light.skyHorizon = { 0.010f, 0.010f, 0.016f };
        s.light.skyBelow = { 0.004f, 0.004f, 0.006f };
        s.light.sunGlow = 0.0f;
        s.light.fogUseHorizon = false;
        s.light.fogColor = { 0.012f, 0.012f, 0.015f };
        s.light.fogStart = 12.0f;
        s.light.fogEnd = 70.0f;
        s.light.fogMax = 0.92f;
        s.grass = false;
        s.difficultyHpMul = 1.30f; s.difficultyDamageMul = 1.20f; s.difficultySpawnMul = 1.05f;   // 一番硬いマップ（2026-10-21。「何面目か」の倍率は PosDifficulty）
        s.splitter = { 120.0f, 0.04f, 0.08f, 480.0f };
        s.shield = { 60.0f, 0.05f, 0.10f, 480.0f };       // 盾兵（2026-10-08 は遺跡の主力 0.08 → 0.18。10-25 に遺跡の敵へ枠を譲った）
        // 遺跡の敵（2026-10-25）：主力は呪いの鎧、ガーゴイル・術師・番人が時間で混ざる
        s.armor = { 60.0f, 0.05f, 0.12f, 480.0f };
        s.gargoyle = { 90.0f, 0.03f, 0.06f, 480.0f };
        s.necro = { 120.0f, 0.02f, 0.05f, 480.0f };
        s.warden = { 120.0f, 0.03f, 0.06f, 480.0f };
        s.torchLights = 12;
        return s;
    }

    const StageDef& Table(int i)
    {
        static const StageDef kStages[StageConfig::kStageCount] = { MakeGrassland(), MakeDesert(), MakeDungeon() };
        return kStages[i];
    }
}

namespace StageConfig
{
    const StageDef& Get(int stage)
    {
        return Table(std::clamp(stage, 1, kStageCount) - 1);
    }

    const RunDifficulty& PosDifficulty(int posInRun)
    {
        // 2026-10-17 の値：2 面目 = 表 +10 分 × HP 2.5 / ダメージ 1.6 / 湧き 1.3、3 面目 = +20 分 × 2.8 / 1.6 / 1.3
        // （×6 / ×2.4 / ×1.6 は壁、×3.2 / ×1.8 / ×1.35 でも毎秒 12〜51 減る。第 1 面の持ち物での計測なので少し甘く）
        // 10-26 ユーザー「敵が溜まり過ぎ」：湧き ×1.3 を外した（2 面目 / 3 面目の難度は HP・ダメージの倍率で。湧きは MobSpawner::maxSpawnPerSecond でも止める）
        static const RunDifficulty kPos[kStageCount] = {
            { 0.0f, 1.0f, 1.0f, 1.0f },
            // 2026-11-01：全マップ共通で「前の面の終わり × HP 1.6 / ダメージ 1.25」（マップ自体の倍率はその上に掛かる）。
            // 砂漠・遺跡は以前の 2.5 / 2.8 のままで、1 面の終わり → 2 面の始めで HP が 2.5 倍に跳んでいた。
            // マップ毎に変えたい時は StageDef::posHpMul / posDamageMul
            { 10.0f, 1.6f, 1.25f, 1.0f },
            { 20.0f, 2.56f, 1.56f, 1.0f },
        };
        return kPos[std::clamp(posInRun, 0, kStageCount - 1)];
    }

    const TierDifficulty& TierMul(int tier)
    {
        static const TierDifficulty kTier[kMaxTier] = {
            { 1.0f, 1.0f, 1.0f },
            { 1.2f, 1.2f, 1.1f },
            { 1.45f, 1.45f, 1.2f },
        };
        return kTier[std::clamp(tier, 1, kMaxTier) - 1];
    }
}
