// ============================================================
// HUD.h
// 常時表示の HUD
//   経験値バー（最上段）、HP / MP バー（左上）、経過時間と撃破数（上の中央）、
//   魔法の欄（下の中央。冷却と MP 不足）、瀕死の赤い縁、画面外の目印（箱・精英）
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
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <SimpleMath.h>
#include "SpellID.h"

class SpriteRenderer;
class TextRenderer;
class Texture;
struct HealthComponent;
struct ManaComponent;
struct LevelComponent;
struct WandComponent;

// ============================================================
// 画面外の目印を出したい物（世界座標）。シーンが毎フレーム積む
// ============================================================
struct HUDMarker
{
    DirectX::SimpleMath::Vector3 position;
    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };
};

// ============================================================
// HUD が毎フレーム受け取る、コンポーネント以外の情報
// ============================================================
struct HUDFrameInfo
{
    float    runTime = 0.0f;       // 経過時間（止まっている間は進まない物）
    uint32_t kills = 0;            // 撃破数（GPU counter の累計）
    const WandComponent* wand = nullptr;   // 魔法の欄。null なら出さない

    // 画面外の目印。viewProj は世界 → クリップ（SimpleMath の行ベクトル順 view * proj）
    DirectX::SimpleMath::Matrix viewProj;
    std::vector<HUDMarker> markers;
};

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

    // ---- 枠（幻想 UI：古金の細い線）----
    bool  drawBorder = true;
    float borderSize = 1.0f;
    float gemSize = 9.0f;         // HP / MP バーの両端の菱形（0 = 無し）

    // ---- 配色 ----
    // 戦闘の UI は場面の HDR バッファに描かれ、トーンマップ + ガンマを通る。色は全部線形の値
    // （sRGB の見た目 c なら c^2.2。0.07 でも画面では中間の灰色になる）
    DirectX::SimpleMath::Vector4 bgColor = { 0.003f, 0.0025f, 0.005f, 0.95f };
    DirectX::SimpleMath::Vector4 hpColor = { 0.55f, 0.035f, 0.030f, 1.0f };    // 見た目 #C0342F くらい
    DirectX::SimpleMath::Vector4 mpColor = { 0.050f, 0.16f, 0.75f, 1.0f };     // 見た目 #3F6FE0 くらい
    DirectX::SimpleMath::Vector4 expColor = { 0.79f, 0.49f, 0.07f, 1.0f };     // 見た目 #E6B84D くらい
    DirectX::SimpleMath::Vector4 trailColor = { 1.00f, 0.85f, 0.60f, 0.50f };
    DirectX::SimpleMath::Vector4 borderColor = { 0.69f, 0.47f, 0.15f, 0.90f }; // 古金（UIDeco と同じ）
    DirectX::SimpleMath::Vector4 textColor = { 0.91f, 0.84f, 0.70f, 1.0f };    // 暖かい白
    DirectX::SimpleMath::Vector4 shadowColor = { 0.00f, 0.00f, 0.00f, 0.85f };

    // ---- 経過時間と撃破数（アンカーは文字の上端の中央）----
    HUDAnchor runInfo = { { 0.5f, 0.0f }, { 0.0f, 16.0f } };
    float timerScale = 0.80f;
    float killScale = 0.42f;
    float runDividerWidth = 200.0f;   // 時間と撃破数の間の細い分割線（0 = 無し）

    // ---- 魔法の欄（アンカーは欄の下端の中央）----
    // 丸い欄：暗い円 + 古金の輪 + 後ろでゆっくり回る魔法陣。冷却は円を上から暗く
    bool  showSpellBar = true;
    HUDAnchor spellBar = { { 0.5f, 1.0f }, { 0.0f, -26.0f } };
    float slotSize = 50.0f;
    float slotGap = 26.0f;            // 魔法陣が欄より大きいので広め
    float slotCircleScale = 1.5f;     // 魔法陣の直径 / 欄（0 = 無し）
    DirectX::SimpleMath::Vector4 slotBgColor = { 0.003f, 0.0025f, 0.005f, 0.98f };
    DirectX::SimpleMath::Vector4 cooldownColor = { 0.00f, 0.00f, 0.00f, 0.78f };
    DirectX::SimpleMath::Vector4 noManaColor = { 0.010f, 0.020f, 0.14f, 0.70f };   // MP が足りない時に被せる

    // ---- 瀕死の赤い縁（HP が lowHpRatio を切ったら出す。低いほど濃い）----
    bool  lowHpVignette = true;
    float lowHpRatio = 0.30f;
    float vignetteWidth = 0.10f;    // 画面短辺に対する縁の太さ
    float vignettePulse = 5.0f;     // 明滅の速さ（rad/s）
    DirectX::SimpleMath::Vector4 vignetteColor = { 0.85f, 0.05f, 0.05f, 0.55f };

    // ---- 画面外の目印（画面の縁に、その方向を指す矢印）----
    bool  showMarkers = true;
    float markerSize = 18.0f;
    float markerMargin = 40.0f;     // 画面の縁からの距離
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

    // castingPaused: プレイヤーが施法を止めている時、MP バーの下に一行出す
    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const HealthComponent& hp, const ManaComponent& mp,
        const LevelComponent& lv, bool castingPaused,
        const HUDFrameInfo& info);

    // 魔法の欄のアイコン（無ければ道具の色の四角）。GameUI が背包と同じ物を渡す
    void SetIconLookup(std::function<std::shared_ptr<Texture>(ItemID)> f) { m_IconLookup = std::move(f); }

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
    // gems: 両端に菱形を付ける（HP / MP。画面幅いっぱいの経験値バーには付けない）
    void DrawBar(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size,
        float ratio, float trailRatio,
        const DirectX::SimpleMath::Vector4& fillColor, bool gems = false);

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
    std::function<std::shared_ptr<Texture>(ItemID)> m_IconLookup;
    float m_Time = 0.0f;   // 明滅用（Update で進める）

    void DrawRunInfo(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info);
    void DrawSpellBar(SpriteRenderer& sprite,
        const WandComponent& wand, const ManaComponent& mp);
    void DrawLowHpVignette(SpriteRenderer& sprite, const HealthComponent& hp);
    void DrawMarkers(SpriteRenderer& sprite, const HUDFrameInfo& info);

    BarTrail m_HpTrail;
    BarTrail m_MpTrail;

    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;
};
