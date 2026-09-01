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

    ID3D11DeviceContext* m_Context = nullptr;

    std::unique_ptr<DirectX::SpriteBatch> m_Batch;
    std::unique_ptr<DirectX::SpriteFont>  m_Font;

    std::vector<TextEntry> m_Queue;

    UINT m_LastStringCount = 0;
    bool m_InBegin = false;
};
