#include "VFXParticleEntry.h"
#include "GPUParticleSystem.h"
#include "imgui.h"

void VFXParticleEntry::OnPlay(const VFXContext& ctx)
{
    isPlaying = true;
    emitterData.SetActive(true);
}


void VFXParticleEntry::OnStop(const VFXContext& ctx)
{
    isPlaying = false;
    emitterData.SetActive(false);
}
void VFXParticleEntry::OnUpdate(float dt, const VFXContext& ctx)
{
    // GPUParticleSystemが更新するので何もしない
}

void VFXParticleEntry::OnImGui()
{
    auto& e = emitterData;

    // 形状
    const char* shapeNames[] = { "Point", "Sphere", "Cone", "Box", "Ring", "Disc", "Mesh" };
    int currentType = static_cast<int>(e.emitType);
    if (ImGui::Combo("Shape", &currentType, shapeNames, IM_ARRAYSIZE(shapeNames)))
        e.emitType = static_cast<EmitType>(currentType);

    switch (e.emitType)
    {
    case EmitType::Point:
        ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 180.0f);
        break;
    case EmitType::Sphere:
        ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Cone:
        ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 90.0f);
        ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Box:
        ImGui::DragFloat3("Extents", &e.shape.boxExtents.x, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Ring:
        ImGui::DragFloat("Outer Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Inner Radius", &e.shape.innerRadius, 0.1f, 0.0f, 50.0f);
        break;
    case EmitType::Disc:
        ImGui::DragFloat("Radius", &e.shape.radius, 0.1f, 0.0f, 50.0f);
        ImGui::SliderFloat("Spread", &e.shape.spreadAngle, 0.0f, 180.0f);
        break;
    default:
        break;
    }
    ImGui::Separator();

    ImGui::DragFloat3("Position", &e.position.x, 0.1f);
    ImGui::DragFloat3("Direction", &e.direction.x, 0.01f);
    ImGui::Separator();

    ImGui::SliderFloat("Rate", &e.emitRate, 0.0f, 1000.0f);
    ImGui::DragInt("Max Particles", &e.maxParticles, 100, 100, 50000);
    ImGui::DragFloat2("Speed", &e.speedRange.x, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat2("Lifetime", &e.lifetimeRange.x, 0.1f, 0.1f, 10.0f);
    ImGui::Separator();

    ImGui::DragFloat4("Size", &e.sizeRange.x, 0.01f, 0.0f, 5.0f);
    ImGui::Separator();

    // Color over Lifetime
    ImGui::Text("Color over Lifetime");
    bool useColorKeys = (e.colorKeyCount > 0);
    if (ImGui::Checkbox("Use Color Keys", &useColorKeys))
    {
        if (useColorKeys && e.colorKeyCount == 0)
        {
            e.colorKeyCount = 2;
            e.colorKeys[0] = { {1,1,1,1}, 0.0f, 0,0,0 };
            e.colorKeys[1] = { {1,1,1,0}, 1.0f, 0,0,0 };
        }
        else if (!useColorKeys)
        {
            e.colorKeyCount = 0;
        }
    }

    if (e.colorKeyCount > 0)
    {
        if (e.colorKeyCount < GPUParticleEmitter::MAX_COLOR_KEYS)
        {
            if (ImGui::Button("+ Add Key"))
            {
                e.colorKeys[e.colorKeyCount] = { {1,1,1,1}, 1.0f, 0,0,0 };
                e.colorKeyCount++;
            }
        }

        int removeKey = -1;
        for (int k = 0; k < e.colorKeyCount; k++)
        {
            ImGui::PushID(k + 1000);
            ImGui::ColorEdit4(("Key " + std::to_string(k)).c_str(), &e.colorKeys[k].color.x);
            ImGui::SliderFloat("Time", &e.colorKeys[k].time, 0.0f, 1.0f);
            if (e.colorKeyCount > 2)
            {
                ImGui::SameLine();
                if (ImGui::Button("X"))
                    removeKey = k;
            }
            ImGui::PopID();
        }

        if (removeKey >= 0)
        {
            for (int k = removeKey; k < e.colorKeyCount - 1; k++)
                e.colorKeys[k] = e.colorKeys[k + 1];
            e.colorKeyCount--;
        }

        // 渐变条
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float barWidth = 200.0f;
        float barHeight = 20.0f;

        for (int x = 0; x < (int)barWidth; x++)
        {
            float t = x / barWidth;
            Vector4 col = e.colorKeys[0].color;
            for (int k = 1; k < e.colorKeyCount; k++)
            {
                if (t >= e.colorKeys[k - 1].time && t <= e.colorKeys[k].time)
                {
                    float diff = e.colorKeys[k].time - e.colorKeys[k - 1].time;
                    float localT = (diff > 0.0001f) ? (t - e.colorKeys[k - 1].time) / diff : 0.0f;
                    col = Vector4::Lerp(e.colorKeys[k - 1].color, e.colorKeys[k].color, localT);
                    break;
                }
                if (t > e.colorKeys[k].time)
                    col = e.colorKeys[k].color;
            }
            ImU32 imCol = IM_COL32((int)(col.x * 255), (int)(col.y * 255),
                (int)(col.z * 255), (int)(col.w * 255));
            drawList->AddLine(ImVec2(pos.x + x, pos.y), ImVec2(pos.x + x, pos.y + barHeight), imCol);
        }
        ImGui::Dummy(ImVec2(barWidth, barHeight + 4));
    }
    else
    {
        ImGui::ColorEdit4("Start Min", &e.startColorMin.x);
        ImGui::ColorEdit4("Start Max", &e.startColorMax.x);
        ImGui::ColorEdit4("End Min", &e.endColorMin.x);
        ImGui::ColorEdit4("End Max", &e.endColorMax.x);
    }
    ImGui::Separator();

    ImGui::DragFloat3("Gravity", &e.gravity.x, 0.1f);
    ImGui::DragFloat("Drag", &e.dragCoeff, 0.01f, 0.0f, 5.0f);
    ImGui::Separator();

    ImGui::DragFloat2("Rotation", &e.rotationRange.x, 0.01f);
    ImGui::DragFloat2("Angular Vel", &e.angularVelRange.x, 0.01f);
    ImGui::Separator();

    ImGui::DragInt("Atlas Rows", &e.atlasRows, 1, 1, 16);
    ImGui::DragInt("Atlas Cols", &e.atlasCols, 1, 1, 16);
    ImGui::Checkbox("Atlas Animate", &e.atlasAnimate);
    if (!e.atlasAnimate)
    {
        int maxIdx = e.atlasRows * e.atlasCols - 1;
        ImGui::SliderInt("Atlas Index", &e.atlasIndex, 0, maxIdx);
    }
}

