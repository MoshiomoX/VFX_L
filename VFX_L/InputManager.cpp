#include "InputManager.h"

void InputManager::Initialize(HWND hwnd)
{
    m_hWnd = hwnd;
    memset(m_KeyState, 0, sizeof(m_KeyState));
    memset(m_KeyStateOld, 0, sizeof(m_KeyStateOld));
    m_FirstMouse = true;
}

void InputManager::Update()
{
    // 前フレームの入力を保存
    memcpy(m_KeyStateOld, m_KeyState, sizeof(m_KeyState));

    // キーボード状態を取得
    GetKeyboardState(m_KeyState);

    // マウスデルタを計算
    if (m_FirstMouse)
    {
        m_MousePosOld = m_MousePos;
        m_FirstMouse = false;
    }

    m_MouseDelta.x = m_MousePos.x - m_MousePosOld.x;
    m_MouseDelta.y = m_MousePos.y - m_MousePosOld.y;
    m_MousePosOld = m_MousePos;

    // スクロールは1フレームだけ有効、次フレームでリセット
    m_MouseWheel = 0.0f;
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