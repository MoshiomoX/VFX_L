// ============================================================
// MobSpawner.h
// 雑魚の湧き管理（CPU 側の窓口）。実体は GPU（SwarmSystem）にしか居ない。
//   ・SpawnDirector が「何体をどこへ」を決め、ここが GPU へ生成 / 遠い雑魚の転送を依頼する
//   ・数えるのは GPU の存活数（回読なので 1〜2 フレーム古い）。
//     空き枠が無い分は「遠い雑魚の転送」として GPU に投げる
//   ・雑魚の初期値（HP / 速さ）と Mob AI の定数（GPU、次の固定ステップから効く）の面板
//   ・自爆兵：新規・転送の 1 体ずつ m_BomberRatio の確率で自爆兵にする。
//     導火線・爆発の定数は SwarmSystem::GetBomberParams（見た目の範囲は AreaData/BomberBlast.json）
//   ・難度（経過時間で上がる。Megabonk 風）：
//       強さの倍率 = 1 + statGrowthPerMin × 分。HP は湧いた（転送された）瞬間の倍率で決まり、その後は変わらない。
//       接触ダメージ・爆発ダメージは雑魚毎の値を持たない（GPU の定数）ので、全員に今の倍率を掛ける。
//       湧く速さは spawnRateStart から 10 分で spawnRateAt10Min まで直線で上がる（その後も伸びる）
// ============================================================
#pragma once
#include "Enemy/SpawnDirector.h"
#include <SimpleMath.h>

class GridWorld;
class SwarmSystem;

class MobSpawner
{
public:
    void Init(SwarmSystem& swarm);
    // runTime: 戦闘の経過秒（一時停止・背包・三択の間は進まない。HUD の計時と同じ）
    void Update(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float dt, float runTime,
        SwarmSystem& swarm);

    SpawnDirector& Director() { return m_Director; }

    // ---- 難度 ----
    bool  scaling = true;               // false なら倍率 1・湧く速さは Director の値のまま（自測・負荷試験用）
    float statGrowthPerMin = 0.12f;     // HP・ダメージの伸び（1 分あたり）。10 分で 2.2 倍
    float statMulBonus = 0.0f;          // 面の下駄（StageDef::difficultyBonus。第 2 面 +0.6、第 3 面 +1.2）。倍率に足す
    float spawnRateStart = 1.0f;        // 開始時の湧き（体/秒）
    float spawnRateAt10Min = 8.0f;      // 10 分時点の湧き（体/秒）
    // 面の終わりの押し寄せ（StageDirector が毎フレーム書く。時間切れ前は 1 / 0 / 1）
    float finalStatMul = 1.0f;          // 強さの倍率に更に掛ける
    float finalSpawnRate = 0.0f;        // > 0 なら湧く速さをこれにする（体/秒）
    float finalSpeedMul = 1.0f;         // 新しく湧く雑魚の速さに掛ける
    // 倍率 1 の時の接触 / 爆発ダメージ。Init で GPU の既定値（AICB / BomberCB）から取る
    float baseContactDamage = 0.0f;
    float baseBlastDamage = 0.0f;
    float GetStatMul() const { return m_StatMul; }

    // 次の Update で玩家の周りに自爆兵を n 体（面板のボタン・自測用）
    void QueueDebugBombers(int n) { m_DebugBombers += n; }

    // Enemies 面板の湧き管理・雑魚の初期値・自爆兵・Mob AI の段
    void DrawImGui(SwarmSystem& swarm);

private:
    // 1 体分の依頼（種類は m_BomberRatio で抽選）
    void Request(SwarmSystem& swarm, const DirectX::SimpleMath::Vector3& pos, bool recycle);
    // 面板のボタンの分：玩家の周り（kDebugRingMin〜Max m の歩けるマス）に自爆兵を湧かせる
    void SpawnDebugBombers(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm);

    SpawnDirector m_Director;
    float m_MobHp = 15.0f;         // Megabonk 1 面の雑魚（6〜20）の中ほど
    float m_MobSpeed = 3.5f;

    float m_BomberRatio = 0.15f;   // 湧きのうち自爆兵の割合
    float m_BomberHp = 18.0f;      // Megabonk の Boomer
    float m_BomberSpeed = 4.5f;    // 雑魚より速く寄ってくる
    int   m_DebugBombers = 0;      // ボタンで溜めた数。次の Update で湧かせる
    float m_StatMul = 1.0f;        // 今の強さの倍率（Update が経過時間から出す）
    float m_RunTime = 0.0f;
};
