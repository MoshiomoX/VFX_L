// ============================================================
// TitleScene.h
// タイトル画面。背景はデバッググリッドの上をカメラがゆっくり回り、
// 手前を道具の色の四角が漂う。
// メニュー（はじめる / ゲームを終了）は MenuList（一時停止のメニューと共用）。
// 文字とスプライトは GameUI と同じ SpriteRenderer / TextRenderer を使う。
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/MenuList.h"
#include <memory>
#include <vector>

class Texture;

class TitleScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    // 漂う四角（飾り）
    struct Deco
    {
        DirectX::SimpleMath::Vector2 pos;
        DirectX::SimpleMath::Vector4 color;
        float size = 20.0f;
        float speed = 30.0f;    // 上へ（px/s）
        float angle = 0.0f;
        float spin = 0.5f;      // rad/s
    };

    void Layout();
    void InitDecos();

    CameraBase     m_Camera;
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_WhiteTex;   // 暗幕・下線・四角用の 1x1
    MenuList       m_Menu;
    std::vector<Deco> m_Decos;

    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_Time = 0.0f;        // カメラ回転用
    bool  m_Starting = false;   // 決定後の二重送信防止
};
