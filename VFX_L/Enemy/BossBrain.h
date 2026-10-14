// ============================================================
// BossBrain.h
// Boss の次の技を決める（規則版。2026-10-07、ユーザー「Boss の技を作る」。選び方は推奨の「距離 + 技ごとの冷却」）。
//   ・技ごとに冷却（使ってから cooldown 秒は選ばない）。HP が狂暴の線を切った後は冷却 × enragedCooldownMul
//   ・プレイヤーまでの距離で 近い（< nearDist）/ 中 / 遠い（> farDist）の 3 段に分け、段ごとの重みで抽選
//     （遠ければ突進・投石雨、近ければ重撃・衝撃波が出やすい）。minDist〜maxDist の外の技は選ばない
//   ・直前と同じ技は、他に選べる技がある限り選ばない
// 後で Jev（AI の判断。jev-boss-ai-plan）に替える時はこのクラスの Choose だけを差し替える
// ============================================================
#pragma once
#include <random>

enum class BossMove : int
{
    None = -1,
    Slam = 0,     // 重撃：プレイヤーの足元に赤い輪を 3 つ（2026-10-03 からの技）
    RockRain,     // 投石雨：プレイヤーの周りのばらばらな所に小さい輪、岩が落ちる（走って避ける）
    Shockwave,    // 衝撃波：Boss が地面を叩き、地面を這う光の帯が外へ広がる（跳んで避ける）
    Charge,       // 突進：道筋の輪が出た後、Boss が一直線に走る（横へ避ける）
    Summon,       // 召喚：Boss が止まって吠え、周りの小さな輪から雑魚・自爆兵が湧く（2026-10-07）
    Count
};

const char* BossMoveName(BossMove m);

class BossBrain
{
public:
    struct Rule
    {
        bool  enabled = true;
        float cooldown = 6.0f;            // 秒
        float wNear = 1.0f, wMid = 1.0f, wFar = 1.0f;   // 距離の段ごとの重み（0 = その段では出さない）
        float minDist = 0.0f, maxDist = 1.0e9f;         // この外なら選ばない（m）
    };
    Rule  rules[(int)BossMove::Count];
    float nearDist = 8.0f;
    float farDist = 16.0f;
    float enragedCooldownMul = 0.7f;

    BossBrain();
    void Reset();
    void Tick(float dt);
    // dist = Boss からプレイヤーまでの水平距離。選べる技が無ければ None（冷却待ち）
    BossMove Choose(float dist, std::mt19937& rng) const;
    void OnStarted(BossMove m, bool enraged);

    float CooldownLeft(BossMove m) const { return (m == BossMove::None) ? 0.0f : m_Cooldown[(int)m]; }
    BossMove Last() const { return m_Last; }

    void DrawImGui();

private:
    float    m_Cooldown[(int)BossMove::Count] = {};
    BossMove m_Last = BossMove::None;
};
