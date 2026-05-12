#pragma once
#include <d3d11.h>
#include <wrl/client.h> //com_ptr

#pragma comment(lib, "d3d11.lib")	
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

class Graphics
{
public:
	bool Initialize(HWND hWnd, int width, int height);
	void BeginFrame();
	void EndFrame();
	void Shutdown();

	ID3D11Device* GetDevice()const{ return m_Device.Get(); }
	ID3D11DeviceContext* GetContext()const { return m_Context.Get(); }

private:
	//=======================================
	// Direct3D 11の主要コンポーネント
	//=======================================
	ComPtr<ID3D11Device> m_Device;// デバイス
	ComPtr<ID3D11DeviceContext> m_Context;// デバイスコンテキスト
	ComPtr<IDXGISwapChain> m_SwapChain;// スワップチェーン
	ComPtr<ID3D11RenderTargetView> m_RenderTargetView;// レンダーターゲットビュー
	ComPtr<ID3D11DepthStencilView> m_DepthStencilView;// デプスステンシルビュー

	D3D11_VIEWPORT m_Viewport = {};// ビューポート
};