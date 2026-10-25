// ============================================================
// InputGlyph.cpp
// ============================================================
#include "UI/InputGlyph.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"

using namespace DirectX::SimpleMath;

namespace
{
    // 絵の形。どれも 128px の中の透明な余白を切って描く（そのままだと余白の分だけ小さく見え、横長の物は文字が読めなかった）
    enum class Shape
    {
        Square,   // 丸いボタン・十字・スティック・キーキャップ：16〜111 の 96x96
        Wide,     // LB などの横長：16〜111 x 32〜95 の 96x64
        Arrows,   // 矢印キーの塊：18〜109 x 34〜93 の 92x60
        Mouse,    // マウス：30〜97 x 20〜107 の 68x88（縦長）
    };

    struct Glyph
    {
        const wchar_t* token;
        const wchar_t* path;
        Shape shape;
        std::shared_ptr<Texture> tex;
        bool tried = false;
    };

    Glyph g_Glyphs[] = {
        { L"A", Res::PadIcon::A, Shape::Square },
        { L"B", Res::PadIcon::B, Shape::Square },
        { L"X", Res::PadIcon::X, Shape::Square },
        { L"Y", Res::PadIcon::Y, Shape::Square },
        { L"LB", Res::PadIcon::LB, Shape::Wide },
        { L"RB", Res::PadIcon::RB, Shape::Wide },
        { L"START", Res::PadIcon::Start, Shape::Wide },
        { L"BACK", Res::PadIcon::Back, Shape::Wide },
        { L"DPAD", Res::PadIcon::Dpad, Shape::Square },
        { L"DPADV", Res::PadIcon::DpadV, Shape::Square },
        { L"DPADH", Res::PadIcon::DpadH, Shape::Square },
        { L"LS", Res::PadIcon::LS, Shape::Square },
        { L"K:ARROWS", Res::KeyIcon::Arrows, Shape::Arrows },
        { L"K:ARROWSV", Res::KeyIcon::ArrowsV, Shape::Arrows },
        { L"K:ARROWSH", Res::KeyIcon::ArrowsH, Shape::Arrows },
        { L"M:L", Res::KeyIcon::MouseL, Shape::Mouse },
        { L"M:R", Res::KeyIcon::MouseR, Shape::Mouse },
        { L"M:WHEEL", Res::KeyIcon::MouseWheel, Shape::Mouse },
    };
    Glyph g_KeyCap = { L"", Res::KeyIcon::Cap, Shape::Square };   // {K:名前} で名前入りの絵が無いキーの下地

    // {K:名前} のキー：Tools/BuildKeyIcons.ps1 が作る Key<名前>.png（キー名を太字で焼き込んだ絵。高さ 64、幅はキー名次第）。
    // 名前ごとに初めて使う時に読む。無ければ無地のキーに文字で名前を載せる
    struct KeyImage
    {
        std::shared_ptr<Texture> tex;
        float aspect = 1.0f;   // キーの本体（上下左右の余白 = 高さの 1/8 を切った所）の幅 / 高さ
        Vector4 uv;
    };
    std::unordered_map<std::wstring, KeyImage> g_KeyImages;

    const KeyImage* FindKeyImage(const std::wstring& name)
    {
        auto it = g_KeyImages.find(name);
        if (it == g_KeyImages.end())
        {
            KeyImage k;
            if (ID3D11Device* device = ResourceManager::Get().GetDevice())
            {
                const std::wstring path = std::wstring(Res::KeyIcon::Dir) + L"Key" + name + L".png";
                if (std::filesystem::exists(path))
                {
                    auto tex = std::make_shared<Texture>();
                    if (tex->Load(device, path, true) && tex->GetHeight() > 0)
                    {
                        // 余白は上下左右とも高さの 1/8（128 なら 16px、64 なら 8px）
                        const float w = (float)tex->GetWidth(), h = (float)tex->GetHeight(), m = h * 0.125f;
                        k.tex = tex;
                        k.aspect = (w - m * 2.0f) / (h - m * 2.0f);
                        k.uv = { m / w, m / h, (w - m * 2.0f) / w, (h - m * 2.0f) / h };
                    }
                }
            }
            it = g_KeyImages.emplace(name, k).first;
        }
        return it->second.tex ? &it->second : nullptr;
    }

