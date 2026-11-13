// ============================================================
// DifficultyCurve.h
// 経過時間 → 難度の表。点を並べ、区間毎に形を決められる。
//
//   点 = { 分, 湧く速さ（体/秒）, HP の倍率, ダメージの倍率 }。間は直線、最後の点より後は最後の区間の傾きで伸ばす。
//   ダメージは HP よりゆっくり伸ばす（後半に数発で溶けないように。圧は数の多さで出す）。
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

    // 表の編集・保存のボタン・曲線。nowMinutes = 表で今読んでいる分（面の進め分込み）、
    // offsetMinutes = 面の進め分（曲線は表の offsetMinutes〜+12 分 = その面の 0〜12 分を描く）
    void DrawImGui(float nowMinutes, float offsetMinutes = 0.0f);
};
