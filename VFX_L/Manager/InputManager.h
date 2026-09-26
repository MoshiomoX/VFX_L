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

    // ====== マウスの捕獲（TPS の視点操作）======
    // 捕獲中はカーソルを隠してウィンドウ中央の 1px に閉じ込め、ImGui にもマウスを渡さない。
    // 毎フレーム RequestMouseCapture を呼んでいる間だけ有効で、呼ばなくなった次のフレームで放す
    // （場面を抜けた時に戻し忘れて、タイトルでカーソルが消えたままになるのを防ぐ）。
    // ウィンドウが前面に無い間は、要求があっても放しておく
    void RequestMouseCapture() { m_CaptureRequested = true; }
    bool IsMouseCaptured() const { return m_Captured; }
    void ReleaseMouseCapture();   // 即座に放す（非アクティブ化・破棄の時）
    // 捕獲中の相対移動（Raw Input のカウント、1 フレーム分の合計）。捕獲していない時は 0。
    // 加速の掛からない生の値なので、カーソルが画面端に着いても止まらない
    DirectX::XMFLOAT2 GetMouseLookDelta() const { return m_LookDelta; }
    void OnRawInput(HRAWINPUT handle);   // WM_INPUT から

    // Alt の単押し：押してから離すまでに他のキー・マウスボタンが押されなかった時、離したフレームだけ true。
    // Alt+Tab / Alt+F4 / Alt+クリック（デバッグカメラ）では立たない
    bool GetAltTap() const { return m_AltTap; }

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

    // マウスの捕獲
    void ApplyMouseCapture(bool capture);
    bool IsWindowActive() const;
    bool m_CaptureRequested = false;   // 今フレーム要求された（次の Update で反映して下ろす）
    bool m_Captured = false;
    DirectX::XMFLOAT2 m_RawAccum = {};    // WM_INPUT の合計（Update で取り出す）
    DirectX::XMFLOAT2 m_LookDelta = {};

    // Alt の単押し
    bool m_AltArmed = false;
    bool m_AltTap = false;

    // ゲームパッド
    XINPUT_STATE m_PadState = {};
    XINPUT_STATE m_PadStateOld = {};
    bool m_PadConnected = false;

    // 振動管理
    float m_VibrationTimer = 0.0f;
    bool  m_Vibrating = false;
};