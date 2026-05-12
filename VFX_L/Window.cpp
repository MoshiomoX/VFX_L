#include "Window.h"
#include "imgui.h"  
#include "InputManager.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
// 静态成员函数实现窗口过程
LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{

    // ImGuiにメッセージを渡す
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))

        return true;
    switch (msg)
    {
    case WM_DESTROY:
		// 关闭窗口时退出消息循环
        PostQuitMessage(0);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
		{   // 按下ESC键时关闭窗口
            PostQuitMessage(0);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        InputManager::Get().OnMouseMove(LOWORD(lp), HIWORD(lp));
        break;

    case WM_MOUSEWHEEL:
        InputManager::Get().OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wp) / 120.0f);
        break;
    }



	// 默认消息处理
    return DefWindowProc(hwnd, msg, wp, lp);
}

bool Window::Create(int width, int height, const wchar_t* title)
{
    m_Width = width;
    m_Height = height;
	// 注册窗口类
    WNDCLASSEX wc = {};   
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"VFXEngineWindow";

	// ウィンドウクラスの登録
    if (!RegisterClassEx(&wc))
        return false;

    // クライアント領域のサイズを正確に設定
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
        nullptr
    );

    if (!m_hWnd)
        return false;

    ShowWindow(m_hWnd, SW_SHOW);
    return true;
}

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