// ============================================================
// MobSpawner.h
// 雑魚の湧き管理（CPU 側の窓口）。実体は GPU（SwarmSystem）にしか居ない。
//   ・SpawnDirector が「何体をどこへ」を決め、ここが GPU へ生成 / 遠い雑魚の転送を依頼する
//   ・数えるのは GPU の生存数（リードバックなので 1〜2 フレーム古い）。
//     空き枠が無い分は「遠い雑魚の転送」として GPU に投げる
//   ・雑魚の初期値（HP / 速さ）と Mob AI の定数（GPU、次の固定ステップから効く）のパネル
//   ・自爆兵：新規・転送の 1 体ずつ m_BomberRatio の確率で自爆兵にする。
//     導火線・爆発の定数は SwarmSystem::GetBomberParams（見た目の範囲は AreaData/BomberBlast.json）
//   ・スプリッター：同じく m_SplitterRatio の確率（面毎に経過時間で伸びる）。死ぬと GPU の分裂の環が届き、分裂体を 3 体湧かせる
//   ・難度（経過時間で上がる）：DifficultyCurve の表（2026-10-04 から。以前は全部直線で、掛け合わせて二乗で伸びた）。
//       湧く速さ・HP の倍率・ダメージの倍率が表から決まる。面の下駄 statMulBonus は HP とダメージの両方に足す。
//       HP は湧いた（転送された）瞬間の倍率で決まり、その後は変わらない。
//       接触ダメージ・爆発ダメージは雑魚毎の値を持たない（GPU の定数）ので、全員に今のダメージ倍率を掛ける
// ============================================================
#pragma once
#include "Enemy/DifficultyCurve.h"
#include "Enemy/SpawnDirector.h"
#include <SimpleMath.h>
#include <cstdint>

class GridWorld;
class SwarmSystem;

class MobSpawner
{
public:
    void Init(SwarmSystem& swarm);
    // runTime: 戦闘の経過秒（一時停止・バックパック・三択の間は進まない。HUD の計時と同じ）
    void Update(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float dt, float runTime,
        SwarmSystem& swarm);

    SpawnDirector& Director() { return m_Director; }

    // ---- 難度 ----
    bool  scaling = true;               // false なら倍率 1・湧く速さは Director の値のまま（自動テスト・負荷試験用）
    DifficultyCurve curve;              // 経過時間 → 湧く速さ・HP・ダメージ（Init で Difficulty.json を読む）
    float statMulBonus = 0.0f;          // 面の下駄（StageDef::difficultyBonus。第 2 面 +0.6、第 3 面 +1.2）。HP・ダメージの倍率に足す
    // 面の終わりの押し寄せ（StageDirector が毎フレーム書く。時間切れ前は 1 / 0 / 1）
    float finalStatMul = 1.0f;          // HP・ダメージの倍率に更に掛ける
    float finalSpawnRate = 0.0f;        // > 0 なら湧く速さをこれにする（体/秒）
    float finalSpeedMul = 1.0f;         // 新しく湧く雑魚の速さに掛ける
    // 湧きの波（SpawnWaves。2026-10-07）と最終ウェーブの予告の上乗せ：湧く速さに掛ける。StageDirector が毎フレーム書く
    float waveRateMul = 1.0f;
    // 最終ウェーブの幽霊（kEnemyKindGhost。2026-09-30 ユーザー「Megabonk の時間切れの幽霊」）：
    // 雑魚と同じ HP（× 難度）、速さ × ghostSpeedMul、壁も台地も素通り。StageDirector が毎フレーム書く（時間切れ前は 0）
    float finalGhostRate = 0.0f;        // 体/秒
    float ghostSpeedMul = 1.57f;        // 雑魚の速さに掛ける（3.5 → 5.5 m/s = プレイヤーの走り 5 の 1.1 倍、滑りなら振り切れる。
                                        // 2026-10-07 夜に 2.6 → 1.57：同類の幽霊・終盤の速い敵はプレイヤー以下〜1.1 倍、9.1 m/s は飛び抜けていた）
    void QueueDebugGhosts(int n) { m_DebugGhosts += n; }
    // 倍率 1 の時の接触 / 爆発ダメージ。Init で GPU の既定値（AICB / BomberCB）から取る
    float baseContactDamage = 0.0f;
    float baseBlastDamage = 0.0f;
    float GetHpMul() const { return m_HpMul; }           // 今湧く敵の HP の倍率（エリート・Boss の HP にも使う）
    float GetDamageMul() const { return m_DamageMul; }   // 今のダメージの倍率（接触・爆発・Boss のスラム）