    constexpr Vector4 kUvSquare = { 16.0f / 128.0f, 16.0f / 128.0f, 96.0f / 128.0f, 96.0f / 128.0f };
    constexpr Vector4 kUvWide = { 16.0f / 128.0f, 32.0f / 128.0f, 96.0f / 128.0f, 64.0f / 128.0f };
    constexpr Vector4 kUvArrows = { 18.0f / 128.0f, 34.0f / 128.0f, 92.0f / 128.0f, 60.0f / 128.0f };
    constexpr Vector4 kUvMouse = { 30.0f / 128.0f, 20.0f / 128.0f, 68.0f / 128.0f, 88.0f / 128.0f };
    constexpr float kWideAspect = 96.0f / 64.0f;
    constexpr float kArrowsAspect = 92.0f / 60.0f;
    constexpr float kMouseAspect = 68.0f / 88.0f;

    // キーキャップの 3 分割（96px の中の左右の角の幅。真ん中は無地なので伸ばす）
    constexpr float kCapEdge = 28.0f;

    // キーボード・マウスの絵（白地。keyGain を掛ける）
    bool IsKeyboardMouse(const Glyph& g) { return g.shape == Shape::Arrows || g.shape == Shape::Mouse; }

    const Vector4& UvOf(Shape s)
    {
        switch (s)
        {
        case Shape::Wide:   return kUvWide;
        case Shape::Arrows: return kUvArrows;
        case Shape::Mouse:  return kUvMouse;
        default:            return kUvSquare;
        }
    }

    // 初めて使う時に読む。読めない時は false
    bool Ensure(Glyph& g)
    {
        if (!g.tried)
        {
            g.tried = true;
            // 色付きの絵なので sRGB で読む（共有キャッシュは UNORM なので通さない。木箱の絵と同じ）
            if (ID3D11Device* device = ResourceManager::Get().GetDevice())
            {
                auto tex = std::make_shared<Texture>();
                if (tex->Load(device, g.path, true)) g.tex = tex;
                else std::wcout << L"[InputGlyph] load failed: " << g.path << std::endl;
            }
        }
        return g.tex != nullptr;
    }

    // 記号の絵。知らない記号 / 読めない時は nullptr
    const Glyph* Find(const wchar_t* token, size_t len)
    {
        for (Glyph& g : g_Glyphs)
        {
            if (std::wcslen(g.token) != len || std::wcsncmp(g.token, token, len) != 0) continue;
            return Ensure(g) ? &g : nullptr;
        }
        return nullptr;
    }

    // 文の 1 片：文字の段 / 絵 / キーの絵 / 無地のキー + 文字（text = キー名）
    struct Part
    {
        const Glyph* glyph = nullptr;
        const KeyImage* keyImage = nullptr;
        bool key = false;
        std::wstring text;
    };

