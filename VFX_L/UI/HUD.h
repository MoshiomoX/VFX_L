// ============================================================
// HUD.h
// 常時表示の HUD（経験値バー最上段、HP / MP バー左上）
//
// UIManager には入れない（モーダルではなく、入力も取らない）。
// 描画は SpriteRenderer / TextRenderer に相乗りし、
// 自前の GPU リソースは 1x1 の白テクスチャだけ。
// バーは白テクスチャを色染めして描く（LevelUpUI と同じ方式）。
//
// 調整用の数値は HUDStyle にまとめ、ImGui から触って
// Assets/Data/HUD.json へ保存する。見た目を詰めるたびに
// コードを書き換えて再コンパイルするのを避けるため。
//
// 位置は「アンカー（画面比 0..1）+ オフセット（ピクセル）」で持つ。
// 生のピクセルだけだと解像度が変わった時に配置が崩れ、
// Layout() が走るたびに ImGui の調整が消えてしまう。
// ============================================================
#pragma once
#include <d3d11.h>
#include <memory>
#include <string>
#include <SimpleMath.h>

class SpriteRenderer;
class TextRenderer;
class Texture;
struct HealthComponent;
struct ManaComponent;
struct LevelComponent;

// ============================================================
// 画面上の一点。pos = anchor * 画面サイズ + offset
// anchor(0,0) = 左上、(1,1) = 右下。右下に置きたい要素は
// anchor(1,1) + 負の offset で表す。
// ============================================================
struct HUDAnchor
{
    DirectX::SimpleMath::Vector2 anchor = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 offset = { 0.0f, 0.0f };

    DirectX::SimpleMath::Vector2 Resolve(float screenW, float screenH) const
    {
        return { anchor.x * screenW + offset.x,
                 anchor.y * screenH + offset.y };
    }
};

// ============================================================
// HUD の見た目パラメータ（純データ）
// 増える時はここに足して、DrawDebugUI と JSON の両方へ足す
// ============================================================
struct HUDStyle
{
    // ---- レイアウト ----
    HUDAnchor expBar = { { 0.0f, 0.0f }, {  0.0f,  0.0f } };
    float expBarHeight = 10.0f;
    float expBarMargin = 0.0f;    // 左右の余白。幅 = 画面幅 - margin * 2

    HUDAnchor lvText = { { 0.0f, 0.0f }, { 20.0f, 16.0f } };
    HUDAnchor hpBar = { { 0.0f, 0.0f }, { 20.0f, 44.0f } };
    HUDAnchor mpBar = { { 0.0f, 0.0f }, { 20.0f, 72.0f } };

    // HP と MP は別々に調整できる（片方だけ太くする等）
    DirectX::SimpleMath::Vector2 hpBarSize = { 260.0f, 22.0f };
    DirectX::SimpleMath::Vector2 mpBarSize = { 260.0f, 22.0f };
    float barPadding = 2.0f;      // 中身が背景の内側へ入る量

    // ---- 文字 ----
    float lvTextScale = 0.5f;
    float barTextScale = 0.45f;
    bool  centerBarText = true;   // バーの中央に寄せる
    DirectX::SimpleMath::Vector2 barTextOffset = { 8.0f, 3.0f };  // 非中央時の左上からの位置
    bool  textShadow = true;
    float textShadowOffset = 1.0f;

    // ---- ダメージ遅延バー ----
    // 減った分を残像として残し、少し遅れて追いつかせる。
    // 一撃でどれだけ持っていかれたかを目で追えるようにするため。
    bool  damageTrail = true;
    float trailDelay = 0.25f;     // 減ってから動き出すまでの秒数
    float trailSpeed = 0.9f;      // 追いつく速さ（割合 / 秒）

    // ---- 枠 ----
    bool  drawBorder = true;
    float borderSize = 1.0f;

    // ---- 配色 ----
    DirectX::SimpleMath::Vector4 bgColor = { 0.08f, 0.08f, 0.10f, 0.85f };
    DirectX::SimpleMath::Vector4 hpColor = { 0.85f, 0.25f, 0.25f, 1.0f };
    DirectX::SimpleMath::Vector4 mpColor = { 0.30f, 0.50f, 0.95f, 1.0f };
    DirectX::SimpleMath::Vector4 expColor = { 0.95f, 0.80f, 0.25f, 1.0f };
    DirectX::SimpleMath::Vector4 trailColor = { 1.00f, 1.00f, 1.00f, 0.55f };
    DirectX::SimpleMath::Vector4 borderColor = { 0.00f, 0.00f, 0.00f, 0.90f };
    DirectX::SimpleMath::Vector4 textColor = { 1.00f, 1.00f, 1.00f, 1.0f };
    DirectX::SimpleMath::Vector4 shadowColor = { 0.00f, 0.00f, 0.00f, 0.80f };
};

class HUD
{
public:
    bool Initialize(ID3D11Device* device);

    // 画面サイズを記録する（リサイズ時にも呼ぶ）
    // 実際の位置は Draw の時に HUDStyle から解決するので、
    // ImGui でいじった値がここで潰れることはない
    void Layout(float screenW, float screenH);

    // 残像の追従。描画しない間（モーダル表示中）も進める
    void Update(float dt, const HealthComponent& hp, const ManaComponent& mp);

    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const HealthComponent& hp, const ManaComponent& mp,
        const LevelComponent& lv);

    // ---- 調整用（ImGui）----
    void DrawDebugUI();

    bool SaveStyle(const char* path = nullptr) const;
    bool LoadStyle(const char* path = nullptr);
    void ResetStyle();

    HUDStyle& Style() { return m_Style; }
    const HUDStyle& Style() const { return m_Style; }

private:
    // 減った分だけ遅れて追いつく値（0..1）
    struct BarTrail
    {
        float value = 1.0f;
        float timer = 0.0f;

        void Update(float dt, float ratio, const HUDStyle& style);
    };

    // 背景 → 残像 → 中身 → 枠 の順で1本のバーを描く
    // ratio / trailRatio は 0..1 に丸めて渡すこと
    void DrawBar(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size,
        float ratio, float trailRatio,
        const DirectX::SimpleMath::Vector4& fillColor);

    void DrawBorder(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size);

    // 影付きの一行。影を切ると 1 回分の Draw で済む
    void DrawLabel(TextRenderer& text, const std::wstring& str,
        const DirectX::SimpleMath::Vector2& pos, float scale);

    // バーの中に重ねる文字（centerBarText で中央寄せ）
    void DrawBarLabel(TextRenderer& text, const std::wstring& str,
        const DirectX::SimpleMath::Vector2& barPos,
        const DirectX::SimpleMath::Vector2& barSize);

    HUDStyle m_Style;

    std::shared_ptr<Texture> m_WhiteTex;

    BarTrail m_HpTrail;
    BarTrail m_MpTrail;

    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;
};
