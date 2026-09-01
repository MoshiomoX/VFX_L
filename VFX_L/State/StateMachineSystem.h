// ============================================================
// StateMachineSystem.h
// 状態機を駆動する System（宣言）
// ============================================================
#pragma once
#include "State/StateMachine.h"     // 既存の汎用 HSM
#include "State/ECSComponents.h"

class Registry;  // 前方宣言（.cpp で View.h を include）

class StateMachineSystem
{
public:
    using SM = StateMachine<CounterStateID, CounterComponent, StateMachineComponent>;

    void Init();                           // Behavior 登録（1回）
    void Update(Registry& reg, float dt);  // 全 Entity の状態機を駆動

private:
    SM m_SM;   // System が持つ共有 StateMachine（pool には入れない）
};