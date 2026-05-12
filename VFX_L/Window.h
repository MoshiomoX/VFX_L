#pragma once
#include <Windows.h>
#include <iostream>
class Window
{
public:
	//创建窗口
    bool Create(int width, int height, const wchar_t* title);
	//处理窗口消息
    bool ProcessMessage();
	//获取窗口句柄
    HWND GetHandle() const { return m_hWnd; }
	//获取窗口宽度
    int GetWidth() const { return m_Width; }
	//获取窗口高度
    int GetHeight() const { return m_Height; }

private:
	//窗口句柄
    HWND m_hWnd = nullptr;
    int m_Width = 0;
    int m_Height = 0;
	//窗口过程函数
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
};