json VFXParticleEntry::ToJson() const
{
    json j;
    auto& e = emitterData;

    j["emitType"] = static_cast<int>(e.emitType);
    j["position"] = { e.position.x, e.position.y, e.position.z };
    j["direction"] = { e.direction.x, e.direction.y, e.direction.z };
    j["spreadAngle"] = e.shape.spreadAngle;
    j["radius"] = e.shape.radius;
    j["innerRadius"] = e.shape.innerRadius;
    j["boxExtents"] = { e.shape.boxExtents.x, e.shape.boxExtents.y, e.shape.boxExtents.z };
    j["emitRate"] = e.emitRate;
    j["maxParticles"] = e.maxParticles;
    j["speedRange"] = { e.speedRange.x, e.speedRange.y };
    j["lifetimeRange"] = { e.lifetimeRange.x, e.lifetimeRange.y };
    j["sizeRange"] = { e.sizeRange.x, e.sizeRange.y, e.sizeRange.z, e.sizeRange.w };
    j["startColorMin"] = { e.startColorMin.x, e.startColorMin.y, e.startColorMin.z, e.startColorMin.w };
    j["startColorMax"] = { e.startColorMax.x, e.startColorMax.y, e.startColorMax.z, e.startColorMax.w };
    j["endColorMin"] = { e.endColorMin.x, e.endColorMin.y, e.endColorMin.z, e.endColorMin.w };
    j["endColorMax"] = { e.endColorMax.x, e.endColorMax.y, e.endColorMax.z, e.endColorMax.w };
    j["gravity"] = { e.gravity.x, e.gravity.y, e.gravity.z };
    j["dragCoeff"] = e.dragCoeff;
    j["rotationRange"] = { e.rotationRange.x, e.rotationRange.y };
    j["angularVelRange"] = { e.angularVelRange.x, e.angularVelRange.y };
    j["atlasRows"] = e.atlasRows;
    j["atlasCols"] = e.atlasCols;
    j["atlasIndex"] = e.atlasIndex;
    j["atlasAnimate"] = e.atlasAnimate;

    if (e.colorKeyCount > 0)
    {
        json keys = json::array();
        for (int k = 0; k < e.colorKeyCount; k++)
        {
            json key;
            key["color"] = { e.colorKeys[k].color.x, e.colorKeys[k].color.y,
                             e.colorKeys[k].color.z, e.colorKeys[k].color.w };
            key["time"] = e.colorKeys[k].time;
            keys.push_back(key);
        }
        j["colorKeys"] = keys;
    }

    return j;
}

