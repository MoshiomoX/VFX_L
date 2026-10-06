// ============================================================
// SpellLab.h
// 魔法の組み合わせを自由に試す実験場（2026-10-06、ユーザー：魔法を任意に組み合わせて試せる場面）。
// 実験場シーン（F7、SpellLabScene）で使う。やる事は最小限（10-06 ユーザー：「箱に入れてくれれば自分で組む」）：
//   ・実験モード = 湧き停止・全消し・無敵・MP 無限・経験値 0（レベルアップの四択が出ない）・9x9 全部に枠
//   ・全部の魔法・ルーンを各 kCopies 個ずつ魔法書（木箱）へ入れる → 組み合わせは Tab の普通のバックパックで
//   ・的だけキー：T / Y / U / O（正面 3 体 / 周り 12 体 / エリート / Boss。全部動かない）、K = 敵を全消し
// 窓「Spell Lab (F7)」はキー一覧と集約後の数値（間隔・MP・威力・半径・連鎖）を見せるだけ。
// 普通の戦闘では Game Test パネルの折り畳み（実験モードの入 / 切付き。入の間だけキーが効く）。
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "SpellID.h"

class SwarmSystem;
class MobSpawner;
class GridWorld;

class SpellLab
{
public:
    // 戦闘シーンの Game Test パネルの中の折り畳み（実験モードの入 / 切付き）
    void DrawImGui(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs);
    // 実験場シーン（F7）：独立した窓
    void DrawWindow(Registry& reg, Entity player);
    // 実験場シーンの開始時：実験モードに入り、9x9 全部に枠を敷き、全部の道具を箱へ入れる
    void EnterLab(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs);
    // 毎フレーム（gameplay の中）：実験モードの間、経験値を 0 に保つ・MP を満たす・的のキーを読む
    void Update(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid);
    bool Enabled() const { return m_Enabled; }

private:
    void SetEnabled(Registry& reg, Entity player, SwarmSystem& swarm, MobSpawner& mobs, bool on);
    void FillFrames(Registry& reg, Entity player);
    void FillChest(Registry& reg, Entity player);   // 全部の魔法・ルーンを箱へ（足りない分だけ足す）
    void SpawnTargets(Registry& reg, Entity player, SwarmSystem& swarm, const GridWorld& grid, int pattern);
    void DrawBody(Registry& reg, Entity player);
    void DrawStats(Registry& reg, Entity player);

    static constexpr int kCopies = 3;   // 箱に入れる各道具の数

    bool  m_Enabled = false;
    bool  m_WasInvincible = false;
    float m_WasManaMax = 100.0f;
    bool  m_WasSpawning = true;
    float m_TargetHp = 1.0e6f;
    char  m_Message[96] = "";   // 最後の操作の結果
};
