// ============================================================
// InputGlyph.h
// UI の文の中にボタン・キーの絵を混ぜて描く（2026-10-09 ユーザー：
// 「手柄のボタンの説明を文字ではなく、ボタンの絵にして」→ 同日「キーボードの絵も入れて、今の入力機器で表示を替えて」）。
//
// 文の中に {A} のように書くと、そこに絵が入る。使える記号：
//   パッド     : {A} {B} {X} {Y} {LB} {RB} {START} {BACK} {DPAD}（十字）{DPADV}（十字の上下）{DPADH}（十字の左右）{LS}（左スティック）
//   キーボード : {K:名前}（Key<名前>.png = キー名入りの白いキー。{K:F} {K:Esc} {K:Enter} {K:Tab} {K:BackSpace} など。
//                新しいキーは Tools/BuildKeyIcons.ps1 の表に足して作る。絵の無い名前は無地のキーに文字で載せる）
//                {K:ARROWS} {K:ARROWSV}（矢印キーの上下）{K:ARROWSH}（左右）
//   マウス     : {M:L}（左クリック）{M:R}（右クリック）{M:WHEEL}（ホイール）
// 知らない記号は文字のまま出す。
//
// 機器で出し分け：<キーボードの時|パッドの時> と書くと、最後に触った機器（InputManager::GetLastDevice）の側だけを出す。
//   例: L"<{K:F}|{B}>  開ける"、L"<{K:Esc}|{BACK} / {B}> で再開"。中に {…} を書ける（< の入れ子は不可）
//
// 絵は Kenney Input Prompts（Xbox Series / Keyboard & Mouse、CC0）。Assets/Texture/UI/Input、パスは Res::PadIcon / Res::KeyIcon。
// 絵は SpriteRenderer、文字（キー名も）は TextRenderer に積む（文字はスプライトの後にまとめて描かれるので、キー名はキーの上に乗る）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <string>

class SpriteRenderer;
class TextRenderer;

namespace InputGlyph
{
    // 最後に触った機器がパッドか（コードの側で出し分ける時。HUD の札など）
    bool UsingPad();

    // 1 行の幅と高さ（px）。絵の幅も入れる
    DirectX::SimpleMath::Vector2 Measure(const TextRenderer& text, const std::wstring& s, float scale);

    // 1 行を描く。pos = 左上（TextRenderer::Draw と同じ）。
    // icons = false は文字だけ（影・縁取りの段。絵の分の隙間は空ける。キー名も描かない）
    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const DirectX::SimpleMath::Vector2& pos, const DirectX::SimpleMath::Vector4& color, float scale,
        bool icons = true);

    // 影付き（影は文字だけ。絵には付けない）
    void DrawShadowed(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const DirectX::SimpleMath::Vector2& pos, const DirectX::SimpleMath::Vector4& color, float scale,
        const DirectX::SimpleMath::Vector2& shadowOffset, const DirectX::SimpleMath::Vector4& shadowColor);

    // 縁取り付き（上下左右 + 右下に o px ずらした黒を敷く。タイトル・リザルトの書き方と同じ）
    void DrawOutlined(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const DirectX::SimpleMath::Vector2& pos, const DirectX::SimpleMath::Vector4& color, float scale, float o);

    // 絵 1 つだけ（HUD の札など）。token は "Y" / "M:L" のように括弧なし（{K:名前} のキーキャップは不可）。
    // 中心と高さ（px。余白を切った後の大きさ）
    void DrawIcon(SpriteRenderer& sprite, const wchar_t* token, const DirectX::SimpleMath::Vector2& center,
        float size, float alpha = 1.0f);

    // 文字の大きさ scale の行に入る絵の一辺（px）
    float IconSize(const TextRenderer& text, float scale);

    // 絵を捨てる（Application::Shutdown。デバイスより先に）
    void Shutdown();

    // ---- 調整 ----（絵は透明な余白を切ってから大きさを決める。高さは行の高さ GetLineHeight に対する割合）
    inline float iconHeight = 0.66f;   // 丸いボタン・十字の直径（漢字の高さくらい）
    inline float wideHeight = 0.56f;   // 横長（LB / RB / START / BACK）の高さ（幅はその 1.5 倍）
    inline float arrowsHeight = 0.62f; // 矢印キーの塊の高さ（幅はその 1.53 倍）
    inline float mouseHeight = 0.80f;  // マウスの高さ（幅はその 0.77 倍）
    inline float keyHeight = 0.80f;    // キーの高さ（1 文字なら正方形。字が小さく見えるので丸いボタンより少し大きく）
    inline float keyGain = 0.78f;      // キーの絵の色に掛ける（白いキーが 1.0 だとブルームで滲み、黒い字が灰色に浮いた）
    // 以下は名前入りの絵が無いキー（無地のキーに文字で載せる）だけ
    inline float keyPadX = 0.22f;      // キー名の左右の余白（キーの高さに対する割合）
    inline float keyLabelScale = 0.62f;   // キー名の文字の大きさ（行の文字に対する倍率）
    inline float keyLabelY = 0.50f;       // キー名の行の高さのどこをキーの中心に合わせるか
    inline DirectX::SimpleMath::Vector4 keyLabelColor = { 0.02f, 0.02f, 0.03f, 1.0f };   // キー名（線形。白いキーの上の濃い字）
    inline float iconCenter = 0.50f;   // 行の上から絵の中心まで
    inline float iconGap = 0.12f;      // 絵の左右の隙間（丸い絵の直径に対する割合）
    inline float iconGain = 1.0f;      // 絵の色に掛ける
}
