#pragma once
#include <Windows.h>
#include <Xinput.h>
#include <DirectXMath.h>
#include <SimpleMath.h>

#pragma comment(lib, "Xinput.lib")

using DirectX::SimpleMath::Vector2;

class InputManager
{
public:
    static InputManager& Get()
    {
        static InputManager instance;
        return instance;
    }

    void Initialize(HWND hwnd);
    void Update();

    // ====== キーボード ======
    bool GetKeyPress(int key) const;
    bool GetKeyTrigger(int key) const;
    bool GetKeyRelease(int key) const;

    // ====== マウス ======
    bool GetMousePress(int button) const;
    bool GetMouseTrigger(int button) const;
    DirectX::XMFLOAT2 GetMouseDelta() const { return m_MouseDelta; }
    float GetMouseWheel() const { return m_MouseWheel; }
    void OnMouseMove(int x, int y);
    void OnMouseWheel(float delta);
    DirectX::XMFLOAT2 GetMousePos() const { return m_MousePos; }

    // ====== ゲームパッド（XInput）======
    bool IsPadConnected() const { return m_PadConnected; }

    // ボタン三態（button は XINPUT_GAMEPAD_A 等の定数）
    bool GetPadPress(WORD button) const;
    bool GetPadTrigger(WORD button) const;
    bool GetPadRelease(WORD button) const;

    // スティック（デッドゾーン処理済み、-1〜1 で返す）
    Vector2 GetPadLeftStick() const;
    Vector2 GetPadRightStick() const;

    // トリガー（0〜1 で返す）
    float GetPadLeftTrigger() const;
    float GetPadRightTrigger() const;

    // 振動（強度 0〜1、duration 秒後に自動停止）
    void SetVibration(float leftMotor, float rightMotor, float duration);
    void StopVibration();

private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // スティック生値（short）→ デッドゾーン処理して -1〜1 に正規化
    Vector2 ProcessStick(SHORT x, SHORT y, SHORT deadzone) const;

private:
    HWND m_hWnd = nullptr;
    LARGE_INTEGER m_LastCounter = {};
    LARGE_INTEGER m_Frequency = {};
    bool m_TimerInit = false;
    // キーボード
    BYTE m_KeyState[256] = {};
    BYTE m_KeyStateOld[256] = {};

    // マウス
    DirectX::XMFLOAT2 m_MousePos = {};
    DirectX::XMFLOAT2 m_MousePosOld = {};
    DirectX::XMFLOAT2 m_MouseDelta = {};
    float m_MouseWheel = 0.0f;
    bool m_FirstMouse = true;

    // ゲームパッド
    XINPUT_STATE m_PadState = {};
    XINPUT_STATE m_PadStateOld = {};
    bool m_PadConnected = false;

    // 振動管理
    float m_VibrationTimer = 0.0f;
    bool  m_Vibrating = false;
};