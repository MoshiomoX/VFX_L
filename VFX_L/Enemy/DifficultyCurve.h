// ============================================================
// DifficultyCurve.h
// 経過時間 → 難度の表（2026-10-04 ユーザー指定：前半を上げ、6 分以降の伸びを抑える）。
//   以前は「湧く速さ・HP・ダメージが全部直線で伸びる」で、掛け合わせると二乗で伸び、
//   1〜2 分は楽すぎ・6 分以降は理不尽だった。点を並べた表にして、区間毎に形を決められるようにした。
//
//   点 = { 分, 湧く速さ（体/秒）, HP の倍率, ダメージの倍率 }。間は直線、最後の点より後は最後の区間の傾きで伸ばす。
//   ダメージは HP よりゆっくり伸ばす（ユーザー：後半は数発で溶けるのが理不尽。押されるのは数の多さで）。
//
// Enemies パネルで点を直接いじり、Assets/Data/Difficulty.json に保存できる（無ければコードの既定値）
// ============================================================
#pragma once
#include <vector>

struct DifficultyCurve
{
    struct Point
    {
        float minute = 0.0f;
        float spawnRate = 1.0f;   // 湧く速さ（体/秒）
        float hpMul = 1.0f;       // 湧いた雑魚の HP に掛ける
        float damageMul = 1.0f;   // 接触・爆発・Boss のスラムに掛ける
    };
    std::vector<Point> points;   // 分の昇順

    struct Sample
    {
        float spawnRate = 1.0f;
        float hpMul = 1.0f;
        float damageMul = 1.0f;
    };

    DifficultyCurve() { Reset(); }

    // コードの既定値に戻す
    void Reset();
    // minutes 分の値（間は直線、最初の点より前は最初の値、最後の点より後は最後の区間の傾きで伸ばす）
    Sample Evaluate(float minutes) const;

    bool Save(const char* path = nullptr) const;
    bool Load(const char* path = nullptr);

    // 表の編集・保存のボタン・曲線（nowMinutes に縦線の代わりの印）
    void DrawImGui(float nowMinutes);
};
