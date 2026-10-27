// ============================================================
// StageConfig.h
// 面（ステージ）の設定（2026-09-30）。戦闘シーンは 1 つ（CollisionTestScene）で、
// どの面かで 地形の見た目（TerrainGenerator::Biome）・照明と空と霧・草・難度の下駄 が変わる。
//   第 1 面 草原（今までの野原）/ 第 2 面 砂漠 / 第 3 面 遺跡（ダンジョン）
// 面の流れ: Boss を倒す → リザルト「次のステージへ」→ バックパック・魔法書・等級を持ったまま次の面（RunResult.h の g_RunCarry）。
// デバッグ: 環境変数 VFXL_STAGE=2 / 3 でその面から始める
// ============================================================
#pragma once
#include "World/TerrainGenerator.h"
#include "Graphics/Light/SceneLighting.h"
#include "Enemy/Boss/BossBrain.h"
#include <SimpleMath.h>
#include <algorithm>

struct StageDef
{
    int index = 1;                          // 1 始まり
    const wchar_t* name = L"";              // HUD・リザルト用
    TerrainGenerator::Biome biome = TerrainGenerator::Biome::Grassland;
    SceneLighting::Preset light;

    // 保存した地図（Assets/Data/MapData/<名前>.vmap、World/MapData）。nullptr = 毎回 seed から生成（2026-10-05）。
    // 地図を使う時、地形の見た目（Biome）と seed は地図の物になる
    const char* mapFile = nullptr;

    // 草（GrassRenderer）。砂漠は疎らな枯れ草、遺跡は無し
    bool  grass = true;
    float grassSpacing = 0.25f;
    DirectX::SimpleMath::Vector3 grassRoot = { 0.55f, 0.60f, 0.50f };
    DirectX::SimpleMath::Vector3 grassTip = { 1.25f, 1.20f, 0.85f };
    float grassHeightMin = 0.28f, grassHeightMax = 0.55f;   // 葉の高さ m

    // マップそのものの難しさ（2026-10-21 から）。Megabonk の「砂漠は上級」のように、開始マップとして選んだ時にも効く小さな倍率。
    // 「何面目か」による難度（表を進める + 大きな倍率。2026-10-17 の値）は StageConfig::PosDifficulty（プレイの中の位置で決まる）、
    // Tier の倍率は StageConfig::TierMul。実際の倍率 = 表 × PosDifficulty × これ × Tier（CollisionTestScene::Init）
    float difficultyTimeOffsetMin = 0.0f;
    float difficultyHpMul = 1.0f;
    float difficultyDamageMul = 1.0f;
    float difficultySpawnMul = 1.0f;
    // このマップでの「何面目か」の HP / ダメージの倍率（2026-10-27、ユーザー「草原 1→2→3 の難度曲線を直す、後でマップ毎に差別化」）。
    // [0] = 1 面目。0 = 共通の StageConfig::PosDifficulty を使う（表を進める分 timeOffsetMin は共通のまま）
    float posHpMul[3] = { 0.0f, 0.0f, 0.0f };
    float posDamageMul[3] = { 0.0f, 0.0f, 0.0f };

