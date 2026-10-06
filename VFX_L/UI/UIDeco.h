// ============================================================
// UIDeco.h
// 幻想 UI の飾り：暗い半透明のパネル + 細い二重線 + 四隅の組紐 + 分割線 + 魔法陣。
// 飾りの絵は全部「白い線画」（Assets/Texture/UI/Deco）で、描く時に色を掛ける。
//
// 色はアイテムの種類で決める（2026-09-28 ユーザー決定）：
//   攻撃魔法・範囲魔法 = 古金 / ルーン = 奥術青 / 拡張枠・能力アップ = 月銀
//   HUD・メニューなど、どの魔法にも属さない所 = 古金
// ============================================================
#pragma once
#include "Item/ItemTypes.h"
#include <SimpleMath.h>
#include <memory>

class SpriteRenderer;
class Texture;

namespace UIDeco
{
    enum class Tint { Gold, Silver, Arcane, Violet };   // Violet = 召喚物（水晶玉。2026-10-06）

    // 線の色（a = 1）。UI は線形で合成されるので、sRGB の見た目の色を線形へ直した値
    DirectX::SimpleMath::Vector4 TintColor(Tint t);
    Tint TintFor(ItemCategory c);
    inline DirectX::SimpleMath::Vector4 CategoryColor(ItemCategory c) { return TintColor(TintFor(c)); }

    // 飾りのテクスチャ（初回に読む。ResourceManager のキャッシュ経由）
    struct Textures
    {
        std::shared_ptr<Texture> white;          // 無地（パネルの地・線）
        std::shared_ptr<Texture> circleStar;     // 八芒星の魔法陣
        std::shared_ptr<Texture> circleFlower;   // 花の魔法陣
        std::shared_ptr<Texture> corner;         // 右上の角の組紐（他の角は反転して使う）
        std::shared_ptr<Texture> dividerFleur;   // 百合紋の分割線（見出し用）
        std::shared_ptr<Texture> dividerThin;    // 細い分割線（区切り用）
        std::shared_ptr<Texture> disc;           // 塗りの円（コードで作る。丸い欄の地・クールダウン）
        std::shared_ptr<Texture> ring;           // 細い円の線（コードで作る。丸い欄の縁）
    };
    const Textures& Tex();

    struct PanelStyle
    {
        // 線形の値（戦闘の UI は HDR バッファに描かれてガンマを通るので、画面ではかなり明るく見える）
        DirectX::SimpleMath::Vector4 fill = { 0.0025f, 0.0020f, 0.0045f, 0.97f };
        float innerInset = 5.0f;      // 外の線から内の線まで（px）
        float innerAlpha = 0.40f;     // 内の線の濃さ
        float cornerSize = 0.0f;      // 四隅の組紐の大きさ（px）。0 = 描かない
        float cornerAlpha = 0.90f;
    };

    // パネル。highlight（0〜1）で線を明るく・外に光を足す（選択中のカードなど）
    void DrawPanel(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size, const DirectX::SimpleMath::Vector4& tint,
        const PanelStyle& style, float highlight = 0.0f);

    // 1 px の四角い線（枠）
    void DrawFrameLines(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size, const DirectX::SimpleMath::Vector4& color, float thickness = 1.0f);

    // 分割線。中心と幅で置く（高さは絵の比率から）
    void DrawDivider(SpriteRenderer& sprite, bool fleur, const DirectX::SimpleMath::Vector2& center,
        float width, const DirectX::SimpleMath::Vector4& tint);

    // 魔法陣。中心・直径・回転（ラジアン）
    void DrawCircle(SpriteRenderer& sprite, bool star, const DirectX::SimpleMath::Vector2& center,
        float diameter, const DirectX::SimpleMath::Vector4& tint, float radians);

    // 魔法陣を回す時計（一時停止中も回るよう実時間、秒）
    float Clock();
}