    // 文を片に分ける。<キーボード|パッド> は今の機器の側だけを読む（中の {…} もそのまま使える。入れ子の < は不可）
    void Parse(const std::wstring& s, size_t begin, size_t end, bool pad, std::vector<Part>& out)
    {
        std::wstring run;
        auto flush = [&] { if (!run.empty()) { out.push_back({ nullptr, nullptr, false, run }); run.clear(); } };

        size_t i = begin;
        while (i < end)
        {
            const wchar_t c = s[i];
            if (c == L'<')
            {
                const size_t bar = s.find(L'|', i + 1);
                const size_t close = s.find(L'>', i + 1);
                if (bar < close && close < end)
                {
                    flush();
                    if (pad) Parse(s, bar + 1, close, pad, out);
                    else     Parse(s, i + 1, bar, pad, out);
                    i = close + 1;
                    continue;
                }
            }
            else if (c == L'{')
            {
                const size_t close = s.find(L'}', i + 1);
                if (close < end && close - i <= 24)
                {
                    const wchar_t* tok = s.c_str() + i + 1;
                    const size_t len = close - i - 1;
                    if (const Glyph* g = Find(tok, len))
                    {
                        flush();
                        out.push_back({ g, nullptr, false, {} });
                        i = close + 1;
                        continue;
                    }
                    if (len > 2 && tok[0] == L'K' && tok[1] == L':')
                    {
                        flush();
                        // 名前入りの絵 → 無地のキー + 文字 → 文字だけ
                        std::wstring name(tok + 2, len - 2);
                        if (const KeyImage* k = FindKeyImage(name)) out.push_back({ nullptr, k, false, {} });
                        else if (Ensure(g_KeyCap))                  out.push_back({ nullptr, nullptr, true, std::move(name) });
                        else                                        out.push_back({ nullptr, nullptr, false, std::move(name) });
                        i = close + 1;
                        continue;
                    }
                }
            }
            run += s[i++];
        }
        flush();
    }

    // 分けた結果を「文字列 × 今の機器」ごとに覚えておく（2026-10-25。以前は Measure / Draw のたびに分け直して
    // 片の文字列を確保していた。DrawOutlined は 1 回で 6 回）。
    // 片が指す絵（g_Glyphs / g_KeyImages）は Shutdown まで動かないので、そのまま覚えて良い。
    // 数字の入る文（距離・金貨）は毎フレーム変わるので、溜まり過ぎたら丸ごと捨てる。
    // 返す参照は次に Split を呼ぶまで有効（捨てるのは新しい文を入れる時だけ）
    constexpr size_t kSplitCacheMax = 256;
    std::unordered_map<std::wstring, std::vector<Part>> g_SplitCache[2];   // [0] キーボード・マウス / [1] パッド

    const std::vector<Part>& Split(const std::wstring& s)
    {
        const bool pad = InputManager::Get().IsUsingPad();
        auto& cache = g_SplitCache[pad ? 1 : 0];
        auto it = cache.find(s);
        if (it != cache.end()) return it->second;
        if (cache.size() >= kSplitCacheMax) cache.clear();

        std::vector<Part> parts;
        Parse(s, 0, s.size(), pad, parts);
        return cache.emplace(s, std::move(parts)).first->second;
    }

    float KeyLabelScale(float scale) { return scale * InputGlyph::keyLabelScale; }

    // 絵の描く大きさ（px）。lineH = 行の高さ
    Vector2 GlyphSize(Shape shape, float lineH)
    {
        switch (shape)
        {
        case Shape::Wide:
        {
            const float h = lineH * InputGlyph::wideHeight;
            return { h * kWideAspect, h };
        }
        case Shape::Arrows:
        {
            const float h = lineH * InputGlyph::arrowsHeight;
            return { h * kArrowsAspect, h };
        }
        case Shape::Mouse:
        {
            const float h = lineH * InputGlyph::mouseHeight;
            return { h * kMouseAspect, h };
        }
        default:
        {
            const float s = lineH * InputGlyph::iconHeight;
            return { s, s };
        }
        }
    }

    // キーキャップの大きさ。高さは丸いボタンと同じ、幅はキー名が収まるまで伸ばす（1 文字なら正方形）
    Vector2 KeySize(const TextRenderer& text, const std::wstring& label, float scale, float lineH)
    {
        const float h = lineH * InputGlyph::keyHeight;
        const float w = text.Measure(label, KeyLabelScale(scale)).x + h * InputGlyph::keyPadX * 2.0f;
        return { (std::max)(h, w), h };
    }

