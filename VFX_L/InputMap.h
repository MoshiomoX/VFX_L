// ============================================================
// InputMap.h
// 入力マッピング層：物理入力 → ゲーム動作 の翻訳。
// gameplay は「どのキー/スティックか」を知らず、動作だけを問い合わせる。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include "InputManager.h"

namespace InputMap
{
    using DirectX::SimpleMath::Vector2;

    // 移動入力（-1〜1）。キーボード WASD とパッド左スティックを合成。
    // x = 左右、y = 前後
    inline Vector2 GetMoveInput()
    {
        auto& input = InputManager::Get();
        Vector2 move(0, 0);

        // --- キーボード WASD ---
        if (input.GetKeyPress('A')) move.x -= 1.0f;
        if (input.GetKeyPress('D')) move.x += 1.0f;
        if (input.GetKeyPress('S')) move.y -= 1.0f;
        if (input.GetKeyPress('W')) move.y += 1.0f;

        // 斜め入力が速くならないよう正規化
        if (move.LengthSquared() > 1.0f)
            move.Normalize();

        // --- パッド左スティック（入力があれば優先）---
        Vector2 stick = input.GetPadLeftStick();
        if (stick.LengthSquared() > 0.0f)
            move = stick;

        return move;
    }

    // 施法（トリガー：押した瞬間のみ）
    inline bool GetCastTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetMouseTrigger(0) ||
            input.GetPadTrigger(XINPUT_GAMEPAD_X);
    }


    inline bool GetJumpTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger(VK_SPACE) ||
            input.GetPadTrigger(XINPUT_GAMEPAD_A);
    }

    // バックパック開閉（Tab / I / E、パッド Start）
    // ※Trigger を使う。Press だと毎フレーム反転して点滅する
    inline bool GetBackpackToggle()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger(VK_TAB)
            || input.GetKeyTrigger('I')
            || input.GetKeyTrigger('E')
            || input.GetPadTrigger(XINPUT_GAMEPAD_START);
    }
}