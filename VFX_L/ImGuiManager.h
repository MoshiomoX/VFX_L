#pragma once
#include <d3d11.h>

class ImGuiManager
{
public:
	static ImGuiManager& Get()
	{
		static ImGuiManager instance;
		return instance;
	}
	bool Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
	void Shutdown();
	void BeginFrame();
	void EndFrame();

private:
	ImGuiManager() = default;
	~ImGuiManager() = default;

	ImGuiManager(const ImGuiManager&) = delete;
	ImGuiManager& operator=(const ImGuiManager&) = delete;

private:
	bool m_Initialized = false;

};