    Vector2 KeyImageSize(const KeyImage& k, float lineH)
    {
        const float h = lineH * InputGlyph::keyHeight;
        return { h * k.aspect, h };
    }

    // 文字列の送り幅（末尾の空白も入れる）。
    // SpriteFont::MeasureString は空白を数えず「最後の字の右端」を返すので、後ろに 1 字足した差で測る
    float Advance(const TextRenderer& text, const std::wstring& seg, float scale)
    {
        if (seg.empty()) return 0.0f;
        return text.Measure(seg + L"|", scale).x - text.Measure(L"|", scale).x;
    }

    // 無地のキーを左の角・伸ばす真ん中・右の角の 3 枚で描き、キー名を真ん中に載せる
    void DrawKey(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& label,
        const Vector2& tl, const Vector2& size, float scale, float alpha)
    {
        const float edge = size.y * (kCapEdge / 96.0f);
        const float g = InputGlyph::keyGain;
        const Vector4 tint = { g, g, g, alpha };
        const float u0 = 16.0f / 128.0f, v0 = 16.0f / 128.0f, vh = 96.0f / 128.0f;
        const float eu = kCapEdge / 128.0f;
        const float mid = (std::max)(0.0f, size.x - edge * 2.0f);
        sprite.Draw(g_KeyCap.tex, tl, { edge, size.y }, tint, { u0, v0, eu, vh });
        if (mid > 0.0f)
            sprite.Draw(g_KeyCap.tex, { tl.x + edge, tl.y }, { mid, size.y }, tint, { u0 + eu, v0, 96.0f / 128.0f - eu * 2.0f, vh });
        sprite.Draw(g_KeyCap.tex, { tl.x + edge + mid, tl.y }, { edge, size.y }, tint, { u0 + 96.0f / 128.0f - eu, v0, eu, vh });

        const float ls = KeyLabelScale(scale);
        const Vector2 m = text.Measure(label, ls);
        Vector4 col = InputGlyph::keyLabelColor;
        col.w *= alpha;
        text.Draw(label, { tl.x + (size.x - m.x) * 0.5f, tl.y + size.y * 0.5f - m.y * InputGlyph::keyLabelY }, col, ls);
    }
}

namespace InputGlyph
{
    bool UsingPad()
    {
        return InputManager::Get().IsUsingPad();
    }

    float IconSize(const TextRenderer& text, float scale)
    {
        return text.GetLineHeight(scale) * iconHeight;
    }

    Vector2 Measure(const TextRenderer& text, const std::wstring& s, float scale)
    {
        const float lineH = text.GetLineHeight(scale);
        const float gap = lineH * iconHeight * iconGap;
        float w = 0.0f, h = 0.0f, lastText = 0.0f;
        for (const Part& p : Split(s))
        {
            if (p.glyph || p.key || p.keyImage)
            {
                const Vector2 gs = p.key ? KeySize(text, p.text, scale, lineH)
                    : p.keyImage ? KeyImageSize(*p.keyImage, lineH) : GlyphSize(p.glyph->shape, lineH);
                w += gs.x + gap * 2.0f;
                lastText = 0.0f;
                h = (std::max)(h, gs.y);
            }
            else
            {
                // 最後の文字の段だけは「字の右端」まで（末尾の空白で中央寄せがずれないように）
                const Vector2 m = text.Measure(p.text, scale);
                const float adv = Advance(text, p.text, scale);
                lastText = adv - m.x;
                w += adv;
                h = (std::max)(h, m.y);
            }
        }
        return { w - (std::max)(0.0f, lastText), h };
    }

