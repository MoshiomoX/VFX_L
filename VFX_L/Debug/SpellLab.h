// ============================================================
// SpellLab.h
// 魔法の組み合わせを自由に試す実験場（2026-10-06、ユーザー：魔法を任意に組み合わせて試せる場面）。
// 実験場シーン（タイトルの「トレーニング」/ F7、SpellLabScene）で使う：
//   ・実験モード = 湧き停止・全消し・無敵・MP 無限・経験値 0（レベルアップの四択が出ない）・9x9 全部に枠
//   ・2026-10-07 から操作はゲームの UI（UI/TrainingMenuUI、T / パッド RB）：的・雑魚の群れ・Boss を出す、
//     魔法・ルーンを木箱へ入れる / 空にする。メニューは依頼を積むだけで、ここ（Apply）が実際にやる。
//     木箱は空で始まる（以前は全部の道具を各 3 個入れていた。メニューの「全部入れる」で同じ事ができる）
// 窓「Spell Lab (F7)」（ImGui）は集約後の数値（間隔・MP・威力・半径・連鎖）を見せるだけ。
// 普通の戦闘では Game Test パネルの折り畳み（実験モードの入 / 切付き。的のボタンもそこ）。
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "SpellID.h"
#include <cstdint>
#include <random>
#include <string>

class SwarmSystem;
class MobSpawner;
class StageDirector;
class GridWorld;
struct TrainingRequest;

class SpellLab
{
public:
    // 戦闘シーンの Game Test パネルの中の折り畳み（実験モードの入 / 切付き）
    void DrawImGui(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs, const GridWorld& grid);
    // 実験場シーン：独立した窓（数値の表だけ）
    void DrawWindow(Registry& reg, Entity player);
    // 実験場シーンの開始時：実験モードに入り、9x9 全部に枠を敷く（木箱は空のまま）
    void EnterLab(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs);
    // 毎フレーム（gameplay の中）：実験モードの間、経験値を 0 に保つ・MP を満たす・群れの残りを少しずつ湧かせる
    void Update(Registry& reg, Entity player, SwarmSystem& swarm, const MobSpawner& mobs, const GridWorld& grid);
    // トレーニングのメニューの依頼を 1 つ実行する（一時停止中にも呼ばれる）。戻り値 = メニューに出す結果の一行
    std::wstring Apply(const TrainingRequest& q, Registry& reg, Entity player, SwarmSystem& swarm,
        MobSpawner& mobs, StageDirector& stage, const GridWorld& grid);
    bool Enabled() const { return m_Enabled; }

private:
    void SetEnabled(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs, bool on);
    void FillFrames(Registry& reg, Entity player);
    void FillChest(Registry& reg, Entity player);    // 全部の魔法・ルーンを箱へ（足りない分だけ足す）
    void ClearChest(Registry& reg, Entity player);   // 箱の中（バックパックに置いていない分）を全部捨てる
    void SpawnTargets(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid, int pattern, float hp);
    void SpawnSwarmStep(Registry& reg, Entity player, SwarmSystem& swarm, const MobSpawner& mobs, const GridWorld& grid);
    void DrawBody(Registry& reg, Entity player);
    void DrawStats(Registry& reg, Entity player);

    static constexpr int kCopies = 3;            // 「全部入れる」で揃える各道具の数
    static constexpr int kSwarmPerFrame = 64;    // 群れは 1 フレームにこれだけ（GPU の生成依頼は 1 フレーム 256 まで）

    bool  m_Enabled = false;
    bool  m_WasInvincible = false;
    float m_WasManaMax = 100.0f;
    bool  m_WasSpawning = true;
    float m_TargetHp = 1.0e6f;   // ImGui の的のボタン用
    char  m_Message[96] = "";    // 最後の操作の結果（ImGui）

    // 群れの残り（Apply で積み、Update で kSwarmPerFrame ずつ湧かせる）
    int   m_SwarmLeft = 0;
    int   m_SwarmKind = 0;       // TrainingMenuUI::kSwarmKinds の番号（4 = 混合）
    bool  m_SwarmMoving = true;
    std::mt19937 m_Rng{ 20261007u };
};
