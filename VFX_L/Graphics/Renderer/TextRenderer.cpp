// ============================================================
// TextRenderer.cpp
// ============================================================
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include <iostream>

using namespace DirectX;
using namespace DirectX::SimpleMath;

bool TextRenderer::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
    const wchar_t* fontPath)
{
    m_Context = context;

    // SpriteFont のコンストラクタはファイルが無いと例外を投げる
    try
    {
        m_Batch = std::make_unique<SpriteBatch>(context);
        m_Font = std::make_unique<SpriteFont>(device, fontPath);
    }
    catch (const std::exception& e)
    {
        std::cout << "[Error] TextRenderer: font load failed: "
            << e.what() << std::endl;
        m_Batch.reset();
        m_Font.reset();
        return false;
    }

    // フォントに無い文字で DrawString が例外を投げないよう、
    // 既定文字を設定する（無い文字は ? で表示される）
    if (m_Font->GetDefaultCharacter() == 0 && m_Font->ContainsCharacter(L'?'))
        m_Font->SetDefaultCharacter(L'?');

    m_Queue.reserve(64);
    std::cout << "[OK] TextRenderer initialized" << std::endl;
    return true;
}

void TextRenderer::Shutdown()
{
    m_Queue.clear();
    m_Font.reset();
    m_Batch.reset();
    m_Context = nullptr;
}

void TextRenderer::Begin()
{
    m_Queue.clear();
    m_LastStringCount = 0;
    m_InBegin = true;
}

// ============================================================
// 溜めた分を SpriteBatch で一括描画する
// SpriteBatch::Begin の既定ステート＝アルファ合成（事前乗算）、深度無し
// ============================================================
void TextRenderer::End()
{
    m_InBegin = false;
    if (m_Queue.empty() || !m_Batch || !m_Font || !m_Context) return;

    m_Batch->Begin();
    for (const auto& e : m_Queue)
    {
        m_Font->DrawString(m_Batch.get(), e.text.c_str(),
            XMFLOAT2(e.position.x, e.position.y),
            XMLoadFloat4(&e.color),
            0.0f, XMFLOAT2(0.0f, 0.0f), e.scale);
    }
    m_Batch->End();

    m_LastStringCount = (UINT)m_Queue.size();
    m_Queue.clear();

    // 次のパスへステートを持ち越さない
    RenderStates::Get().Restore(m_Context);
}

void TextRenderer::Draw(const std::wstring& text,
    const Vector2& position, const Vector4& color, float scale)
{
    if (!m_InBegin || text.empty()) return;

    TextEntry entry;
    entry.text = text;
    entry.position = position;
    entry.color = color;
    entry.scale = scale;
    m_Queue.push_back(std::move(entry));
}

void TextRenderer::DrawUTF8(const std::string& textUtf8,
    const Vector2& position, const Vector4& color, float scale)
{
    if (!m_InBegin || textUtf8.empty()) return;

    int len = MultiByteToWideChar(CP_UTF8, 0,
        textUtf8.c_str(), (int)textUtf8.size(), nullptr, 0);
    if (len <= 0) return;

    std::wstring wide((size_t)len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
        textUtf8.c_str(), (int)textUtf8.size(), wide.data(), len);

    Draw(wide, position, color, scale);
}

Vector2 TextRenderer::Measure(const std::wstring& text, float scale) const
{
    if (!m_Font || text.empty()) return { 0.0f, 0.0f };

    XMVECTOR v = m_Font->MeasureString(text.c_str());
    return { XMVectorGetX(v) * scale, XMVectorGetY(v) * scale };
}

float TextRenderer::GetLineHeight(float scale) const
{
    return m_Font ? m_Font->GetLineSpacing() * scale : 0.0f;
}
