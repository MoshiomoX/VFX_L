// ============================================================
// StateMachineSystem.cpp
// 状態機を駆動する System（実装）
// ============================================================
#include "State/StateMachineSystem.h"
#include "State/CounterStates.h"
#include "ECS/View.h"        // Registry + View（実装で必要なのでここで include）
#include <iostream>

void StateMachineSystem::Init()
{
    m_SM.SetRoot(CounterStateID::ROOT);

    // { parent, onEnter, onUpdateAlways, onUpdate, onExit }
    m_SM.RegisterState(CounterStateID::Low,
        { CounterStateID::ROOT, nullptr, nullptr, &CounterState_Low::Update, nullptr });
    m_SM.RegisterState(CounterStateID::High,
        { CounterStateID::ROOT, nullptr, nullptr, &CounterState_High::Update, nullptr });

    // 状態切替の通知（デバッグ用）
    m_SM.SetOnStateChanged([](CounterStateID from, CounterStateID to) {
        std::cout << "    [transition] " << (int)from << " -> " << (int)to << "\n";
        });
}

void StateMachineSystem::Update(Registry& reg, float dt)
{
    // StateMachineComponent と CounterComponent 両方持つ Entity を回す
    reg.CreateView<StateMachineComponent, CounterComponent>()
        .Each([this, dt](Entity e, StateMachineComponent& sc, CounterComponent& cc) {
        // System が StateMachine（道具）を呼ぶ。sc=状態データ, cc=業務データ
        m_SM.Update(sc, cc, dt);
            });
}