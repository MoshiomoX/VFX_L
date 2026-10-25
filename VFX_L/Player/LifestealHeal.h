// ============================================================
// LifestealHeal.h
// 吸血のルーン（2026-10-25）：GPU の吸血の累計（SwarmSystem::ConsumeLifesteal = 吸血の印の付いた敵が失った HP）
// × ratio をプレイヤーの HP に戻す。戻る速さは 1 秒あたり最大 HP の capPerSec まで（ユーザーが選んだ推奨の
// 「ダメージの割合 3%・上限 2% / 秒」）。上限を超えた分は bankSec 秒分だけ溜めておき、それ以上は捨てる
// （群れに撃ち込み続けても一瞬で満タンにならない・後から一気に戻らない）。
// 見た目：回復した量を showInterval 秒毎にまとめて緑の「+n」、同じ間隔で赤い吸収のエフェクト（シーンが出す）
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"

struct HealthComponent;

class LifestealHeal
{
public:
    struct Result
    {
        float healed = 0.0f;     // このフレームに戻した HP
        float showAmount = 0.0f; // > 0 = 数字を出す（まとめた量）
        bool  playFx = false;    // 吸収のエフェクトを出す
    };

    // stolen = 前回からの吸血の累計の差。alive でなければ溜めた分も捨てる
    Result Update(float dt, float stolen, HealthComponent& hp, bool alive);
    void Reset() { m_Bank = 0.0f; m_Shown = 0.0f; m_ShowTimer = 0.0f; }
    float Total() const { return m_Total; }   // 自動テスト用の累計
    void DrawImGui();

    float ratio = Swarm::kLifestealRatio;   // 与えたダメージのこの割合
    float capPerSec = 0.02f;                // 1 秒あたり最大 HP のこの割合まで
    float bankSec = 1.0f;                   // 上限を超えた分を溜めておける量（秒分）
    float showInterval = 0.5f;              // 数字・エフェクトの間隔

private:
    float m_Bank = 0.0f;
    float m_Shown = 0.0f;
    float m_ShowTimer = 0.0f;
    float m_Total = 0.0f;
};