    // 次の Update でプレイヤーの周りに自爆兵を n 体（パネルのボタン・自動テスト用）
    void QueueDebugBombers(int n) { m_DebugBombers += n; }

    // ---- スプリッター（kEnemyKindSplitter。2026-10-03）----
    // 湧き（新規・転送）の splitterRatio がスプリッター。比率は面毎（StageDef）に経過時間で伸びる:
    //   splitterStart 秒までは 0、そこで splitterRatioStart、splitterRampEnd 秒で splitterRatioEnd（間は直線、以降そのまま）
    // 死ぬと GPU の分裂の環 → ここで分裂体（kEnemyKindSplitling）を splitCount 体、死んだ所の周りに湧かせる
    float splitterStart = 1.0e9f;
    float splitterRatioStart = 0.0f;
    float splitterRatioEnd = 0.0f;
    float splitterRampEnd = 480.0f;
    float GetSplitterRatio() const { return m_SplitterRatio; }
    float GetBomberRatio() const { return m_BomberRatio; }
    // 種類毎の今の HP（素の値 × 今の HP 倍率）と速さ。トレーニングの「群れを出す」用（2026-10-07）
    void KindStats(uint32_t kind, float& hp, float& speed) const;
    void QueueDebugSplitters(int n) { m_DebugSplitters += n; }

    // ---- 重装兵（kEnemyKindBrute。2026-10-07 夜、ユーザー：中盤は数ではなく敵の組み合わせで押す）----
    // 湧き（新規・転送）の bruteRatio が重装兵（雑魚と入れ替わる。湧く数は変えない）。全ステージ共通:
    //   bruteStart 秒までは 0、そこで bruteRatioStart、bruteRampEnd 秒で bruteRatioEnd（間は直線、以降そのまま）
    float bruteStart = 240.0f;
    float bruteRatioStart = 0.08f;
    float bruteRatioEnd = 0.22f;
    float bruteRampEnd = 540.0f;
    float GetBruteRatio() const { return m_BruteRatio; }
    void QueueDebugBrutes(int n) { m_DebugBrutes += n; }

    // ---- 突撃兵（kEnemyKindCharger）・盾兵（kEnemyKindShield）。2026-10-08 ----
    // 湧き（新規・転送）のうちの割合。スプリッターと同じ形の直線で、面毎（StageDef::charger / shield、シーンが入れる）。
    // 第 2 面は突撃兵、第 3 面は盾兵が主力。雑魚と入れ替わる（湧く数は変えない）。
    // 溜め・突進・盾の装甲の定数は GPU（SwarmSystem::GetBomberParams の charger* / shield*）
    struct KindRamp
    {
        float start = 1.0e9f;      // この秒までは 0
        float ratioStart = 0.0f;   // start の時の割合
        float ratioEnd = 0.0f;     // rampEnd 秒の割合（間は直線、以降そのまま）
        float rampEnd = 480.0f;
        float At(float runTime) const;
    };
    KindRamp chargerMix;
    KindRamp shieldMix;
    float GetChargerRatio() const { return m_ChargerRatio; }
    float GetShieldRatio() const { return m_ShieldRatio; }
    void QueueDebugChargers(int n) { m_DebugChargers += n; }
    void QueueDebugShields(int n) { m_DebugShields += n; }
    // 自動テストの記録用: 届いた分裂の数・湧かせた分裂体の数（累計）
    uint32_t GetSplitEventsSeen() const { return m_SplitEventsSeen; }
    uint32_t GetSplitlingsSpawned() const { return m_SplitlingsSpawned; }

