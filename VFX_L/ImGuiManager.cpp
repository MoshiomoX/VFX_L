#include"ImGuiManager.h"	
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include <iostream>

bool ImGuiManager::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{

	if (!hwnd || !device || !context) return false;

	// Initialize ImGui for Win32 and DirectX11
	IMGUI_CHECKVERSION();	
	ImGui::CreateContext();

	//スタイルを確認
	ImGui::StyleColorsClassic();
	
	ImGuiIO& io = ImGui::GetIO();
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

void ImGuiManager::Shutdown()
{
	if (!m_Initialized)
		return;

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	m_Initialized = false;
	std::cout << "[OK] ImGui shutdown" << std::endl;
}
void ImGuiManager::BeginFrame()
{
	if (!m_Initialized)
		return;

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiManager::EndFrame()
{
	if (!m_Initialized)
		return;

	ImGui::Render();
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}


