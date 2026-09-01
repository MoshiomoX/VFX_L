#include "Manager/InputManager.h"
#include <algorithm>
void InputManager::Initialize(HWND hwnd)
{
    m_hWnd = hwnd;
    memset(m_KeyState, 0, sizeof(m_KeyState));
    memset(m_KeyStateOld, 0, sizeof(m_KeyStateOld));
    m_FirstMouse = true;

    QueryPerformanceFrequency(&m_Frequency);   // ← 追加
    QueryPerformanceCounter(&m_LastCounter);   // ← 追加
    m_TimerInit = true;
}
void InputManager::Update()
{
    // ---- キーボード ----
    memcpy(m_KeyStateOld, m_KeyState, sizeof(m_KeyState));
    GetKeyboardState(m_KeyState);

    // ---- マウスデルタ ----
    if (m_FirstMouse) { m_MousePosOld = m_MousePos; m_FirstMouse = false; }
    m_MouseDelta.x = m_MousePos.x - m_MousePosOld.x;
    m_MouseDelta.y = m_MousePos.y - m_MousePosOld.y;
    m_MousePosOld = m_MousePos;
    m_MouseWheel = 0.0f;

    // ---- ゲームパッド ----
    m_PadStateOld = m_PadState;
    ZeroMemory(&m_PadState, sizeof(XINPUT_STATE));

    DWORD result = XInputGetState(0, &m_PadState);   // 0 = 1台目
    m_PadConnected = (result == ERROR_SUCCESS);

    // 未接続なら状態をゼロに（前フレームの残留を防ぐ）
    if (!m_PadConnected)
        ZeroMemory(&m_PadState, sizeof(XINPUT_STATE));

    // ---- 内部 dt 計測 ----
    float dt = 0.0f;
    if (m_TimerInit)
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        dt = (float)(now.QuadPart - m_LastCounter.QuadPart) / (float)m_Frequency.QuadPart;
        m_LastCounter = now;
    }

    // ---- 振動の自動停止 ----
    if (m_Vibrating)
    {
        m_VibrationTimer -= dt;
        if (m_VibrationTimer <= 0.0f)
            StopVibration();
    }


}
bool InputManager::GetKeyPress(int key) const
{
    return (m_KeyState[key] & 0x80) != 0;
}

bool InputManager::GetKeyTrigger(int key) const
{
    return (m_KeyState[key] & 0x80) && !(m_KeyStateOld[key] & 0x80);
}

bool InputManager::GetKeyRelease(int key) const
{
    return !(m_KeyState[key] & 0x80) && (m_KeyStateOld[key] & 0x80);
}

bool InputManager::GetMousePress(int button) const
{
    // 0=左(VK_LBUTTON), 1=右(VK_RBUTTON), 2=中(VK_MBUTTON)
    int vk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
    if (button < 0 || button > 2) return false;
    return (m_KeyState[vk[button]] & 0x80) != 0;
}

bool InputManager::GetMouseTrigger(int button) const
{
    int vk[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
    if (button < 0 || button > 2) return false;
    return (m_KeyState[vk[button]] & 0x80) && !(m_KeyStateOld[vk[button]] & 0x80);
}

void InputManager::OnMouseMove(int x, int y)
{
    m_MousePos.x = static_cast<float>(x);
    m_MousePos.y = static_cast<float>(y);
}

void InputManager::OnMouseWheel(float delta)
{
    m_MouseWheel = delta;
}

// ====== ゲームパッド ボタン三態 ======
bool InputManager::GetPadPress(WORD button) const
{
    if (!m_PadConnected) return false;
    return (m_PadState.Gamepad.wButtons & button) != 0;
}

bool InputManager::GetPadTrigger(WORD button) const
{
    if (!m_PadConnected) return false;
    bool now = (m_PadState.Gamepad.wButtons & button) != 0;
    bool old = (m_PadStateOld.Gamepad.wButtons & button) != 0;
    return now && !old;
}

bool InputManager::GetPadRelease(WORD button) const
{
    if (!m_PadConnected) return false;
    bool now = (m_PadState.Gamepad.wButtons & button) != 0;
    bool old = (m_PadStateOld.Gamepad.wButtons & button) != 0;
    return !now && old;
}

// ====== スティック処理（デッドゾーン + 正規化）======
Vector2 InputManager::ProcessStick(SHORT x, SHORT y, SHORT deadzone) const
{
    float fx = (float)x;
    float fy = (float)y;
    float len = std::sqrt(fx * fx + fy * fy);

    if (len < deadzone) return Vector2(0, 0);   // デッドゾーン内 → 0

    // デッドゾーン分を差し引いて 0〜1 に正規化
    float normLen = (len - deadzone) / (32767.0f - deadzone);
    normLen = (normLen > 1.0f) ? 1.0f : normLen;

    // 方向を維持したまま正規化後の長さを掛ける
    return Vector2(fx / len * normLen, fy / len * normLen);
}

Vector2 InputManager::GetPadLeftStick() const
{
    if (!m_PadConnected) return Vector2(0, 0);
    return ProcessStick(m_PadState.Gamepad.sThumbLX,
        m_PadState.Gamepad.sThumbLY,
        XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
}

Vector2 InputManager::GetPadRightStick() const
{
    if (!m_PadConnected) return Vector2(0, 0);
    return ProcessStick(m_PadState.Gamepad.sThumbRX,
        m_PadState.Gamepad.sThumbRY,
        XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
}

// ====== トリガー（0〜1）======
float InputManager::GetPadLeftTrigger() const
{
    if (!m_PadConnected) return 0.0f;
    return m_PadState.Gamepad.bLeftTrigger / 255.0f;
}

float InputManager::GetPadRightTrigger() const
{
    if (!m_PadConnected) return 0.0f;
    return m_PadState.Gamepad.bRightTrigger / 255.0f;
}

// ====== 振動 ======
void InputManager::SetVibration(float leftMotor, float rightMotor, float duration)
{
    if (!m_PadConnected) return;

    XINPUT_VIBRATION vib = {};
    vib.wLeftMotorSpeed = (WORD)(std::clamp(leftMotor, 0.0f, 1.0f) * 65535.0f);
    vib.wRightMotorSpeed = (WORD)(std::clamp(rightMotor, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(0, &vib);

    m_VibrationTimer = duration;
    m_Vibrating = true;
}

void InputManager::StopVibration()
{
    XINPUT_VIBRATION vib = {};   // 両モーター 0
    XInputSetState(0, &vib);
    m_Vibrating = false;
    m_VibrationTimer = 0.0f;
}