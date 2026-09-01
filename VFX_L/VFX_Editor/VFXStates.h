// ============================================================
// VFXStates.h
// VFXEffect 用の状態定義と振る舞い
// StateMachine にフラット登録（parent = ROOT、階層なし）
// ============================================================
#pragma once
#include "State/StateMachine.h"
#include <optional>

class VFXEffect;
struct VFXContext;

// 状態ID（フラット構成）
enum class VFXStateID {
    Idle,       // 待機（未再生）
    Playing,    // 再生中（Entry駆動 + 粒子発射 + 粒子更新）
    Finishing,  // 終了待ち（発射停止、既存粒子の自然消滅待ち）
    Stopped,    // 完全停止
    ROOT        // 番兵（階層の終端）
};

struct VFXStateContext 
{
    VFXStateID current = VFXStateID::Idle;
    float      timeInState = 0.0f;
    VFXContext* vfxCtx = nullptr;  // 業務データへの参照（particleSystem 等）
};

// 型エイリアス
using VFXStateMachine = StateMachine<VFXStateID, VFXEffect, VFXStateContext>;

// ============================================================
// 各状態の振る舞い（static メンバ関数 = 純ロジック、データ持たない）
// ============================================================

// --- 待機状態：外部から Play イベントで Playing へ ---
struct VFXState_Idle {
    static void Enter(VFXStateContext& ctx, VFXEffect& vfx);
    static std::optional<VFXStateID> Update(VFXStateContext& ctx, VFXEffect& vfx, float dt);
};

// --- 再生中：Entry 駆動 + 粒子発射 + 粒子更新 ---
struct VFXState_Playing {
    static void Enter(VFXStateContext& ctx, VFXEffect& vfx);
    static std::optional<VFXStateID> Update(VFXStateContext& ctx, VFXEffect& vfx, float dt);
};

// --- 終了待ち：発射停止、既存粒子の自然消滅を待つ ---
struct VFXState_Finishing {
    static void Enter(VFXStateContext& ctx, VFXEffect& vfx);
    static std::optional<VFXStateID> Update(VFXStateContext& ctx, VFXEffect& vfx, float dt);
};

// --- 完全停止 ---
struct VFXState_Stopped {
    static void Enter(VFXStateContext& ctx, VFXEffect& vfx);
    static std::optional<VFXStateID> Update(VFXStateContext& ctx, VFXEffect& vfx, float dt);
};

// 状態テーブル登録ヘルパー
void RegisterVFXStates(VFXStateMachine& sm);