// ============================================================
// InputMap.h
// 入力マッピング層：物理入力 → ゲーム動作 の翻訳。
// gameplay は「どのキー/スティックか」を知らず、動作だけを問い合わせる。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include "Manager/InputManager.h"

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

    // 詠唱（トリガー：押した瞬間のみ）
    inline bool GetCastTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetMouseTrigger(0) ||
            input.GetPadTrigger(XINPUT_GAMEPAD_X);
    }


    // 近くの物を使う（F、パッド B。トリガー）。報酬の箱など
    inline bool GetInteractTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger('F') ||
            input.GetPadTrigger(XINPUT_GAMEPAD_B);
    }

    // 魔力解放：3 秒間魔力を消費しない（Q、パッド Y。トリガー。2026-10-01 までは詠唱の一時停止だった）
    inline bool GetManaSurgeTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger('Q') ||
            input.GetPadTrigger(XINPUT_GAMEPAD_Y);
    }

    inline bool GetJumpTrigger()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger(VK_SPACE) ||
            input.GetPadTrigger(XINPUT_GAMEPAD_A);
    }

    // 滑り（左 Ctrl、パッド X。押している間ずっと）。
    // パッド X は手動詠唱（CastMode::Manual、デバッグ用）の撃つ操作と重なるが、普段の自動詠唱では使っていない
    inline bool GetSlideHeld()
    {
        auto& input = InputManager::Get();
        return input.GetKeyPress(VK_LCONTROL) ||
            input.GetPadPress(XINPUT_GAMEPAD_X);
    }

    // 一時停止のメニュー開閉（Esc、パッド Back。トリガー）
    inline bool GetPauseToggle()
    {
        auto& input = InputManager::Get();
        return input.GetKeyTrigger(VK_ESCAPE) ||
            input.GetPadTrigger(XINPUT_GAMEPAD_BACK);
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