    // 分け終えた片を描く（Draw / DrawShadowed / DrawOutlined が共用。影・縁の段で分け直さない）
    static void DrawParts(SpriteRenderer& sprite, TextRenderer& text, const std::vector<Part>& parts,
        const Vector2& pos, const Vector4& color, float scale, bool icons)
    {
        const float lineH = text.GetLineHeight(scale);
        const float gap = lineH * iconHeight * iconGap;
        const float cy = pos.y + lineH * iconCenter;
        const Vector4 tint = { iconGain, iconGain, iconGain, color.w };
        const Vector4 keyTint = { keyGain, keyGain, keyGain, color.w };   // キーボード・マウスの白い絵（ブルームで滲まないように）
        float x = pos.x;
        for (const Part& p : parts)
        {
            if (p.keyImage)
            {
                const Vector2 ks = KeyImageSize(*p.keyImage, lineH);
                if (icons) sprite.Draw(p.keyImage->tex, { x + gap, cy - ks.y * 0.5f }, ks, keyTint, p.keyImage->uv);
                x += ks.x + gap * 2.0f;
            }
            else if (p.key)
            {
                const Vector2 ks = KeySize(text, p.text, scale, lineH);
                if (icons) DrawKey(sprite, text, p.text, { x + gap, cy - ks.y * 0.5f }, ks, scale, color.w);
                x += ks.x + gap * 2.0f;
            }
            else if (p.glyph)
            {
                const Vector2 gs = GlyphSize(p.glyph->shape, lineH);
                if (icons)
                    sprite.Draw(p.glyph->tex, { x + gap, cy - gs.y * 0.5f }, gs, IsKeyboardMouse(*p.glyph) ? keyTint : tint, UvOf(p.glyph->shape));
                x += gs.x + gap * 2.0f;
            }
            else
            {
                text.Draw(p.text, { x, pos.y }, color, scale);
                x += Advance(text, p.text, scale);
            }
        }
    }

    void Draw(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const Vector2& pos, const Vector4& color, float scale, bool icons)
    {
        DrawParts(sprite, text, Split(s), pos, color, scale, icons);
    }

    void DrawShadowed(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const Vector2& pos, const Vector4& color, float scale,
        const Vector2& shadowOffset, const Vector4& shadowColor)
    {
        const std::vector<Part>& parts = Split(s);   // 1 回だけ分ける
        DrawParts(sprite, text, parts, pos + shadowOffset, shadowColor, scale, false);
        DrawParts(sprite, text, parts, pos, color, scale, true);
    }

    void DrawOutlined(SpriteRenderer& sprite, TextRenderer& text, const std::wstring& s,
        const Vector2& pos, const Vector4& color, float scale, float o)
    {
        const std::vector<Part>& parts = Split(s);   // 1 回だけ分ける（以前は縁 5 回 + 本体 1 回分け直していた）
        const Vector4 shadow = { 0.0f, 0.0f, 0.0f, 0.75f * color.w };
        for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o), Vector2(o * 1.5f, o * 1.5f) })
            DrawParts(sprite, text, parts, pos + d, shadow, scale, false);
        DrawParts(sprite, text, parts, pos, color, scale, true);
    }

    void DrawIcon(SpriteRenderer& sprite, const wchar_t* token, const Vector2& center, float size, float alpha)
    {
        const Glyph* g = Find(token, std::wcslen(token));
        if (!g) return;
        Vector2 gs = { size, size };
        if (g->shape == Shape::Wide)   gs.x = size * kWideAspect;
        if (g->shape == Shape::Arrows) gs.x = size * kArrowsAspect;
        if (g->shape == Shape::Mouse)  gs.x = size * kMouseAspect;
        sprite.Draw(g->tex, { center.x - gs.x * 0.5f, center.y - gs.y * 0.5f }, gs,
            { iconGain, iconGain, iconGain, alpha }, UvOf(g->shape));
    }

    void Shutdown()
    {
        for (Glyph& g : g_Glyphs)
        {
            g.tex.reset();
            g.tried = false;
        }
        g_KeyCap.tex.reset();
        g_KeyCap.tried = false;
        for (auto& cache : g_SplitCache) cache.clear();   // 片が g_KeyImages を指しているので先に捨てる
        g_KeyImages.clear();
    }
}
