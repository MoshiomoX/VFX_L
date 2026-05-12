#include "Application.h"

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
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