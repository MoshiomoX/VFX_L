#include "VFXParticleEntry.h"
#include "GPUParticleSystem.h"
#include "imgui.h"

void VFXParticleEntry::OnPlay(const VFXContext& ctx)
{
    auto* system = ctx.particleSystem;
    if (!system) return;

    runtimeID = system->AddEmitter(
        emitterData.emitRate,
        emitterData.maxParticles);

    if (runtimeID >= 0)
    {
        auto* emitter = system->GetEmitter(runtimeID);
        if (emitter)
        {
            // パラメータコピー（同じ）
            emitter->emitType = emitterData.emitType;
            emitter->shape = emitterData.shape;
            emitter->position = emitterData.position;
            emitter->direction = emitterData.direction;
            emitter->speedRange = emitterData.speedRange;
            emitter->lifetimeRange = emitterData.lifetimeRange;
            emitter->sizeRange = emitterData.sizeRange;
            emitter->startColorMin = emitterData.startColorMin;
            emitter->startColorMax = emitterData.startColorMax;
            emitter->endColorMin = emitterData.endColorMin;
            emitter->endColorMax = emitterData.endColorMax;
            emitter->gravity = emitterData.gravity;
            emitter->dragCoeff = emitterData.dragCoeff;
            emitter->rotationRange = emitterData.rotationRange;
            emitter->angularVelRange = emitterData.angularVelRange;
            emitter->atlasRows = emitterData.atlasRows;
            emitter->atlasCols = emitterData.atlasCols;
            emitter->atlasIndex = emitterData.atlasIndex;
            emitter->atlasAnimate = emitterData.atlasAnimate;
            emitter->textureIndex = emitterData.textureIndex;
            emitter->colorKeyCount = emitterData.colorKeyCount;
            for (int k = 0; k < emitterData.colorKeyCount; k++)
                emitter->colorKeys[k] = emitterData.colorKeys[k];
        }
        isPlaying = true;
    }
}


void VFXParticleEntry::OnStop(const VFXContext& ctx)
{
    auto* system = ctx.particleSystem;
    if (!system) return;

    if (runtimeID >= 0)
    {
        system->RemoveEmitter(runtimeID);
        runtimeID = -1;
    }
    isPlaying = false;
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