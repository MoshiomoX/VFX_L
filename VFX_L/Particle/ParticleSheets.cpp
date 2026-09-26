// ============================================================
// ParticleSheets.cpp
// ============================================================
#include "Particle/ParticleSheets.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Material/Texture.h"
#include "ResourcePaths.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace
{
    std::vector<ParticleSheets::Sheet> g_Sheets;
    bool g_Loaded = false;

    // "a/b/../c.png" → "a/c.png"。ResourceManager は文字列で共有するので、
    // シーンが直接読む旧 particlesSheet.jpg と同じ書き方に揃えて二重読みを防ぐ
    std::string Normalize(const std::string& path)
    {
        std::vector<std::string> parts;
        size_t begin = 0;
        while (begin <= path.size())
        {
            size_t end = path.find_first_of("/\\", begin);
            if (end == std::string::npos) end = path.size();
            const std::string part = path.substr(begin, end - begin);
            if (part == "..")
            {
                if (!parts.empty()) parts.pop_back();
            }
            else if (!part.empty() && part != ".")
            {
                parts.push_back(part);
            }
            begin = end + 1;
        }
        std::string out;
        for (size_t i = 0; i < parts.size(); ++i)
            out += (i ? "/" : "") + parts[i];
        return out;
    }

    std::string DirOf(const std::string& path)
    {
        const size_t slash = path.find_last_of("/\\");
        return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
    }

    // パスは ASCII だけ（Assets 以下）なので素直に広げる
    std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }
}

void ParticleSheets::Load()
{
    if (g_Loaded) return;
    g_Loaded = true;

    for (const char* manifest : Res::ParticleSheet::kManifests)
    {
        if ((int)g_Sheets.size() >= kMaxSheets) break;

        Sheet s;
        s.name = manifest;

        // 読めなくても番号は詰めない（後ろの貼图の番号がずれると特効が壊れる）
        std::ifstream f(manifest);
        if (!f)
        {
            std::cout << "[ParticleSheets] " << g_Sheets.size() << ": missing " << manifest << std::endl;
            g_Sheets.push_back(std::move(s));
            continue;
        }

        try
        {
            const nlohmann::json j = nlohmann::json::parse(f);
            s.name = j.value("name", s.name);
            s.rows = (std::max)(1, j.value("rows", 1));
            s.cols = (std::max)(1, j.value("cols", 1));
            s.point = (j.value("filter", std::string("linear")) == "point");
            s.premultiplied = j.value("premultiplied", false);

            if (j.contains("frames"))
                for (const auto& n : j["frames"])
                    s.frames.push_back(n.get<std::string>());

            if (j.contains("groups"))
                for (const auto& g : j["groups"])
                {
                    Group gr;
                    gr.name = g.value("name", std::string());
                    gr.start = (std::max)(0, g.value("start", 0));
                    gr.count = (std::max)(1, g.value("count", 1));
                    s.groups.push_back(gr);
                }

            const std::string tex = Normalize(DirOf(manifest) + j.value("texture", std::string()));
            s.texture = ResourceManager::Get().LoadTexture(Widen(tex));
        }
        catch (const std::exception& e)
        {
            std::cout << "[ParticleSheets] " << manifest << ": " << e.what() << std::endl;
        }

        std::cout << "[ParticleSheets] " << g_Sheets.size() << ": " << s.name
            << " (" << s.cols << "x" << s.rows << (s.point ? ", point" : "")
            << (s.texture ? ")" : ", no texture)") << std::endl;
        g_Sheets.push_back(std::move(s));
    }
}

int ParticleSheets::Count()
{
    Load();   // 初回の問い合わせで読む（弾の特効表は最初の描画より先に作られる）
    return (int)g_Sheets.size();
}

const ParticleSheets::Sheet* ParticleSheets::Get(int index)
{
    Load();
    return (index >= 0 && index < (int)g_Sheets.size()) ? &g_Sheets[index] : nullptr;
}
