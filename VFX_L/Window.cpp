#include "Window.h"
#include "imgui.h"
#include "InputManager.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Window* Window::s_Instance = nullptr;

// ============================================================
// ウィンドウプロシージャ
// ============================================================
LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // ImGui にメッセージを渡す（Viewports 使用時は特に必須）
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
        return true;

    switch (msg)
    {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            PostQuitMessage(0);
            return 0;
        }
        if (wp == VK_F11 && s_Instance)
        {
            s_Instance->ToggleFullscreen();
            return 0;
        }
        break;

    case WM_SIZE:
        if (wp != SIZE_MINIMIZED && s_Instance)
        {
            s_Instance->m_Width = LOWORD(lp);
            s_Instance->m_Height = HIWORD(lp);
            s_Instance->m_Resized = true;   
        }
        break;

    case WM_MOUSEMOVE:
        // クライアント座標。UI の当たり判定はこれを使う。
        InputManager::Get().OnMouseMove(LOWORD(lp), HIWORD(lp));
        break;

    case WM_MOUSEWHEEL:
        InputManager::Get().OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp) / 120.0f);
        break;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

// ============================================================
// ウィンドウ作成
// ============================================================
bool Window::Create(int width, int height, const wchar_t* title)
{
    s_Instance = this;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VFXEngineWindow";

    if (!RegisterClassEx(&wc))
        return false;

    // ※まず概算サイズで作る。
    //   AdjustWindowRect は 96 DPI 前提で枠を計算するため、
    //   高 DPI 環境では実クライアント領域が要求値とズレる。
    RECT rect = { 0, 0, width, height };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    m_hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr, nullptr,
        wc.hInstance,
        nullptr);

    if (!m_hWnd)
        return false;

    // ※DPI が確定してから枠を再計算し、クライアント領域を要求値に合わせ直す
    UINT dpi = GetDpiForWindow(m_hWnd);
    RECT fix = { 0, 0, width, height };
    AdjustWindowRectExForDpi(&fix, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    SetWindowPos(m_hWnd, nullptr, 0, 0,
        fix.right - fix.left,
        fix.bottom - fix.top,
        SWP_NOMOVE | SWP_NOZORDER);

    // ※最終的な実クライアント領域を保持する
    RECT client = {};
    GetClientRect(m_hWnd, &client);
    m_Width = client.right - client.left;
    m_Height = client.bottom - client.top;

    std::cout << "[Window] requested " << width << "x" << height
        << " -> client " << m_Width << "x" << m_Height
        << " (dpi " << dpi << ")" << std::endl;

    ShowWindow(m_hWnd, SW_SHOW);
    return true;
}

// ============================================================
// メッセージ処理
// ============================================================
bool Window::ProcessMessage()
{
    MSG msg = {};
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            return false;

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}
void Window::ToggleFullscreen()
{
    if (!m_hWnd) return;

    if (!m_Fullscreen)
    {
        // ---- ウィンドウ → フルスクリーン ----
        // 復帰用に現在の状態を保存
        m_WindowedStyle = GetWindowLong(m_hWnd, GWL_STYLE);
        GetWindowRect(m_hWnd, &m_WindowedRect);

        // ※モニタの作業領域ではなく画面全体を取る（タスクバーも覆う）
        HMONITOR mon = MonitorFromWindow(m_hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(MONITORINFO) };
        GetMonitorInfo(mon, &mi);

        SetWindowLong(m_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(m_hWnd, HWND_TOP,
            mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left,
            mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER);

        m_Fullscreen = true;
        std::cout << "[Window] fullscreen ON" << std::endl;
    }
    else
    {
        // ---- フルスクリーン → ウィンドウ ----
        SetWindowLong(m_hWnd, GWL_STYLE, m_WindowedStyle);
        SetWindowPos(m_hWnd, nullptr,
            m_WindowedRect.left, m_WindowedRect.top,
            m_WindowedRect.right - m_WindowedRect.left,
            m_WindowedRect.bottom - m_WindowedRect.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_NOZORDER);

        m_Fullscreen = false;
        std::cout << "[Window] fullscreen OFF" << std::endl;
    }

    // ※SetWindowPos が WM_SIZE を発生させるので、
    //   バックバッファのリサイズは既存の m_Resized 経路で自動的に行われる
}