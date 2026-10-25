// ============================================================
// TextRenderer.h
// テキスト描画（DirectXTK SpriteFont / SpriteBatch のラッパー）
// UI 全般で使い回す。Begin → Draw を複数回 → End の流れ。
// End() で溜めた分をまとめて描画し、ステートを復元する。
//
// 文字列は wchar_t（UTF-16）が基本。
// ソースに日本語リテラルを書くファイルは UTF-8（BOM 付き）で保存すること。
// JSON 等の動的な UTF-8 文字列は DrawUTF8 で内部変換する。
// ============================================================
#pragma once
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>
#include <SimpleMath.h>
#include <SpriteBatch.h>
#include <SpriteFont.h>

class PixelShader;

class TextRenderer
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
        const wchar_t* fontPath);
    void Shutdown();

    // 描画開始・終了（End でまとめて描画）
    void Begin();
    void End();

    // 文字列を積む（position は左上、ピクセル単位）
    void Draw(const std::wstring& text,
        const DirectX::SimpleMath::Vector2& position,
        const DirectX::SimpleMath::Vector4& color = { 1, 1, 1, 1 },
        float scale = 1.0f);

    // UTF-8 文字列版（JSON 由来の動的テキスト用。内部で UTF-16 へ変換）
    void DrawUTF8(const std::string& textUtf8,
        const DirectX::SimpleMath::Vector2& position,
        const DirectX::SimpleMath::Vector4& color = { 1, 1, 1, 1 },
        float scale = 1.0f);

    // ---- 縁取り（2026-10-23、ダメージの数字）----
    // 縁はシェーダー（Shader/UI/TextOutlinePS）が字の周りを拾って作るので、縁取り付きの 1 文字列 = DrawString 2 回（縁 + 本体）。
    // （以前は 8 方向にずらして 9 回描いていた。Debug では 1 文字ごとの DrawString の代が重い）
    // 字体は縁の分の余白を持って焼くこと（Tools/BuildSpriteFont.ps1 -Pad）。maxTexels = 余白 - 1（これより遠くは拾わない）。
    // シェーダーが読めなかった時は DrawOutline が 8 方向にずらして描く
    bool EnableOutline(float maxTexels);
    void SetOutlinePx(float px) { m_OutlineCB.outlinePx = px; }   // 縁の太さ（画面 px）。End で使う
    // 縁だけを積む（この後に同じ位置で Draw した本体の下に敷かれる。積んだ順に描くので、文字列ごとに縁 → 本体の順を守る）
    void DrawOutline(const std::wstring& text,
        const DirectX::SimpleMath::Vector2& position,
        const DirectX::SimpleMath::Vector4& outlineColor,
        float scale = 1.0f);

    // 描画せずにサイズを測る（ピクセル単位）
    DirectX::SimpleMath::Vector2 Measure(const std::wstring& text,
        float scale = 1.0f) const;
    float GetLineHeight(float scale = 1.0f) const;

    UINT GetLastStringCount() const { return m_LastStringCount; }

private:
    struct TextEntry
    {
        std::wstring text;
        DirectX::SimpleMath::Vector2 position;
        DirectX::SimpleMath::Vector4 color;
        float scale;
    };

    struct OutlineCB   // Shader/UI/TextOutlinePS.hlsl の TextOutlineCB と同じ並び
    {
        float outlinePx = 0.0f;
        float maxTexels = 0.0f;
        float pad[2] = {};
    };

    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<PixelShader> m_OutlinePS;   // EnableOutline の後だけ。全ての文字列をこれで描く
    OutlineCB m_OutlineCB;

    std::unique_ptr<DirectX::SpriteBatch> m_Batch;
    std::unique_ptr<DirectX::SpriteFont>  m_Font;

    std::vector<TextEntry> m_Queue;

    UINT m_LastStringCount = 0;
    bool m_InBegin = false;
};
