#include "Manager/InputManager.h"
#include "imgui.h"
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

    // 視点操作用に、マウスの生の移動量（WM_INPUT）を受け取る。
    // RIDEV_NOLEGACY は付けない（WM_MOUSEMOVE / クリックは UI と ImGui がそのまま使う）
    RAWINPUTDEVICE rid = {};
    rid.usUsagePage = 0x01;   // Generic Desktop
    rid.usUsage = 0x02;   // Mouse
    rid.dwFlags = 0;
    rid.hwndTarget = hwnd;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
}
void InputManager::Update()
{
    // ---- キーボード ----
    memcpy(m_KeyStateOld, m_KeyState, sizeof(m_KeyState));
    GetKeyboardState(m_KeyState);

    const bool active = IsWindowActive();

    // ---- Alt の単押し ----
    // 押した時に構え、押している間に他のキー（マウスボタン含む）が押されたら取り消す。
    // 前面を離れたら取り消す（Alt+Tab で戻った時に離した扱いで立たないように）
    m_AltTap = false;
    {
        const bool altNow = (m_KeyState[VK_MENU] & 0x80) != 0;
        const bool altOld = (m_KeyStateOld[VK_MENU] & 0x80) != 0;
        if (!active)
        {
            m_AltArmed = false;
        }
        else if (altNow)
        {
            if (!altOld) m_AltArmed = true;
            for (int vk = 1; vk < 256 && m_AltArmed; ++vk)
            {
                if (vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU) continue;
                if (GetKeyTrigger(vk)) m_AltArmed = false;
            }
        }
        else
        {
            m_AltTap = altOld && m_AltArmed;
            m_AltArmed = false;
        }
    }

    // ---- マウスの捕獲（前のフレームの要求を反映）----
    const bool wasCaptured = m_Captured;
    ApplyMouseCapture(m_CaptureRequested && active);
    m_CaptureRequested = false;

    // 捕獲を始めたフレームの分は捨てる（捕獲前の動きで視点が跳ねないように）
    if (m_Captured && wasCaptured) m_LookDelta = m_RawAccum;
    else                           m_LookDelta = { 0.0f, 0.0f };
    m_RawAccum = { 0.0f, 0.0f };

    // ---- マウスデルタ ----
    if (m_FirstMouse) { m_MousePosOld = m_MousePos; m_FirstMouse = false; }
    m_MouseDelta.x = m_MousePos.x - m_MousePosOld.x;
    m_MouseDelta.y = m_MousePos.y - m_MousePosOld.y;
    m_MousePosOld = m_MousePos;
    m_MouseWheel = m_WheelAccum;   // 前の Update からのノッチ数（上 = 正）
    m_WheelAccum = 0.0f;

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

// ホイールは溜めておき、Update で 1 フレーム分として出す（マウスの移動量と同じ）。
// 以前は直接 m_MouseWheel に書いていたので、メッセージ処理の後の Update で 0 に消され、読めなかった
void InputManager::OnMouseWheel(float delta)
{
    m_WheelAccum += delta;
}

// ====== マウスの捕獲 ======
void InputManager::OnRawInput(HRAWINPUT handle)
{
    RAWINPUT raw = {};
    UINT size = sizeof(raw);
    if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
        return;
    if (raw.header.dwType != RIM_TYPEMOUSE) return;

    // 絶対座標で来る機器（ペンタブ・リモートデスクトップ）は視点操作に使わない
    if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) return;

    m_RawAccum.x += (float)raw.data.mouse.lLastX;
    m_RawAccum.y += (float)raw.data.mouse.lLastY;
}

// 前面が自分のスレッドのウィンドウ（本体か ImGui の別窓）で、最小化されていない
bool InputManager::IsWindowActive() const
{
    if (!m_hWnd || IsIconic(m_hWnd)) return false;
    const HWND fg = GetForegroundWindow();
    return fg && GetWindowThreadProcessId(fg, nullptr) == GetCurrentThreadId();
}

void InputManager::ApplyMouseCapture(bool capture)
{
    if (capture)
    {
        // ImGui の別窓が前面なら本体を前へ（視点を回す窓とキーを受ける窓を揃える）
        if (GetForegroundWindow() != m_hWnd) SetForegroundWindow(m_hWnd);

        // クライアント中央の 1px に閉じ込める（クリックが外の窓へ行かない）。
        // ClipCursor は窓の移動や他のアプリで外れることがあるので毎フレーム掛け直す
        RECT rc = {};
        GetClientRect(m_hWnd, &rc);
        POINT c = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
        ClientToScreen(m_hWnd, &c);
        const RECT clip = { c.x, c.y, c.x + 1, c.y + 1 };
        ClipCursor(&clip);
    }

    if (capture == m_Captured) return;
    m_Captured = capture;

    // ShowCursor は表示カウンタ。捕獲の出入りで 1 回ずつ呼んで釣り合わせる
    ShowCursor(capture ? FALSE : TRUE);
    if (!capture) ClipCursor(nullptr);

    // 中央に止めたカーソルの下に ImGui の窓があっても反応しないよう、捕獲中はマウスを渡さない
    if (ImGui::GetCurrentContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        if (capture) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        else         io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }
}

void InputManager::ReleaseMouseCapture()
{
    ApplyMouseCapture(false);
    m_CaptureRequested = false;
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