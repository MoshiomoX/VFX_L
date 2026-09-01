// ============================================================
// CounterStates.cpp
// 各状態の振る舞い（実装）
// ============================================================
#include "State/CounterStates.h"

// Low 状態：カウント増加。閾値超えで High へ。
std::optional<CounterStateID> CounterState_Low::Update(
    StateMachineComponent& ctx, CounterComponent& counter, float dt)
{
    counter.count += 1;

    if (counter.count >= counter.threshold)
        return CounterStateID::High;
    return std::nullopt;
}

// High 状態：カウント減少。0 以下で Low へ。
std::optional<CounterStateID> CounterState_High::Update(
    StateMachineComponent& ctx, CounterComponent& counter, float dt)
{
    counter.count -= 2;

    if (counter.count <= 0)
        return CounterStateID::Low;
    return std::nullopt;
}