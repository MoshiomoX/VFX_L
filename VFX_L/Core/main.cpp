#include "Core/Application.h"

#include <ShellScalingApi.h>
#pragma comment(lib, "Shcore.lib")

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    // Assets / Shader are looked up relative to the working directory: always run from the exe's folder
    // (started from VS with another working directory, a shortcut, etc. used to fail to find them. 2026-10-07)
    {
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
            SetCurrentDirectoryW(std::filesystem::path(exePath).parent_path().c_str());
    }

    // ???????????? DPI ????????
    //   150% ????????????Windows ????????????
    //   ?????????????????????(ImGui ????????????)?
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

#ifndef VFXL_DEMO
    // Log console for development (the Demo build has none; Core/DevUI.h)
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONIN$", "r", stdin);
#endif

    std::cout << "=== VFX Engine ===" << std::endl;

    Application app;
    if (!app.Initialize())
    {
        std::cout << "[Error] Main Initialize failed" << std::endl;
#ifdef VFXL_DEMO
        MessageBoxW(nullptr, L"Failed to start. Please check that the Assets folder is next to the exe.", L"VFX_L", MB_OK | MB_ICONERROR);
#else
        std::cin.get();
#endif
        return -1;
    }

    app.Run();
    app.Shutdown();

    return 0;
}