void VFXParticleEntry::FromJson(const json& j)
{
    auto& e = emitterData;

    e.emitType = static_cast<EmitType>(j.value("emitType", 0));

    auto pos = j.value("position", std::vector<float>{0, 0, 0});
    e.position = { pos[0], pos[1], pos[2] };

    auto dir = j.value("direction", std::vector<float>{0, 1, 0});
    e.direction = { dir[0], dir[1], dir[2] };

    e.shape.spreadAngle = j.value("spreadAngle", 0.0f);
    e.shape.radius = j.value("radius", 1.0f);
    e.shape.innerRadius = j.value("innerRadius", 0.0f);

    auto box = j.value("boxExtents", std::vector<float>{1, 1, 1});
    e.shape.boxExtents = { box[0], box[1], box[2] };

    e.emitRate = j.value("emitRate", 10.0f);
    e.maxParticles = j.value("maxParticles", 1000);

    auto spd = j.value("speedRange", std::vector<float>{1, 3});
    e.speedRange = { spd[0], spd[1] };

    auto life = j.value("lifetimeRange", std::vector<float>{1, 3});
    e.lifetimeRange = { life[0], life[1] };

    auto sz = j.value("sizeRange", std::vector<float>{0.1f, 0.3f, 0.0f, 0.1f});
    e.sizeRange = { sz[0], sz[1], sz[2], sz[3] };

    auto scmin = j.value("startColorMin", std::vector<float>{1, 1, 1, 1});
    e.startColorMin = { scmin[0], scmin[1], scmin[2], scmin[3] };

    auto scmax = j.value("startColorMax", std::vector<float>{1, 1, 1, 1});
    e.startColorMax = { scmax[0], scmax[1], scmax[2], scmax[3] };

    auto ecmin = j.value("endColorMin", std::vector<float>{1, 1, 1, 0});
    e.endColorMin = { ecmin[0], ecmin[1], ecmin[2], ecmin[3] };

    auto ecmax = j.value("endColorMax", std::vector<float>{1, 1, 1, 0});
    e.endColorMax = { ecmax[0], ecmax[1], ecmax[2], ecmax[3] };

    auto grav = j.value("gravity", std::vector<float>{0, -9.81f, 0});
    e.gravity = { grav[0], grav[1], grav[2] };

    e.dragCoeff = j.value("dragCoeff", 0.0f);

    auto rot = j.value("rotationRange", std::vector<float>{0, 0});
    e.rotationRange = { rot[0], rot[1] };

    auto angvel = j.value("angularVelRange", std::vector<float>{0, 0});
    e.angularVelRange = { angvel[0], angvel[1] };

    e.atlasRows = j.value("atlasRows", 1);
    e.atlasCols = j.value("atlasCols", 1);
    e.atlasIndex = j.value("atlasIndex", 0);
    e.atlasAnimate = j.value("atlasAnimate", false);

    if (j.contains("colorKeys"))
    {
        auto& keys = j["colorKeys"];
        e.colorKeyCount = static_cast<int>(keys.size());
        for (int k = 0; k < e.colorKeyCount && k < GPUParticleEmitter::MAX_COLOR_KEYS; k++)
        {
            auto col = keys[k]["color"].get<std::vector<float>>();
            e.colorKeys[k].color = { col[0], col[1], col[2], col[3] };
            e.colorKeys[k].time = keys[k]["time"];
        }
    }
}
