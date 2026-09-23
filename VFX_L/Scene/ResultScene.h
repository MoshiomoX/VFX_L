// ============================================================
// ResultScene.h
// リザルト画面。g_LastRun（RunResult.h）を表示するだけ。
// Enter / Space / パッド A でもう一度、BackSpace / パッド B でタイトルへ。
// （Esc はアプリ終了に取られている）
// 行は上から順に少しずつ出す（一気に出すと数字を読む前に終わる）。
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include <memory>
#include <string>

class Texture;

class ResultScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    // ラベルと値の 1 行
    struct Row
    {
        std::wstring label;
        std::wstring value;
    };

    void BuildRows();

    CameraBase     m_Camera;
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_WhiteTex;

    Row   m_Rows[4];
    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_Time = 0.0f;
    bool  m_Leaving = false;
};