    // Enemies パネルの湧き管理・雑魚の初期値・自爆兵・Mob AI の段
    void DrawImGui(SwarmSystem& swarm);

private:
    // 1 体分の依頼（種類は m_BomberRatio・m_SplitterRatio で抽選）
    void Request(SwarmSystem& swarm, const DirectX::SimpleMath::Vector3& pos, bool recycle);
    // パネルのボタンの分：プレイヤーの周り（kDebugRingMin〜Max m の歩けるマス）に自爆兵・スプリッターを湧かせる
    void SpawnDebugKind(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm,
        int& count, float hp, float speed, uint32_t kind);
    // リードバックで届いたスプリッターの死 → 分裂体
    void SpawnSplitlings(const GridWorld& grid, SwarmSystem& swarm);
    // Enemies パネルの突撃兵・盾兵・凍結の段（MobSpawnerKinds.cpp。2026-10-08）
    void DrawChargerShieldImGui(SwarmSystem& swarm);

    SpawnDirector m_Director;
    float m_MobHp = 15.0f;         // Megabonk 1 面の雑魚（6〜20）の中ほど
    float m_MobSpeed = 3.5f;

    float m_BomberRatio = 0.15f;   // 湧きのうち自爆兵の割合
    float m_BomberHp = 18.0f;      // Megabonk の Boomer
    float m_BomberSpeed = 4.5f;    // 雑魚より速く寄ってくる
    int   m_DebugBombers = 0;      // ボタンで溜めた数。次の Update で湧かせる

    float m_SplitterRatio = 0.0f;  // 今の湧きのうちスプリッターの割合（Update が splitter* と経過時間から出す）
    float m_SplitterHp = 30.0f;    // 雑魚の 2 倍
    float m_SplitterSpeed = 3.2f;  // 少し遅い
    float m_SplitlingHp = 6.0f;
    float m_SplitlingSpeedMul = 1.3f;   // 雑魚の速さに掛ける
    int   m_SplitCount = 3;
    float m_SplitSpread = 0.7f;    // 死んだ所から分裂体までの距離 m
    int   m_DebugSplitters = 0;
    float m_BruteRatio = 0.0f;     // 今の湧きのうち重装兵の割合（Update が brute* と経過時間から出す）
    float m_BruteHp = 45.0f;       // 雑魚の 3 倍（Megabonk の Goblin Tank 40 / 雑魚 7〜20）
    float m_BruteSpeed = 2.8f;     // 雑魚 3.5 の 0.8 倍、プレイヤー 5 の 0.56 倍
    int   m_DebugBrutes = 0;
    float m_ChargerRatio = 0.0f;   // 今の湧きのうち突撃兵の割合（Update が chargerMix と経過時間から出す）
    float m_ChargerHp = 20.0f;     // 雑魚の 1.33 倍（突進の後の息切れが倒し時）
    float m_ChargerSpeed = 3.8f;   // 追いかける速さは雑魚より少し速い（突進は GPU の chargerDashSpeed）
    int   m_DebugChargers = 0;
    float m_ShieldRatio = 0.0f;    // 今の湧きのうち盾兵の割合
    float m_ShieldHp = 30.0f;      // 雑魚の 2 倍。これに 1 発毎の装甲（BomberCB::shieldArmor）が乗る
    float m_ShieldSpeed = 3.0f;    // 盾が重いので少し遅い
    int   m_DebugShields = 0;
    uint32_t m_SplitEventsSeen = 0;
    uint32_t m_SplitlingsSpawned = 0;
    int   m_DebugGhosts = 0;
    float m_GhostAccum = 0.0f;     // finalGhostRate の端数の持ち越し
    void SpawnGhosts(const DirectX::SimpleMath::Vector3& player, float dt, SwarmSystem& swarm);
    float m_HpMul = 1.0f;          // 今の HP の倍率（Update が経過時間から出す）
    float m_DamageMul = 1.0f;      // 今のダメージの倍率（同上）
    float m_RunTime = 0.0f;
};
