// ============================================================
// RunResult.h
// 1 回のプレイの戦績。ゲームシーンが死亡時に書き、リザルトシーンが読む。
//
// シーンは切替のたびに作り直されるので、シーンのメンバには置けない。
// 受け渡しはこのグローバル 1 つだけに限定する（SceneManager に
// 汎用の「引数」を足すほどの物ではない）。
// ============================================================
#pragma once
#include <cstdint>

struct RunResult
{
    bool     valid = false;       // false ならまだ 1 回も遊んでいない
    float    survivedSec = 0.0f;  // 生存時間（秒）
    int      level = 1;           // 到達レベル
    uint32_t kills = 0;           // 撃破数（GPU counter の累計）
    float    expGained = 0.0f;    // 拾った経験値の合計
};

// 直近のプレイの戦績
inline RunResult g_LastRun;
