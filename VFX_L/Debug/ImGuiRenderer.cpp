#include "Debug/ImGuiRenderer.h"	
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "ResourcePaths.h"
#include "Core/VideoCapture.h"

bool ImguiRenderer::Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context)
{
    if (!hwnd || !device || !context) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsClassic();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (VideoCapture::Get().Enabled())
    {
        // 動画の書き出し：パネルを別の OS 窓にせずゲームの画面の中に描く（書き出すのはバックバッファだけ）。
        // 並びは書き出し用の ini（普段の imgui.ini は読み書きしない）
        io.IniFilename = "imgui_capture.ini";
    }
    else
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

      // ViewportsEnable時、ウィンドウ外でも見た目を統一
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 日本語の字体（2026-10-18 ユーザー「ImGui の日本語字体を加えて」「簡体字も」）：既定の ProggyClean には CJK の字形が無く、
    // デバッグパネルに日本語 / 中国語を書くと「?」になっていた。Noto Sans JP（OFL、可変フォント = 既定の Regular）を主に、
    // JP に無い字（「简」など簡体字だけの字）は Noto Sans SC を MergeMode で後ろに繋いで補う。ImGui 1.92 の動的な図集なので
    // 範囲は指定しない（使った字だけその時に作られる）。読めなければ既定の字体に戻る
    {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        if (io.Fonts->AddFontFromFileTTF(Res::Fnt::ImGuiJP, 16.0f, &cfg))
        {
            ImFontConfig sc = cfg;
            sc.MergeMode = true;
            if (!io.Fonts->AddFontFromFileTTF(Res::Fnt::ImGuiSC, 16.0f, &sc))
                std::cout << "[Warn] ImGui: Simplified Chinese fallback font not found (" << Res::Fnt::ImGuiSC << ")" << std::endl;
        }
        else
        {
            std::cout << "[Warn] ImGui: Japanese font not found (" << Res::Fnt::ImGuiJP << "), using the default font" << std::endl;
            io.Fonts->AddFontDefault();
        }
    }
    // 失敗した時は作った物を片付けてから返す（Shutdown は m_Initialized を見て何もしないので、ここで放さないと残る）
    if (!ImGui_ImplWin32_Init(hwnd))
    {
        std::cout << "[Error] ImGui Win32 init failed" << std::endl;
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, context))
    {
        std::cout << "[Error] ImGui DX11 init failed" << std::endl;
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
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

