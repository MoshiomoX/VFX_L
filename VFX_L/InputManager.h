#pragma once
#include <Windows.h>
#include <DirectXMath.h>

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

    // キーボード
    bool GetKeyPress(int key) const;
    bool GetKeyTrigger(int key) const;
    bool GetKeyRelease(int key) const;

    // マウスボタン（0=左, 1=右, 2=中）
    bool GetMousePress(int button) const;
    bool GetMouseTrigger(int button) const;

    // マウス移動量・スクロール
    DirectX::XMFLOAT2 GetMouseDelta() const { return m_MouseDelta; }
    float GetMouseWheel() const { return m_MouseWheel; }

    // WndProcから呼ぶ
    void OnMouseMove(int x, int y);
    void OnMouseWheel(float delta);

private:
    InputManager() = default;
    ~InputManager() = default;
    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

private:
    HWND m_hWnd = nullptr;

    // キーボード
    BYTE m_KeyState[256] = {};
    BYTE m_KeyStateOld[256] = {};

    // マウス
    DirectX::XMFLOAT2 m_MousePos = {};
    DirectX::XMFLOAT2 m_MousePosOld = {};
    DirectX::XMFLOAT2 m_MouseDelta = {};
    float m_MouseWheel = 0.0f;
    bool m_FirstMouse = true;
};