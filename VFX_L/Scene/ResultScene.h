// ============================================================
// ResultScene.h
// 1 面の終わりの戦績（2026-10-09 作り直し。ユーザー：「リザルトが不格好」、背景は「エフェクトの爆発の一コマ」）。
//   背景：戦闘で写した爆発の山場（SceneSnapshot。無ければ終わった瞬間）。左を暗くして文字を載せる
//   左上：見出し（ステージクリア / 力尽きた）+ 面の名前
//   左：戦績の表（生存時間・レベル・撃破数・金貨・経験値・宝箱。数字は 0 から数え上がる）
//   その右：今回のビルド（終わった時のバックパック。BackpackUI の描き方そのまま、触れない）
//   下：次へ / タイトルへの 2 つのボタン（キー・パッド・マウス）
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "UI/BackpackUI.h"
#include <SimpleMath.h>
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
    // 表の 1 行。value = 数え上げの終わりの値。time = mm:ss で見せる
    struct Row
    {
        std::wstring label;
        float value = 0.0f;
        bool  time = false;
        bool  valid = true;   // false = 「--」
    };
    static constexpr int kRowCount = 6;

    // 下のボタン（0 = 次へ / もう一度、1 = タイトルへ）
    struct Button
    {
        std::wstring label;
        std::wstring key;
        DirectX::SimpleMath::Vector2 pos;   // 下敷きの左上
        DirectX::SimpleMath::Vector2 size;
    };

    void BuildRows();
    void Layout();
    void Leave(int button);
    std::wstring FormatValue(const Row& r, float shown) const;

    CameraBase     m_Camera;
    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_White;
    BackpackUI     m_BackpackView;   // 終わった時のバックパック（描くだけ）

    Row    m_Rows[kRowCount];
    Button m_Buttons[2];
    int    m_Hover = -1;             // マウスが乗っているボタン
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };

    // レイアウト（Layout で画面から決める）
    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_K = 1.0f;                // 文字・余白の倍率
    bool m_LayoutPad = false;        // Layout した時の機器（ボタンのキーの絵の幅が違う）
    DirectX::SimpleMath::Vector2 m_StatsPos, m_StatsSize;
    DirectX::SimpleMath::Vector2 m_BagPos;   // 外枠の左上
    float  m_BagExtent = 0.0f;

    float m_Time = 0.0f;
    bool  m_Leaving = false;
};
