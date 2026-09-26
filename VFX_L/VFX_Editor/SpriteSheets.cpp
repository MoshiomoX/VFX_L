// ============================================================
// SpriteSheets.cpp
// ============================================================
#include "VFX_Editor/SpriteSheets.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Material/Texture.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector4;

namespace
{
    // 読めなかった物も nullptr で覚える（毎フレーム読みに行かない）
    std::unordered_map<std::string, std::unique_ptr<SpriteSheets::Info>> g_Cache;

    std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }   // Assets 以下は ASCII

    std::string JsonPathOf(const std::string& png)
    {
        const size_t dot = png.find_last_of('.');
        return (dot == std::string::npos ? png : png.substr(0, dot)) + ".json";
    }

    // PVFX Foundry の grid manifest
    void ReadPvfx(const nlohmann::json& j, SpriteSheets::Info& s)
    {
        const auto& frames = j["frames"];
        s.frameCount = (std::max)(1, (int)frames.size());
        if (!frames.empty())
        {
            const auto& f0 = frames[0];
            if (f0.contains("sheet"))
            {
                s.cellW = f0["sheet"].value("width", s.cellW);
                s.cellH = f0["sheet"].value("height", s.cellH);
            }
            if (f0.contains("duration"))
            {
                const double num = f0["duration"].value("numerator_ms", 50.0);
                const double den = f0["duration"].value("denominator", 1.0);
                if (den > 0.0 && num > 0.0) s.frameTime = (float)(num / den / 1000.0);
            }
        }
        s.loop = (j.value("loop_mode", std::string("none")) == "loop");
        s.point = true;   // 像素絵（IMPORTING.md: nearest-neighbor）

        int srcW = s.cellW, srcH = s.cellH;
        if (j.contains("source_size") && j["source_size"].size() >= 2)
        {
            srcW = j["source_size"][0];
            srcH = j["source_size"][1];
        }
        if (j.contains("pivot") && j["pivot"].size() >= 2 && srcW > 0 && srcH > 0)
            s.pivot = Vector2((float)j["pivot"][0] / srcW, (float)j["pivot"][1] / srcH);
    }

    // 簡易形式
    void ReadSimple(const nlohmann::json& j, SpriteSheets::Info& s)
    {
        const int cols = (std::max)(1, j.value("cols", 1));
        const int rows = (std::max)(1, j.value("rows", 1));
        s.cellW = s.texW / cols;
        s.cellH = s.texH / rows;
        s.frameCount = std::clamp(j.value("frames", cols * rows), 1, cols * rows);
        const float fps = j.value("fps", 20.0f);
        s.frameTime = (fps > 0.0f) ? 1.0f / fps : 0.05f;
        s.loop = j.value("loop", false);
        s.point = (j.value("filter", std::string("point")) == "point");
        if (j.contains("pivot") && j["pivot"].size() >= 2)
            s.pivot = Vector2(j["pivot"][0], j["pivot"][1]);
    }
}

Vector4 SpriteSheets::Info::FrameUV(int i) const
{
    const int c = (std::max)(1, cols);
    i = std::clamp(i, 0, (std::max)(0, frameCount - 1));
    const float u = (float)cellW / (float)(std::max)(1, texW);
    const float v = (float)cellH / (float)(std::max)(1, texH);
    return Vector4((float)(i % c) * u, (float)(i / c) * v, u, v);
}

const SpriteSheets::Info* SpriteSheets::Get(const std::string& pngPath)
{
    if (pngPath.empty()) return nullptr;
    auto it = g_Cache.find(pngPath);
    if (it != g_Cache.end()) return it->second.get();

    auto s = std::make_unique<Info>();
    s->path = pngPath;
    s->texture = ResourceManager::Get().LoadTexture(Widen(pngPath));
    if (!s->texture)
    {
        std::cout << "[SpriteSheets] load failed: " << pngPath << std::endl;
        g_Cache[pngPath] = nullptr;
        return nullptr;
    }
    s->texW = s->texture->GetWidth();
    s->texH = s->texture->GetHeight();
    s->cellW = s->texW;   // json が無ければ画像全体で 1 コマ
    s->cellH = s->texH;

    std::ifstream f(JsonPathOf(pngPath));
    if (f)
    {
        try
        {
            const nlohmann::json j = nlohmann::json::parse(f);
            if (j.value("schema", std::string()).rfind("pvfx.", 0) == 0 && j.contains("frames"))
                ReadPvfx(j, *s);
            else
                ReadSimple(j, *s);
        }
        catch (const std::exception& e)
        {
            std::cout << "[SpriteSheets] " << JsonPathOf(pngPath) << ": " << e.what() << std::endl;
        }
    }
    s->cellW = std::clamp(s->cellW, 1, (std::max)(1, s->texW));
    s->cellH = std::clamp(s->cellH, 1, (std::max)(1, s->texH));
    s->cols = (std::max)(1, s->texW / s->cellW);
    s->frameCount = std::clamp(s->frameCount, 1, s->cols * (std::max)(1, s->texH / s->cellH));

    const Info* out = s.get();
    g_Cache[pngPath] = std::move(s);
    return out;
}
