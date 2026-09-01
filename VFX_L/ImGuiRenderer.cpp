#include"ImGuiRenderer.h"	
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <iostream>

bool ImguiRenderer::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!hwnd || !device || !context) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsClassic();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

      // ViewportsEnable時、ウィンドウ外でも見た目を統一
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        std::cout << "[Error] ImGui Win32 init failed" << std::endl;
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, context))
    {
        std::cout << "[Error] ImGui DX11 init failed" << std::endl;
        return false;
    }

    m_Initialized = true;
    std::cout << "[OK] ImGui initialized" << std::endl;
    return true;
}

void ImguiRenderer::Shutdown()
{
	if (!m_Initialized)
		return;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	m_Initialized = false;
	std::cout << "[OK] ImGui shutdown" << std::endl;
}
void ImguiRenderer::BeginFrame()
{
	if (!m_Initialized)
		return;
	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}
void ImguiRenderer::EndFrame()
{
	if (!m_Initialized) return;

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

	// Viewport更新（ウィンドウ外描画対応）
	ImGuiIO& io = ImGui::GetIO();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

