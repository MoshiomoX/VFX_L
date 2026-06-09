// ============================================================
// CounterStates.h
// 各状態の振る舞い（宣言）
// ============================================================
#pragma once
#include "ECSComponents.h"
#include <optional>

struct CounterState_Low
{
    static std::optional<CounterStateID> Update(
        StateMachineComponent& ctx, CounterComponent& counter, float dt);
};

struct CounterState_High
{
    static std::optional<CounterStateID> Update(
        StateMachineComponent& ctx, CounterComponent& counter, float dt);
};