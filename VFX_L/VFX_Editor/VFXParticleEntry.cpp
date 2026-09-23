#include "VFX_Editor/VFXParticleEntry.h"
#include "VFX_Editor/VFXFileList.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Mesh/Mesh.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <iostream>

VFXParticleEntry::~VFXParticleEntry()
{
    UnregisterFileSource();
    ReleaseTrailStyle();
}

// ============================================
// 軌跡（帯）の style
//   OnStop では解除しない。止めた後も生きている粒子が帯を引いているので、
//   解除すると帯だけ先に消える。解除するのは OFF にした時と破棄の時だけ
// ============================================
void VFXParticleEntry::SyncTrailStyle(const VFXContext& ctx)
{
    if (!trailEnabled)
    {
        ReleaseTrailStyle();
        return;
    }
    if (!ctx.particleSystem) return;

    trail.texture = trailTex.texture;

    if (m_TrailStyleId < 0)
    {
        m_TrailStyleId = ctx.particleSystem->RegisterTrailStyle(trail);
        m_TrailOwner = (m_TrailStyleId >= 0) ? ctx.particleSystem : nullptr;
    }
    else
    {
        ctx.particleSystem->UpdateTrailStyle(m_TrailStyleId, trail);
    }
}

void VFXParticleEntry::ReleaseTrailStyle()
{
    if (m_TrailStyleId >= 0 && m_TrailOwner)
        m_TrailOwner->UnregisterTrailStyle(m_TrailStyleId);
    m_TrailStyleId = -1;
    m_TrailOwner = nullptr;
}

void VFXParticleEntry::OnPlay(const VFXContext& ctx)
{
    isPlaying = true;
    emitterData.SetActive(true);

    if (emitterData.emitType == EmitType::Mesh && !externalSource)
        RegisterFileSource(ctx);

    SyncTrailStyle(ctx);   // 最初の発射より前に style を登録しておく
}


void VFXParticleEntry::OnStop(const VFXContext& ctx)
{
    isPlaying = false;
    emitterData.SetActive(false);

    // 自分で登録した源だけ返す。external はシーンの物
    UnregisterFileSource();
}
void VFXParticleEntry::OnUpdate(float dt, const VFXContext& ctx)
{
    SyncTrailStyle(ctx);

    // 粒子の更新は GPUParticleSystem がやる。
    // ここでは Inspector で発射源のモデルが変わった時の登録し直しだけ
    if (m_SourceDirty && emitterData.emitType == EmitType::Mesh && !externalSource)
    {
        UnregisterFileSource();
        RegisterFileSource(ctx);
    }
    m_SourceDirty = false;
}

// ============================================
// Mesh 発射源
// ============================================
void VFXParticleEntry::RegisterFileSource(const VFXContext& ctx)
{
    if (m_SourceId >= 0) return;          // 登録済み
    if (!ctx.particleSystem) return;
    if (sourceModelPath.empty()) return;

    m_SourceModel = ResourceManager::Get().LoadModel(sourceModelPath);
    if (!m_SourceModel || m_SourceModel->GetSubMeshes().empty() || !m_SourceModel->GetSubMeshes()[0].mesh)
    {
        std::cout << "[VFXParticleEntry] source model load failed: " << sourceModelPath << std::endl;
        return;
    }

    // 第 0 SubMesh を源にする（複数 SubMesh の合成は今はしない）。index も渡して面から出す
    const auto& mesh = m_SourceModel->GetSubMeshes()[0].mesh;
    m_SourceId = ctx.particleSystem->RegisterEmitSource(
        mesh->GetVertexSRV(), mesh->GetVertexCount(), GPUParticleSystem::kLayoutStatic,
        mesh->GetIndexSRV(), mesh->GetIndexCount(), mesh->GetIndexBytes());
    m_SourceOwner = ctx.particleSystem;

    emitterData.shape.sourceId = m_SourceId;
    emitterData.shape.sourceCount = (m_SourceId >= 0) ? (int)mesh->GetVertexCount() : 0;
}

