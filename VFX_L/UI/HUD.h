// ============================================================
// HUD.h
// 常時表示の HUD（経験値バー最上段、HP / MP バー左上）
//
// UIManager には入れない（モーダルではなく、入力も取らない）。
// 描画は SpriteRenderer / TextRenderer に相乗りし、
// 自前の GPU リソースは 1x1 の白テクスチャだけ。
// バーは白テクスチャを色染めして描く（LevelUpUI と同じ方式）。
// ============================================================
#pragma once
#include <d3d11.h>
#include <memory>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct HealthComponent;
struct ManaComponent;
struct LevelComponent;

class HUD
{
public:
    bool Initialize(ID3D11Device* device);

    // 画面サイズからレイアウトを再計算（リサイズ時にも呼ぶ）
    void Layout(float screenW, float screenH);

    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const HealthComponent& hp, const ManaComponent& mp,
        const LevelComponent& lv);

private:
    // 背景 + 中身の2枚でバーを描く（ratio は 0..1 に丸め済みで渡す）
    void DrawBar(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size,
        float ratio,
        const DirectX::SimpleMath::Vector4& fillColor);

    std::shared_ptr<Texture> m_WhiteTex;

    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;

    // ---- レイアウト（Layout で確定）----
    DirectX::SimpleMath::Vector2 m_ExpBarPos = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_ExpBarSize = { 1920.0f, 10.0f };
    DirectX::SimpleMath::Vector2 m_LvTextPos = { 20.0f, 16.0f };
    DirectX::SimpleMath::Vector2 m_HpBarPos = { 20.0f, 44.0f };
    DirectX::SimpleMath::Vector2 m_MpBarPos = { 20.0f, 72.0f };
    DirectX::SimpleMath::Vector2 m_BarSize = { 260.0f, 22.0f };

    // ---- 配色 ----
    DirectX::SimpleMath::Vector4 m_BgColor = { 0.08f, 0.08f, 0.10f, 0.85f };
    DirectX::SimpleMath::Vector4 m_HpColor = { 0.85f, 0.25f, 0.25f, 1.0f };
    DirectX::SimpleMath::Vector4 m_MpColor = { 0.30f, 0.50f, 0.95f, 1.0f };
    DirectX::SimpleMath::Vector4 m_ExpColor = { 0.95f, 0.80f, 0.25f, 1.0f };
    DirectX::SimpleMath::Vector4 m_TextColor = { 1.0f, 1.0f, 1.0f, 1.0f };
};
