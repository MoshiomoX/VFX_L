// ============================================================
// TitleScene.h
// タイトル画面。背景はデバッググリッドの上をカメラがゆっくり回るだけ。
// Enter / Space / パッド A でゲームへ。
// 文字とスプライトは GameUI と同じ SpriteRenderer / TextRenderer を使う。
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include <memory>

class Texture;

class TitleScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    CameraBase     m_Camera;
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_WhiteTex;   // 暗幕・下線用の 1x1

    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_Time = 0.0f;        // 点滅とカメラ回転用
    bool  m_Starting = false;   // 決定後の二重送信防止
};