    // 新しい敵の混ざり方（2026-10-03、ユーザー「面毎の主力 + 面の中で時間で増える」）。
    // 湧き（新規・転送）のうちその種類の割合: start 秒までは 0、start で ratioStart、rampEnd 秒で ratioEnd（間は直線）。
    // 第 1 面の主力 = スプリッター、第 2 面 = 突撃兵、第 3 面 = 呪いの鎧（2026-10-25 から。それまでの盾兵は 5〜10% に下げた）
    struct EnemyMix
    {
        float start = 1.0e9f;
        float ratioStart = 0.0f;
        float ratioEnd = 0.0f;
        float rampEnd = 480.0f;
    };
    EnemyMix splitter = { 60.0f, 0.10f, 0.25f, 480.0f };
    // 突撃兵（第 2 面の主力）・盾兵（2026-10-08 は第 3 面の主力、10-25 から遺跡の敵に枠を譲った）。他の面は少し遅れて少しだけ混ざる
    EnemyMix charger = { 150.0f, 0.03f, 0.08f, 540.0f };
    EnemyMix shield = { 180.0f, 0.03f, 0.08f, 540.0f };
    EnemyMix spikeBall = { 1.0e9f, 0.0f, 0.0f, 480.0f };   // トゲ玉（砂漠の敵。2026-10-17。砂漠以外は出ない）
    EnemyMix burrower = { 1.0e9f, 0.0f, 0.0f, 480.0f };    // 砂潜り（砂漠の敵。2026-10-17。砂漠以外は出ない）
    EnemyMix sandMage = { 1.0e9f, 0.0f, 0.0f, 480.0f };    // 砂嵐の術師（砂漠の敵。2026-10-17。砂漠以外は出ない）
    EnemyMix dashBomber = { 150.0f, 0.03f, 0.08f, 480.0f }; // 突撃爆弾兵（全ステージ。2026-10-18）：150 秒から 3% → 8 分で 8%
    // 遺跡の敵（2026-10-25。遺跡以外は出ない）：呪いの鎧・石像ガーゴイル・死霊術師（骸骨を起こす）・鎖の番人
    EnemyMix armor = { 1.0e9f, 0.0f, 0.0f, 480.0f };
    EnemyMix gargoyle = { 1.0e9f, 0.0f, 0.0f, 480.0f };
    EnemyMix necro = { 1.0e9f, 0.0f, 0.0f, 480.0f };
    EnemyMix warden = { 1.0e9f, 0.0f, 0.0f, 480.0f };

    // 面の Boss（2026-10-24、ユーザー「面毎に専用の Boss」）。技の組み合わせ・召喚する敵・体の色（BossAttacks::Configure）
    BossType boss = BossType::StoneGiant;
    // 2 面目・3 面目の Boss（2026-10-26 から同じマップを続けるので面毎に別の Boss。Count = 1 面目と同じ）
    BossType boss2 = BossType::Count;
    BossType boss3 = BossType::Count;
    BossType BossAt(int pos) const   // pos = 何面目か（1 始まり）
    {
        const BossType b = (pos >= 3) ? boss3 : (pos == 2) ? boss2 : boss;
        return (b == BossType::Count) ? boss : b;
    }

    // 遺跡の松明（外周の壁に付く）に点光源を付ける数（プレイヤーに近い順。点光源の表（上限 256）を圧迫しないように）
    int torchLights = 12;
};

namespace StageConfig
{
    inline constexpr int kStageCount = 3;

    // 1..kStageCount。範囲外は端に丸める
    const StageDef& Get(int stage);

    // ---- ステージ選択（2026-10-21 ユーザー：「Megabonk に完全に倣う」。10-26 ユーザー「三つのマップを完全に分ける」）----
    // マップを選び、Tier n = 同じマップを n 面続けて戦う（森林 → 森林 2 → 森林 3。以前は草原 → 砂漠 → 遺跡 の順に続けていた）。
    // どのマップも Tier 1〜3。2 面目 / 3 面目の難度は PosDifficulty、マップの解放は Core/GameProgress
    inline constexpr int kMaxTier = 3;
    inline int MaxTier(int) { return kMaxTier; }

    // 「プレイの中で何面目か」による難度（2026-10-17 の面毎の難度をそのまま位置に付け替えた物）：
    // 2 面目は難度表を 10 分進めた所から × HP 2.5 …、3 面目は 20 分 × 2.8 …（持ち物を抱えて来る前提）
    struct RunDifficulty
    {
        float timeOffsetMin = 0.0f;
        float hpMul = 1.0f;
        float damageMul = 1.0f;
        float spawnMul = 1.0f;
    };
    const RunDifficulty& PosDifficulty(int posInRun);   // 0 = 1 面目

    // Tier の倍率（ユーザー 10-21：Tier 2 = 敵 HP・攻撃 ×1.2、Tier 3 = ×1.45。経験値も少し増える）
    struct TierDifficulty
    {
        float hpMul = 1.0f;
        float damageMul = 1.0f;
        float expMul = 1.0f;
    };
    const TierDifficulty& TierMul(int tier);   // 1..kMaxTier
}