void VFXParticleEntry::UnregisterFileSource()
{
    if (m_SourceId >= 0 && m_SourceOwner)
        m_SourceOwner->UnregisterEmitSource(m_SourceId);
    m_SourceId = -1;
    m_SourceOwner = nullptr;
    m_SourceModel.reset();

    if (!externalSource)
    {
        emitterData.shape.sourceId = -1;
        emitterData.shape.sourceCount = 0;
    }
}

void VFXParticleEntry::SetExternalSource(int sourceId, int vertexCount, const DirectX::SimpleMath::Matrix* world)
{
    UnregisterFileSource();
    externalSource = true;
    followWorld = world;
    emitterData.emitType = EmitType::Mesh;
    emitterData.shape.sourceId = sourceId;
    emitterData.shape.sourceCount = vertexCount;
}

void VFXParticleEntry::ClearExternalSource()
{
    externalSource = false;
    followWorld = nullptr;
    emitterData.shape.sourceId = -1;
    emitterData.shape.sourceCount = 0;
    m_SourceDirty = true;   // 再生中なら次の OnUpdate で静的モデルへ戻る
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
    case EmitType::Mesh:
    {
        // ---- Source: ファイルのモデル / シーンの参照モデル（submesh ごと）----
        const std::vector<VFXRefEmitSource>* refs =
            (inspectorCtx && inspectorCtx->refSources) ? inspectorCtx->refSources : nullptr;

        std::vector<std::string> names;
        names.push_back("File model");
        if (refs)
            for (const auto& r : *refs) names.push_back("Ref: " + r.name);

        int current = 0;
        if (externalSource && refs)
            for (int i = 0; i < (int)refs->size(); ++i)
                if ((*refs)[i].sourceId == e.shape.sourceId) { current = i + 1; break; }

        // "a\0b\0c\0\0" 形式（ImGui の版差に依らない Combo）
        std::string zeroSep;
        for (const auto& s : names) { zeroSep += s; zeroSep.push_back('\0'); }
        zeroSep.push_back('\0');
        if (ImGui::Combo("Source", &current, zeroSep.c_str()))
        {
            if (current == 0)
                ClearExternalSource();
            else if (refs && current - 1 < (int)refs->size())
            {
                const auto& r = (*refs)[current - 1];
                SetExternalSource(r.sourceId, r.vertexCount, inspectorCtx->refWorld);
            }
        }

        if (!externalSource)
        {
            if (VFXFileList::Combo("Source Model", Res::Dir::VFXMesh,
                { ".fbx", ".obj", ".gltf", ".glb" }, sourceModelPath))
                m_SourceDirty = true;
        }
        else
        {
            ImGui::TextDisabled("follows the reference model's world matrix (Model Pos/Rot/Scale)");
        }
        ImGui::Text("source id %d  (%d verts)", e.shape.sourceId, e.shape.sourceCount);
        {
            bool edge = (e.shape.edgeMode != 0);
            if (ImGui::Checkbox("Edge Mode (dissolve edge only)", &edge))
                e.shape.edgeMode = edge ? 1 : 0;
            if (edge)
                ImGui::TextDisabled("reads dissolve + noise of the first Mesh entry in this effect");
        }
        break;
    }
    default:
        break;
    }
    ImGui::Separator();

    ImGui::DragFloat3("Position", &e.position.x, 0.1f);
    ImGui::DragFloat3("Direction", &e.direction.x, 0.01f);
    ImGui::Separator();

    const char* renderNames[] = { "Billboard", "Cube" };
    ImGui::Combo("Render", &e.renderMode, renderNames, IM_ARRAYSIZE(renderNames));
    if (e.renderMode == 1)
        ImGui::TextDisabled("cube: opaque, lit, rotation ranges apply to all 3 axes");
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
    ImGui::Separator();

    // ---- 軌跡（帯）：粒子 1 個ずつが引く。GPU 上で完結 ----
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Trail (ribbon per particle)");
    ImGui::Checkbox("Trail Enabled", &trailEnabled);
    if (trailEnabled)
    {
        ImGui::PushID("trail");
        if (m_TrailStyleId < 0)
            ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "not registered yet (press Play) or no free style slot");

        ImGui::DragFloat("Length (sec)", &trail.lifetime, 0.01f, 0.02f, 10.0f);
        ImGui::DragFloat("Width Head", &trail.widthHead, 0.005f, 0.0f, 20.0f);
        ImGui::DragFloat("Width Tail", &trail.widthTail, 0.005f, 0.0f, 20.0f);
        ImGui::Checkbox("Width x Particle Size", &trail.inheritSize);
        ImGui::ColorEdit4("Color Head", &trail.colorHead.x);
        ImGui::ColorEdit4("Color Tail", &trail.colorTail.x);
        ImGui::Checkbox("Color x Particle Color", &trail.inheritColor);
        ImGui::DragFloat("Intensity", &trail.intensity, 0.05f, 0.0f, 50.0f);
        ImGui::SliderFloat("Soft Edge", &trail.softEdge, 0.0f, 1.0f);
        const char* trailBlendNames[] = { "Additive", "Alpha" };
        ImGui::Combo("Blend", &trail.blend, trailBlendNames, 2);
        trailTex.OnImGui("Trail Texture", "Assets/VFX/Tex");
        ImGui::DragFloat("UV Repeat", &trail.uvRepeat, 0.05f, 0.01f, 50.0f);
        ImGui::DragFloat("UV Scroll", &trail.uvScroll, 0.01f, -20.0f, 20.0f);
        ImGui::TextDisabled("particles need speed: a still particle has no ribbon");
        ImGui::PopID();
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

    j["source"] = sourceModelPath;
    j["edgeMode"] = e.shape.edgeMode;
    j["renderMode"] = e.renderMode;

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

    // ---- 軌跡（帯）----
    {
        json t;
        t["enabled"] = trailEnabled;
        t["lifetime"] = trail.lifetime;
        t["widthHead"] = trail.widthHead;
        t["widthTail"] = trail.widthTail;
        t["inheritSize"] = trail.inheritSize;
        t["colorHead"] = { trail.colorHead.x, trail.colorHead.y, trail.colorHead.z, trail.colorHead.w };
        t["colorTail"] = { trail.colorTail.x, trail.colorTail.y, trail.colorTail.z, trail.colorTail.w };
        t["inheritColor"] = trail.inheritColor;
        t["intensity"] = trail.intensity;
        t["softEdge"] = trail.softEdge;
        t["blend"] = trail.blend;
        t["uvRepeat"] = trail.uvRepeat;
        t["uvScroll"] = trail.uvScroll;
        t["tex"] = trailTex.ToJson();
        j["trail"] = t;
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

    sourceModelPath = j.value("source", "");
    e.shape.edgeMode = j.value("edgeMode", 0);
    e.renderMode = j.value("renderMode", 0);
    e.shape.sourceId = -1;      // 登録は OnPlay で
    e.shape.sourceCount = 0;

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

    // ---- 軌跡（帯）。古い json には無い → 既定値（OFF）のまま ----
    if (j.contains("trail"))
    {
        const json& t = j["trail"];
        trailEnabled = t.value("enabled", false);
        trail.lifetime = t.value("lifetime", trail.lifetime);
        trail.widthHead = t.value("widthHead", trail.widthHead);
        trail.widthTail = t.value("widthTail", trail.widthTail);
        trail.inheritSize = t.value("inheritSize", trail.inheritSize);
        auto ch = t.value("colorHead", std::vector<float>{1, 1, 1, 1});
        if (ch.size() >= 4) trail.colorHead = { ch[0], ch[1], ch[2], ch[3] };
        auto ct = t.value("colorTail", std::vector<float>{1, 1, 1, 0});
        if (ct.size() >= 4) trail.colorTail = { ct[0], ct[1], ct[2], ct[3] };
        trail.inheritColor = t.value("inheritColor", trail.inheritColor);
        trail.intensity = t.value("intensity", trail.intensity);
        trail.softEdge = t.value("softEdge", trail.softEdge);
        trail.blend = t.value("blend", trail.blend);
        trail.uvRepeat = t.value("uvRepeat", trail.uvRepeat);
        trail.uvScroll = t.value("uvScroll", trail.uvScroll);
        if (t.contains("tex")) trailTex.FromJson(t["tex"]);
    }
    else
    {
        trailEnabled = false;
    }
}
