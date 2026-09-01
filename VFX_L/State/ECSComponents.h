// ============================================================
// ECSComponents.h
// ECS 用 Component 定義（純データ）
// ============================================================
#pragma once

// 状態 ID（カウンター状態機のサンプル用）
enum class CounterStateID
{
    Low,    // カウント増加中
    High,   // カウント減少中
    ROOT    // 番兵（フラット構成）
};

// Component①：状態機データ（pool に入る、Entity ごと）
struct StateMachineComponent
{
    CounterStateID current = CounterStateID::Low;
    float timeInState = 0.0f;
};

// Component②：業務データ（状態機が操作する対象）
struct CounterComponent
{
    int count = 0;
    int threshold = 100;
};