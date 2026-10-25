// ============================================================
// VFXStates.cpp
// 各状態の実装（既存 VFXEffect のメソッドを呼ぶだけ）
// ============================================================
#include "VFX_Editor/VFXStates.h"
#include "VFX_Editor/VFXEffect.h"
#include "Particle/GPUParticleSystem.h"

// ============================================================
// Idle：待機。外部から SendEvent(Playing) で遷移。
// ============================================================
void VFXState_Idle::Enter(VFXStateContext& ctx, VFXEffect& vfx) {
    // 待機状態では何もしない（再生前 or 完全停止後のリセット完了状態）
}

std::optional<VFXStateID> VFXState_Idle::Update(VFXStateContext& ctx, VFXEffect& vfx, float dt) {
    // Idle からの自動遷移はなし（外部 Event 待ち）
    return std::nullopt;
}

// ============================================================
// Playing：Entry 駆動 + 粒子発射 + 粒子更新
// Loop OFF かつ全 Entry 完了 → Finishing へ
// ============================================================
void VFXState_Playing::Enter(VFXStateContext& ctx, VFXEffect& vfx) {
    // m_CurrentTime = 0、全 Entry リセット。VFXEffect::Play が直前に同じことをしているので、
    // StateMachine の遷移の不具合が直ってここが呼ばれるようになっても二度目は何も変えない（2026-10-25 に確認）
    vfx.ResetTimeline();
}

std::optional<VFXStateID> VFXState_Playing::Update(VFXStateContext& ctx, VFXEffect& vfx, float dt) {
    vfx.AdvanceTime(dt);
    vfx.UpdateEntries(dt, *ctx.vfxCtx);
    vfx.CollectAndDispatch(dt, *ctx.vfxCtx);  // Emitter 収集 + 粒子更新（発射あり）

    // 全 Entry が Duration に到達したか
    if (vfx.IsAllEntriesDone()) {
        if (vfx.IsLooping()) {
            // Loop ON → タイムラインをリセットして Playing のまま
            vfx.ResetTimeline();
            return std::nullopt;
        }
        // Loop OFF → 発射停止へ（ここが Bug 修正の入口）
        return VFXStateID::Finishing;
    }
    return std::nullopt;
}

// ============================================================
// Finishing：発射停止、既存粒子の自然消滅を待つ
// 全粒子消滅 → Stopped へ（Loop OFF Bug の本丸）
// ============================================================
void VFXState_Finishing::Enter(VFXStateContext& ctx, VFXEffect& vfx) {
    // Entry の再生を停止（新規発射を止める）。
    // StateMachine の頂上の状態同士の遷移で Enter が呼ばれていなかった不具合が直ると、ここが初めて動く（2026-10-25 に確認）：
    // 再生中の entry に OnStop が届く = 粒子は発射を止める、Trail entry は帯を切り離してその場で縮める、
    // Mesh / Sprite / 光線 / 液溜まり / 点光源は描くのをやめる（Finishing では元々積まれないので見た目は同じ）。
    // vfxCtx は InitStateMachine で入る（雛形は Play しないのでここへ来ない）
    if (ctx.vfxCtx) vfx.StopAllEntries(*ctx.vfxCtx);
}

std::optional<VFXStateID> VFXState_Finishing::Update(VFXStateContext& ctx, VFXEffect& vfx, float dt) {
    // 発射なし、粒子更新だけ走らせる（自然消滅させる）
    vfx.DispatchUpdateOnly(dt, *ctx.vfxCtx);

    // 全粒子が消滅したら完全停止へ。
    // ただし CPU は粒子池の生存数を知らない（GPUParticleSystem::GetAliveCount は常に 1）ので、
    // この effect が出した粒子・帯が消え切る時間（GetFinishWait）を待ったら止める（2026-10-25）。
    // 以前はこの逃げ道が無く Stopped へ行けず、AreaVFXPlayer の「自分で終わったら捨てる」が働かなかった
    if (vfx.GetAliveCount(*ctx.vfxCtx) == 0 || ctx.timeInState >= vfx.GetFinishWait()) {
        return VFXStateID::Stopped;
    }
    return std::nullopt;
}

// ============================================================
// Stopped：完全停止。Idle に戻すのは外部の責任。
// ============================================================
void VFXState_Stopped::Enter(VFXStateContext& ctx, VFXEffect& vfx) {
    // 必要ならここでリソース解放
}

std::optional<VFXStateID> VFXState_Stopped::Update(VFXStateContext& ctx, VFXEffect& vfx, float dt) {
    // 停止後は何もしない
    return std::nullopt;
}

// ============================================================
// 状態テーブル登録
// ============================================================
void RegisterVFXStates(VFXStateMachine& sm) {
    sm.SetRoot(VFXStateID::ROOT);

    // { parent, onEnter, onUpdateAlways, onUpdate, onExit }
    // フラット構成：parent は全部 ROOT、onUpdateAlways は使わない
    sm.RegisterState(VFXStateID::Idle,
        { VFXStateID::ROOT, &VFXState_Idle::Enter,      nullptr, &VFXState_Idle::Update,      nullptr });
    sm.RegisterState(VFXStateID::Playing,
        { VFXStateID::ROOT, &VFXState_Playing::Enter,   nullptr, &VFXState_Playing::Update,   nullptr });
    sm.RegisterState(VFXStateID::Finishing,
        { VFXStateID::ROOT, &VFXState_Finishing::Enter, nullptr, &VFXState_Finishing::Update, nullptr });
    sm.RegisterState(VFXStateID::Stopped,
        { VFXStateID::ROOT, &VFXState_Stopped::Enter,   nullptr, &VFXState_Stopped::Update,   nullptr });
}