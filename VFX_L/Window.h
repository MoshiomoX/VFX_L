#pragma once
#include <Windows.h>
#include <iostream>

class Window
{
public:
    // ウィンドウ作成（width/height はクライアント領域の希望値）
    bool Create(int width, int height, const wchar_t* title);

    // メッセージ処理。false を返したら終了
    bool ProcessMessage();

    HWND GetHandle() const { return m_hWnd; }

    // ※実際のクライアント領域サイズ。
    //   swap chain / ビューポート / UI は全部これを基準にする。
    int GetWidth()  const { return m_Width; }
    int GetHeight() const { return m_Height; }
    void ToggleFullscreen();
    bool IsFullscreen() const { return m_Fullscreen; }

    bool ConsumeResizeFlag()
    {
        bool r = m_Resized;
        m_Resized = false;
        return r;
    }
private:
    HWND m_hWnd = nullptr;
    int  m_Width = 0;
    int  m_Height = 0;
    bool m_Resized = false;
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    
    bool m_Fullscreen = false;

    // ウィンドウ復帰用に元の状態を保存する
    RECT  m_WindowedRect = {};
    DWORD m_WindowedStyle = 0;
    // WndProc は static なのでインスタンスへ戻る手段が必要
    static Window* s_Instance;
};