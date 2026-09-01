#include "Application.h"
#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // ※ウィンドウ作成より前に DPI 対応を宣言する。
    //   150% 等のスケーリング環境で、Windows の仮想スケーリングにより
    //   報告される座標と実ピクセルがズレるのを防ぐ（ImGui のクリック位置ズレの原因）。
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONIN$", "r", stdin);

    std::cout << "=== VFX Engine ===" << std::endl;

    Application app;
    if (!app.Initialize())
    {
        std::cout << "[Error] Main Initialize failed" << std::endl;
        std::cin.get();
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}