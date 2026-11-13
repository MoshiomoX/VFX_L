// ============================================================
// LifestealHeal.h
// 吸血：GPU の吸血の累計（SwarmSystem::ConsumeLifesteal = 吸血の印の付いた敵が失った HP）
// × ratio をプレイヤーの HP に戻す。capPerSec > 0 の時だけ、戻る速さを 1 秒あたり最大 HP のその割合までに抑え、
// 超えた分は bankSec 秒分だけ溜めておく（既定は 0 = 上限なし）。
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
    float capPerSec = 0.0f;                 // 1 秒あたり最大 HP のこの割合まで（0 = 上限なし）
    float bankSec = 1.0f;                   // 上限がある時、超えた分を溜めておける量（秒分）
    float showInterval = 0.5f;              // 数字・エフェクトの間隔

private:
    float m_Bank = 0.0f;
    float m_Shown = 0.0f;
    float m_ShowTimer = 0.0f;
    float m_Total = 0.0f;
};
