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
//       湧く速さ・HP の倍率・ダメージの倍率が表から決まる。面の倍率 stageHpMul / stageDamageMul / stageSpawnMul を掛け、表は stageTimeOffsetMin 分進めて読む。
//       HP は湧いた（転送された）瞬間の倍率で決まり、その後は変わらない。
//       接触ダメージ・爆発ダメージは雑魚毎の値を持たない（GPU の定数）ので、全員に今のダメージ倍率を掛ける
// ============================================================
#pragma once
#include "Enemy/DifficultyCurve.h"
#include "Enemy/SpawnDirector.h"
#include "Swarm/SwarmTypes.h"
#include <SimpleMath.h>
#include <cstdint>
#include <vector>

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
    // 面毎の難度（StageDef::difficulty*。2026-10-17）：表を stageTimeOffsetMin 分進めた所から読み、HP・ダメージ・湧きに掛ける
    float stageTimeOffsetMin = 0.0f;
    float stageHpMul = 1.0f;
    float stageDamageMul = 1.0f;
    float stageSpawnMul = 1.0f;
    // 面の終わりの押し寄せ（StageDirector が毎フレーム書く。時間切れ前は 1 / 0 / 1）
    float finalStatMul = 1.0f;          // HP・ダメージの倍率に更に掛ける
    float finalSpawnRate = 0.0f;        // > 0 なら湧く速さをこれにする（体/秒）
    float finalSpeedMul = 1.0f;         // 新しく湧く雑魚の速さに掛ける
    // 湧きの波（SpawnWaves。2026-10-07）と最終ウェーブの予告の上乗せ：湧く速さに掛ける。StageDirector が毎フレーム書く
    float waveRateMul = 1.0f;
    // 最終ウェーブの幽霊（kEnemyKindGhost。2026-09-30 ユーザー「Megabonk の時間切れの幽霊」）：
    // HP = 雑魚 × ghostHpMul（× 難度）、速さ × ghostSpeedMul、壁も台地も素通り。StageDirector が毎フレーム書く（時間切れ前は 0）。
    // 2026-10-18（ユーザー「十分過ぎても敵に殺されない」）：雑魚の上限とは別に幽霊だけの上限 ghostCap を持つ（以前は共通の上限を
    // 最終ウェーブの雑魚が先に埋めて幽霊がほぼ湧かなかった）。数は CompactCS が BossInfo に数えた物（2〜3 フレーム遅れ）
    float finalGhostRate = 0.0f;        // 体/秒
    float ghostSpeedMul = 1.71f;        // 雑魚の速さに掛ける（3.5 → 6.0 m/s = プレイヤーの走り 5.5 より速い、スライディングなら振り切れる。
                                        // 2026-10-07 夜に 2.6 → 1.57、10-18 に 1.71：逃げ切れる速さでは最終ウェーブが終わらない）
    float ghostHpMul = 6.0f;            // 雑魚の HP に掛ける（10-18。1 倍では当たった弾全部に落ちていた）
    int   ghostCap = 120;               // 白幽霊だけの同時上限
    // 黒幽霊（kEnemyKindDarkGhost。2026-10-18）：時間切れ finalDarkGhostStart 秒後から。StageDirector が毎フレーム書く
    float finalDarkGhostRate = 0.0f;    // 体/秒
    float darkGhostSpeedMul = 1.9f;     // 雑魚の速さに掛ける（3.5 → 6.65 m/s = 走り 5.5 の 1.2 倍、スライディング 13 なら振り切れる）
    float darkGhostHpMul = 40.0f;       // 雑魚の HP に掛ける（× 難度 ≈ 1000 以上：普通のビルドでは倒せない）
    int   darkGhostCap = 30;            // 黒幽霊だけの同時上限
    void QueueDebugGhosts(int n) { m_DebugGhosts += n; }
    void QueueDebugDarkGhosts(int n) { m_DebugDarkGhosts += n; }
    uint32_t GetGhostsSpawned() const { return m_GhostsSpawned; }
    uint32_t GetDarkGhostsSpawned() const { return m_DarkGhostsSpawned; }
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
    KindRamp spikeBallMix;   // トゲ玉（砂漠。2026-10-17）
    KindRamp burrowerMix;    // 砂潜り（砂漠。2026-10-17）
    KindRamp sandMageMix;    // 砂嵐の術師（砂漠。2026-10-17）
    KindRamp dashBomberMix;  // 突撃爆弾兵（全ステージ。2026-10-18）
    float GetChargerRatio() const { return m_ChargerRatio; }
    float GetShieldRatio() const { return m_ShieldRatio; }
    float GetSpikeBallRatio() const { return m_SpikeBallRatio; }
    float GetBurrowerRatio() const { return m_BurrowerRatio; }
    float GetSandMageRatio() const { return m_SandMageRatio; }
    float GetDashBomberRatio() const { return m_DashBomberRatio; }
    void QueueDebugChargers(int n) { m_DebugChargers += n; }
    void QueueDebugShields(int n) { m_DebugShields += n; }
    void QueueDebugSpikeBalls(int n) { m_DebugSpikeBalls += n; }
    void QueueDebugBurrowers(int n) { m_DebugBurrowers += n; }
    void QueueDebugSandMages(int n) { m_DebugSandMages += n; }
    void QueueDebugDashBombers(int n) { m_DebugDashBombers += n; }
    // 砂嵐の術師が放つ砂旋の速さ m/s（砂旋 = kEnemyKindTornado。飛ぶ秒は BomberCB::tornadoLife）
    float tornadoSpeed = 3.5f;
    uint32_t GetTornadoesSpawned() const { return m_TornadoesSpawned; }   // 自動テスト用（累計）
    // 砂旋が生まれた所・向き・速さ（見た目は CPU：Enemy/SandTornadoFx が同じ直線で追って柱を描く。2026-10-26）。場面が毎フレーム取り出す
    struct TornadoBirth { DirectX::SimpleMath::Vector3 pos; float yaw = 0.0f; float speed = 0.0f; };
    void ConsumeTornadoBirths(std::vector<TornadoBirth>& out) { out.swap(m_TornadoBirths); m_TornadoBirths.clear(); }
    // 自動テストの記録用: 届いた分裂の数・湧かせた分裂体の数（累計）
    uint32_t GetSplitEventsSeen() const { return m_SplitEventsSeen; }
    uint32_t GetSplitlingsSpawned() const { return m_SplitlingsSpawned; }

    // ---- 遺跡の敵（2026-10-25。中身は MobSpawnerRuins.cpp）----
    // 湧きの割合（面毎、StageDef の armor / gargoyle / necro / warden）。骸骨は死霊術師が起こすだけ
    KindRamp armorMix;
    KindRamp gargoyleMix;
    KindRamp necroMix;
    KindRamp wardenMix;
    float armorReviveDelay = 3.0f;      // 呪いの鎧が崩れてから蘇るまでの秒
    float armorReviveHpFrac = 0.5f;     // 蘇った鎧の HP（鎧の HP × 今の倍率 × これ）
    int   necroRaiseCount = 3;          // 1 回の詠唱で起こす骸骨の数
    float necroRaiseRadius = 12.0f;     // 死霊術師からこの距離の最近の死体から起こす（足りなければ自分の周り）
    float necroRaiseDelay = 0.8f;       // 地面に印が出てから骸骨が立つまでの秒
    float corpseMemory = 20.0f;         // 死体を覚えておく秒
    float gargoyleSpawnMin = 14.0f;     // 石像を置く距離（プレイヤーから m。普通の湧きの輪 37〜52m では目覚めを見られない）
    float gargoyleSpawnMax = 24.0f;
    // シーンが見た目・音を出す出来事（Consume で取り出す）
    enum class RuinsFxKind { ArmorFall, ArmorRise, GargoyleWake, GargoyleDive, GargoyleLand, NecroCast, RaiseMark, SkeletonRise };
    struct RuinsFx
    {
        RuinsFxKind kind;
        DirectX::SimpleMath::Vector3 pos;   // 地面の高さ
    };
    void ConsumeRuinsFx(std::vector<RuinsFx>& out) { out.insert(out.end(), m_RuinsFx.begin(), m_RuinsFx.end()); m_RuinsFx.clear(); }
    // 鎖の番人の投げ（GPU の溜めの終わり。向きは溜めの頭で固定した yaw）。鉤を飛ばすのはシーン
    struct ChainThrow
    {
        DirectX::SimpleMath::Vector3 pos;   // 番人の位置（敵の中心の高さ）
        float yaw = 0.0f;
    };
    void ConsumeChainThrows(std::vector<ChainThrow>& out) { out.insert(out.end(), m_ChainThrows.begin(), m_ChainThrows.end()); m_ChainThrows.clear(); }
    // 敵が死んだ所（シーンがダメージの数字の「倒した」から渡す。死霊術師が骸骨を起こす場所）
    void NoteKill(const DirectX::SimpleMath::Vector3& groundPos);
    void QueueDebugRuins(uint32_t kind, int n);
    void ClearRuinsPending();   // 場を消す時（トレーニングの「敵を全部消す」）：蘇り待ち・起き上がり待ちを捨てる
    float GetArmorRatio() const { return m_ArmorRatio; }
    float GetGargoyleRatio() const { return m_GargoyleRatio; }
    float GetNecroRatio() const { return m_NecroRatio; }
    float GetWardenRatio() const { return m_WardenRatio; }
    // 自動テストの記録用（累計）
    uint32_t GetArmorsRevived() const { return m_ArmorsRevived; }
    uint32_t GetSkeletonsRaised() const { return m_SkeletonsRaised; }
    uint32_t GetChainThrowsSeen() const { return m_ChainThrowsSeen; }
    uint32_t GetGargoyleWakes() const { return m_GargoyleWakes; }
    uint32_t GetGargoyleDives() const { return m_GargoyleDives; }

    // Enemies パネルの湧き管理・雑魚の初期値・自爆兵・Mob AI の段
    void DrawImGui(SwarmSystem& swarm);

