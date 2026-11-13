#pragma once
// ============================================================
// CliffSteer.h
// 地面を動く旋風（竜巻・砂旋・魔神の砂旋）が崖を登らないための向き直し。
// 前方 kProbe m までを kSamples 点調べ、どこかで「距離に比例した上り（kProbe で kMaxRise）」を超えれば崖とみなし、
// 左右へ順に向きを振って登らない向きを選ぶ（崖沿いに進む）。下りは構わない。
// 近い点も見るのは、崖の縁の急な斜面（高さ図の 1 マス）を小刻みに這い上がらないため。
// さらに上る先の地面の傾きが kMaxSlope を超えれば崖とする（斜面を斜めに横切ってジグザグに登らないため）。
// GPU の砂旋（SwarmEnemyAICS の SWARM_TORNADO_*）も同じ値・同じ順で調べる
// ============================================================
#include <SimpleMath.h>
#include <cmath>

namespace CliffSteer
{
    constexpr float kProbe = 0.6f;     // 崖を調べる前方の距離（m）
    constexpr float kMaxRise = 0.5f;   // その間にこれ以上上がる所は崖
    constexpr int   kSamples = 4;      // 前方を調べる点の数（等間隔）
    constexpr float kMaxStep = 0.2f;   // 1 回に進んでよい距離（m）。速い物はこれで刻んで調べ直す
    constexpr float kMaxSlope = 1.0f;  // 上る先の地面の傾き（高さ / 水平）がこれを超えれば崖（45°。坂道・丘はこれより緩い）
    constexpr float kMinRise = 0.05f;  // これ以下の上りは傾きを見ない（平らな崖の足元を沿って進めるように）
    constexpr float kSlopeStep = 0.25f;// 傾きを測る差分の幅（m）
    // 試す向き（今の向きからの角度、度）。最後の 180 = 引き返す
    constexpr float kTurnsDeg[] = { 35.0f, -35.0f, 70.0f, -70.0f, 105.0f, -105.0f, 140.0f, -140.0f, 180.0f };

    // pos から dir（水平の単位ベクトル）へ進むと崖を登るか。hHere = pos の地面の高さ
    template <class HeightFn>
    bool Blocked(const DirectX::SimpleMath::Vector3& pos, float hHere, const DirectX::SimpleMath::Vector3& dir, const HeightFn& height)
    {
        for (int k = 1; k <= kSamples; ++k)
        {
            const float f = (float)k / (float)kSamples;
            const DirectX::SimpleMath::Vector3 ahead = pos + dir * (kProbe * f);
            const float rise = height(ahead.x, ahead.z) - hHere;
            if (rise > kMaxRise * f) return true;
            // 上る先の地面そのものが急（崖の縁の斜面）なら、斜めに横切っても登らない
            if (rise > kMinRise)
            {
                const float gx = (height(ahead.x + kSlopeStep, ahead.z) - height(ahead.x - kSlopeStep, ahead.z)) / (2.0f * kSlopeStep);
                const float gz = (height(ahead.x, ahead.z + kSlopeStep) - height(ahead.x, ahead.z - kSlopeStep)) / (2.0f * kSlopeStep);
                if (gx * gx + gz * gz > kMaxSlope * kMaxSlope) return true;
            }
        }
        return false;
    }

    // dir が崖に向いていたら登らない向きへ直す。allowReverse = false なら引き返さず、どこも塞がっていれば false を返す
    template <class HeightFn>
    bool Steer(DirectX::SimpleMath::Vector3& dir, const DirectX::SimpleMath::Vector3& pos, float hHere, const HeightFn& height,
        bool allowReverse = true)
    {
        if (!Blocked(pos, hHere, dir, height)) return true;
        const float cur = std::atan2(dir.x, dir.z);
        for (float deg : kTurnsDeg)
        {
            if (deg == 180.0f && !allowReverse) break;
            const float ang = cur + DirectX::XMConvertToRadians(deg);
            const DirectX::SimpleMath::Vector3 d(std::sin(ang), 0.0f, std::cos(ang));
            if (!Blocked(pos, hHere, d, height) || deg == 180.0f)
            {
                dir = d;
                return true;
            }
        }
        return false;
    }
}
