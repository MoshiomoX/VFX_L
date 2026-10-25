// ============================================================
// TextRenderer.cpp
// ============================================================
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Shader/PixelShader.h"
#include "Manager/ResourceManager.h"

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

bool TextRenderer::EnableOutline(float maxTexels)
{
    m_OutlinePS = ResourceManager::Get().LoadPS(L"TextOutlinePS", L"Shader/UI/TextOutlinePS.hlsl");
    m_OutlineCB.maxTexels = maxTexels;
    if (!m_OutlinePS) std::cout << "[Warning] TextRenderer: outline shader missing, outlines fall back to 8 offset copies" << std::endl;
    return m_OutlinePS != nullptr;
}

void TextRenderer::DrawOutline(const std::wstring& text,
    const Vector2& position, const Vector4& outlineColor, float scale)
{
    if (!m_InBegin || text.empty()) return;
    if (!m_OutlinePS)
    {
        // シェーダーが無い時の代わり：8 方向にずらして縁の色で描く（2026-10-23 以前のやり方）
        static const Vector2 kDirs[8] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 0.71f, 0.71f }, { -0.71f, 0.71f }, { 0.71f, -0.71f }, { -0.71f, -0.71f } };
        for (const Vector2& d : kDirs) Draw(text, position + d * m_OutlineCB.outlinePx, outlineColor, scale);
        return;
    }
    // alpha を負にした印で TextOutlinePS が縁として描く（rgb は乗算済み）
    TextEntry entry;
    entry.text = text;
    entry.position = position;
    const float a = outlineColor.w;
    entry.color = { outlineColor.x * a, outlineColor.y * a, outlineColor.z * a, -a };
    entry.scale = scale;
    m_Queue.push_back(std::move(entry));
}

void TextRenderer::Shutdown()
{
    m_OutlinePS.reset();
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

    if (m_OutlinePS)
    {
        // 縁取りの PS に差し替える（SpriteBatch が既定のステートを積んだ後に呼ばれる）
        m_Batch->Begin(SpriteSortMode_Deferred, nullptr, nullptr, nullptr, nullptr, [this]()
            {
                m_OutlinePS->Bind(m_Context);
                m_OutlinePS->WriteBuffer(m_Context, 0, &m_OutlineCB);
            });
    }
    else
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
    // SpriteBatch の既定は乗算済み alpha（ONE / INV_SRC_ALPHA）。
    // rgb に alpha を掛けておかないと、半透明の文字が加算のように白く浮く
    entry.color = { color.x * color.w, color.y * color.w, color.z * color.w, color.w };
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
