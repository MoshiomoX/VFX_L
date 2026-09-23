// ============================================================
// VFXTextureRef.cpp
// ============================================================
#include "VFX_Editor/VFXTextureRef.h"
#include "VFX_Editor/VFXFileList.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Material/Texture.h"
#include "imgui.h"
#include <sstream>
#include <iostream>

// ---------------- NoiseRecipe ----------------

std::string NoiseRecipe::CacheKey() const
{
    std::ostringstream ss;
    ss << "noise:" << (int)type << ":" << size << ":" << frequency
        << ":" << octaves << ":" << persistence << ":" << seed;
    return ss.str();
}

json NoiseRecipe::ToJson() const
{
    return {
        { "type", (int)type }, { "size", size }, { "frequency", frequency },
        { "octaves", octaves }, { "persistence", persistence }, { "seed", seed }
    };
}

void NoiseRecipe::FromJson(const json& j)
{
    type = (Type)j.value("type", 0);
    size = j.value("size", 256);
    frequency = j.value("frequency", 4);
    octaves = j.value("octaves", 4);
    persistence = j.value("persistence", 0.5f);
    seed = j.value("seed", 1u);
}

bool NoiseRecipe::OnImGui()
{
    bool changed = false;
    const char* names[] = { "Perlin", "Worley", "FBM" };
    int t = (int)type;
    if (ImGui::Combo("Type", &t, names, 3)) { type = (Type)t; changed = true; }

    const char* sizes[] = { "64", "128", "256", "512" };
    int sIdx = (size <= 64) ? 0 : (size <= 128) ? 1 : (size <= 256) ? 2 : 3;
    if (ImGui::Combo("Size", &sIdx, sizes, 4)) { size = 64 << sIdx; changed = true; }

    changed |= ImGui::DragInt("Frequency", &frequency, 1, 1, 64);
    if (type == Type::FBM)
    {
        changed |= ImGui::DragInt("Octaves", &octaves, 1, 1, 8);
        changed |= ImGui::DragFloat("Persistence", &persistence, 0.01f, 0.1f, 1.0f);
    }
    int seedI = (int)seed;
    if (ImGui::DragInt("Seed", &seedI, 1, 0, 100000)) { seed = (uint32_t)seedI; changed = true; }
    return changed;
}

// ---------------- VFXTextureRef ----------------

void VFXTextureRef::Resolve()
{
    texture.reset();
    switch (source)
    {
    case Source::File:
        if (!file.empty())
            texture = ResourceManager::Get().LoadTexture(std::wstring(file.begin(), file.end()));
        break;
    case Source::Generated:
        texture = ResourceManager::Get().LoadNoiseTexture(recipe);
        break;
    default:
        break;
    }
}

json VFXTextureRef::ToJson() const
{
    json j;
    j["source"] = (int)source;
    if (source == Source::File)      j["file"] = file;
    if (source == Source::Generated) j["gen"] = recipe.ToJson();
    return j;
}

void VFXTextureRef::FromJson(const json& j)
{
    source = (Source)j.value("source", 0);
    file = j.value("file", "");
    if (j.contains("gen")) recipe.FromJson(j["gen"]);
    Resolve();
}

bool VFXTextureRef::OnImGui(const char* label, const char* dir)
{
    bool changed = false;
    ImGui::PushID(label);

    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "%s", label);
    const char* srcNames[] = { "None", "File", "Generated" };
    int s = (int)source;
    if (ImGui::Combo("Source", &s, srcNames, 3)) { source = (Source)s; changed = true; }

    if (source == Source::File)
        changed |= VFXFileList::Combo("File", dir, { ".png", ".jpg", ".dds", ".tga" }, file);
    else if (source == Source::Generated)
        changed |= recipe.OnImGui();

    if (changed) Resolve();

    if (texture && texture->IsValid())
    {
        ImGui::Image((ImTextureID)texture->GetSRV(), ImVec2(64, 64));
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d", texture->GetWidth(), texture->GetHeight());
    }

    ImGui::PopID();
    return changed;
}