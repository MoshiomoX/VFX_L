// ============================================================
// VFXFileList.cpp
// ============================================================
#include "VFX_Editor/VFXFileList.h"
#include "imgui.h"
#include <filesystem>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

namespace
{
    std::map<std::string, std::vector<std::string>> s_Cache;

    std::string MakeKey(const std::string& dir, std::initializer_list<const char*> exts)
    {
        std::string k = dir;
        for (auto e : exts) { k += "|"; k += e; }
        return k;
    }
}

const std::vector<std::string>& VFXFileList::List(const std::string& dir,
    std::initializer_list<const char*> exts)
{
    const std::string key = MakeKey(dir, exts);
    auto it = s_Cache.find(key);
    if (it != s_Cache.end()) return it->second;

    std::vector<std::string> out;
    if (fs::exists(dir))
    {
        for (auto& p : fs::recursive_directory_iterator(dir))
        {
            if (!p.is_regular_file()) continue;
            std::string ext = p.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            for (auto e : exts)
                if (ext == e) { out.push_back(p.path().generic_string()); break; }
        }
        std::sort(out.begin(), out.end());
    }
    return s_Cache[key] = std::move(out);
}

void VFXFileList::Refresh() { s_Cache.clear(); }

bool VFXFileList::Combo(const char* label, const std::string& dir,
    std::initializer_list<const char*> exts, std::string& selected)
{
    const auto& files = List(dir, exts);
    bool changed = false;

    const char* preview = selected.empty() ? "(none)" : selected.c_str();
    if (ImGui::BeginCombo(label, preview))
    {
        if (ImGui::Selectable("(none)", selected.empty())) { selected.clear(); changed = true; }
        for (const auto& f : files)
        {
            const bool sel = (f == selected);
            if (ImGui::Selectable(f.c_str(), sel)) { selected = f; changed = true; }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("R")) Refresh();
    return changed;
}