private:
    // 遺跡の敵（MobSpawnerRuins.cpp）
    void UpdateRuins(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float dt, SwarmSystem& swarm);
    // 分裂の環の遺跡の出来事（鎧の崩れ・ガーゴイル・死霊術師の詠唱・鎖の投げ）。扱ったら true
    bool HandleRuinsEvent(const Swarm::SplitEvent& ev, const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm);
    // ガーゴイルの石像を置く所（プレイヤーの周り gargoyleSpawnMin〜Max の歩けるマス。無ければ普通の湧きの所）
    DirectX::SimpleMath::Vector3 GargoyleSpot(const DirectX::SimpleMath::Vector3& fallback, float groundY) const;
    void DrawRuinsImGui(SwarmSystem& swarm);
    float m_ArmorHp = 50.0f;      // 雑魚の 3.3 倍（さらに蘇る）
    float m_ArmorSpeed = 2.4f;    // 遅い
    float m_GargoyleHp = 30.0f;
    float m_GargoyleSpeed = 4.5f; // 飛ぶ速さ（急降下は BomberCB::gargoyleDiveSpeed）
    float m_NecroHp = 25.0f;
    float m_NecroSpeed = 3.0f;
    float m_SkeletonHp = 8.0f;    // 脆い
    float m_SkeletonSpeed = 4.2f; // 速い
    float m_WardenHp = 40.0f;
    float m_WardenSpeed = 2.8f;
    float m_ArmorRatio = 0.0f, m_GargoyleRatio = 0.0f, m_NecroRatio = 0.0f, m_WardenRatio = 0.0f;
    struct Pending { DirectX::SimpleMath::Vector3 pos; float timer; float hp; };
    std::vector<Pending> m_ArmorPending;
    std::vector<Pending> m_RaisePending;
    struct Kill { DirectX::SimpleMath::Vector3 pos; float time; };
    std::vector<Kill> m_RecentKills;
    std::vector<RuinsFx> m_RuinsFx;
    std::vector<ChainThrow> m_ChainThrows;
    int m_DebugRuins[6] = {};     // kEnemyKindArmor〜Warden の順
    const GridWorld* m_Grid = nullptr;
    DirectX::SimpleMath::Vector3 m_Player = { 0.0f, 0.0f, 0.0f };
    uint32_t m_ArmorsRevived = 0, m_SkeletonsRaised = 0, m_ChainThrowsSeen = 0, m_GargoyleWakes = 0, m_GargoyleDives = 0;

    // 1 体分の依頼（種類は m_BomberRatio・m_SplitterRatio で抽選）
    void Request(SwarmSystem& swarm, const DirectX::SimpleMath::Vector3& pos, bool recycle);
    // パネルのボタンの分：プレイヤーの周り（kDebugRingMin〜Max m の歩けるマス）に自爆兵・スプリッターを湧かせる
    void SpawnDebugKind(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm,
        int& count, float hp, float speed, uint32_t kind);
    // リードバックで届いた GPU の出来事（分裂の環）：スプリッターの死 → 分裂体、砂嵐の術師の詠唱 → 砂旋（2026-10-17）
    void SpawnFromEvents(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm);
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
    float m_SpikeBallRatio = 0.0f; // 今の湧きのうちトゲ玉の割合（2026-10-17）
    float m_SpikeBallHp = 60.0f;   // 雑魚の 4 倍（厚い）
    float m_SpikeBallSpeed = 2.6f; // 転がるので遅い（雑魚 3.5 の 0.75 倍）
    int   m_DebugSpikeBalls = 0;
    float m_BurrowerRatio = 0.0f;  // 今の湧きのうち砂潜りの割合（2026-10-17）
    float m_BurrowerHp = 25.0f;    // 雑魚の 1.7 倍（出ている間しか撃てない）
    float m_BurrowerSpeed = 3.0f;  // 基準。砂の下 ×1.3、出ている時 ×0.8（BomberCB の burrow*SpeedMul）
    int   m_DebugBurrowers = 0;
    float m_SandMageRatio = 0.0f;  // 今の湧きのうち砂嵐の術師の割合（2026-10-17）
    float m_SandMageHp = 20.0f;    // 雑魚の 1.33 倍（遠くに立つので弾が届きにくい）
    float m_SandMageSpeed = 3.2f;  // 距離を保つ歩き（溜め中は止まる）
    int   m_DebugSandMages = 0;
    float m_DashBomberRatio = 0.0f;  // 今の湧きのうち突撃爆弾兵の割合（2026-10-18）
    float m_DashBomberHp = 18.0f;    // 自爆兵と同じ（点火前に撃ち落とす）
    float m_DashBomberSpeed = 3.6f;  // 平時の追いかけ（点火後は BomberCB::dashBomberDashSpeed）
    int   m_DebugDashBombers = 0;
    uint32_t m_TornadoesSpawned = 0;
    std::vector<TornadoBirth> m_TornadoBirths;   // 今フレーム生まれた砂旋（ConsumeTornadoBirths で空になる）
    uint32_t m_SplitEventsSeen = 0;
    uint32_t m_SplitlingsSpawned = 0;
    int   m_DebugGhosts = 0;
    float m_GhostAccum = 0.0f;     // finalGhostRate の端数の持ち越し
    int   m_DebugDarkGhosts = 0;
    float m_DarkGhostAccum = 0.0f; // finalDarkGhostRate の端数の持ち越し
    uint32_t m_GhostsSpawned = 0;
    uint32_t m_DarkGhostsSpawned = 0;
    void SpawnGhosts(const DirectX::SimpleMath::Vector3& player, float dt, SwarmSystem& swarm);
    float m_HpMul = 1.0f;          // 今の HP の倍率（Update が経過時間から出す）
    float m_DamageMul = 1.0f;      // 今のダメージの倍率（同上）
    float m_RunTime = 0.0f;
};
