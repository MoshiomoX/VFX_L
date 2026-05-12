#pragma once
#include <d3d11.h>

class ImguiRenderer
{
public:
    ImguiRenderer() = default;
    ~ImguiRenderer() = default;

    // コピー禁止（ImGuiコンテキストは共有不可）
    ImguiRenderer(const ImguiRenderer&) = delete;
    ImguiRenderer& operator=(const ImguiRenderer&) = delete;

    bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void BeginFrame();
    void EndFrame();

private:
    bool m_Initialized = false;
};