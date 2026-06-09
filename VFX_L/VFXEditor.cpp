#include "VFXEditor.h"
#include "VFXParticleEntry.h"
#include "imgui.h"
#include <string>
#include <cstdio>
#include <cmath>
#include <fstream>
void VFXEditor::Draw()
{
    if (!m_Effect) return;

    ImGui::Begin("VFX Editor");
    DrawMenuBar();
    DrawSystemInfo();
    DrawPlaybackControls();
    DrawTimeline();
    DrawEntryList();
    ImGui::End();
}
void VFXEditor::DrawSystemInfo()
{
    if (m_ParticleSystem)
    {
        ImGui::Text("Alive: %u", m_ParticleSystem->GetAliveCount());
        ImGui::Text("Dead:  %u", m_ParticleSystem->GetDeadCount());
        ImGui::Text("Max:   %u", m_ParticleSystem->GetMaxParticles());
        ImGui::Separator();
    }

    // Effect名編集
    char name[128];
    strncpy_s(name, m_Effect->GetName().c_str(), sizeof(name));
    if (ImGui::InputText("Effect Name", name, sizeof(name)))
    {
        m_Effect->SetName(name);
        strncpy_s(m_SaveFileName, name, sizeof(m_SaveFileName));
    }

    ImGui::Text("Time: %.2f / %.2f", m_Effect->GetcurrentTime(), m_Effect->GetTotalDuration());
    ImGui::Separator();
}

void VFXEditor::DrawPlaybackControls()
{
    if (m_Effect->IsPlaying())
    {
        if (ImGui::Button("Stop"))
            m_Effect->Stop();
    }
    else
    {
        if (ImGui::Button("Play"))
            m_Effect->Play();
    }

    ImGui::SameLine();
    bool loop = m_Effect->IsLooping();
    if (ImGui::Checkbox("Loop", &loop))
        m_Effect->SetLooping(loop);

    ImGui::Separator();
}

void VFXEditor::DrawTimeline()
{
    ImGui::Text("Timeline");
    ImGui::SliderFloat("Zoom", &m_TimelineScale, 20.0f, 200.0f);

    float totalDuration = m_Effect->GetTotalDuration();
    if (totalDuration > 100.0f) totalDuration = 10.0f;

    float timelineWidth = totalDuration * m_TimelineScale;
    if (timelineWidth < 200.0f) timelineWidth = 200.0f;
    float timelineHeight = 20.0f + m_Effect->GetEntryCount() * 28.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize(timelineWidth + 60.0f, timelineHeight + 30.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // 背景
    drawList->AddRectFilled(canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(30, 30, 30, 255));

    // 時間目盛り
    float offsetX = 50.0f;
    for (float t = 0.0f; t <= totalDuration; t += 0.5f)
    {
        float x = canvasPos.x + offsetX + t * m_TimelineScale;
        bool isFull = (fmod(t, 1.0f) < 0.01f);

        drawList->AddLine(
            ImVec2(x, canvasPos.y),
            ImVec2(x, canvasPos.y + canvasSize.y),
            isFull ? IM_COL32(80, 80, 80, 255) : IM_COL32(50, 50, 50, 255));

        if (isFull)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0fs", t);
            drawList->AddText(ImVec2(x - 5, canvasPos.y + 2), IM_COL32(180, 180, 180, 255), buf);
        }
    }

    // 各Entryの横条
    const ImU32 entryColors[] = {
        IM_COL32(70, 130, 230, 200),
        IM_COL32(230, 180, 70, 200),
        IM_COL32(70, 230, 130, 200),
        IM_COL32(230, 70, 130, 200),
        IM_COL32(230, 230, 70, 200),
        IM_COL32(130, 70, 230, 200),
    };

    const ImU32 selectedColor = IM_COL32(255, 255, 255, 80);
    const char* typeNames[] = { "Particle", "Sprite", "Trail", "Mesh", "Light", "Sound" };

    float barY = canvasPos.y + 20.0f;
    for (int i = 0; i < m_Effect->GetEntryCount(); i++)
    {
        auto* entry = m_Effect->GetEntry(i);
        if (!entry) continue;

        float startX = canvasPos.x + offsetX + entry->startTime * m_TimelineScale;
        float dur = (entry->duration < 0.0f) ? totalDuration - entry->startTime : entry->duration;
        float endX = startX + dur * m_TimelineScale;
        float y = barY + i * 28.0f;

        int typeIdx = static_cast<int>(entry->GetType());
        ImU32 color = entryColors[typeIdx % 6];

        // 横条
        drawList->AddRectFilled(ImVec2(startX, y), ImVec2(endX, y + 22.0f), color, 4.0f);

        // 選択ハイライト
        if (i == m_SelectedEntry)
        {
            drawList->AddRect(ImVec2(startX - 1, y - 1), ImVec2(endX + 1, y + 23.0f),
                IM_COL32(255, 255, 255, 255), 4.0f, 0, 2.0f);
        }

        // ラベル
        char label[32];
        snprintf(label, sizeof(label), "%d:%s", i, typeNames[typeIdx]);
        drawList->AddText(ImVec2(startX + 4, y + 3), IM_COL32(255, 255, 255, 255), label);

        // クリックで選択 + ドラッグで移動
        ImGui::SetCursorScreenPos(ImVec2(startX, y));
        ImGui::InvisibleButton(("tl_entry" + std::to_string(i)).c_str(), ImVec2(endX - startX, 22.0f));

        if (ImGui::IsItemClicked())
        {
            m_SelectedEntry = i;
        }

        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
        {
            float delta = ImGui::GetIO().MouseDelta.x / m_TimelineScale;
            entry->startTime += delta;
            if (entry->startTime < 0.0f) entry->startTime = 0.0f;
        }
    }

    // 再生ヘッド
    if (m_Effect->IsPlaying())
    {
        float headX = canvasPos.x + offsetX + m_Effect->GetcurrentTime() * m_TimelineScale;
        drawList->AddLine(
            ImVec2(headX, canvasPos.y),
            ImVec2(headX, canvasPos.y + canvasSize.y),
            IM_COL32(255, 50, 50, 255), 2.0f);
    }

    ImGui::Dummy(canvasSize);
    ImGui::Separator();
}

void VFXEditor::DrawEntryList()
{
    if (ImGui::Button("+ Add Entry"))
    {
        ImGui::OpenPopup("AddEntryType");
    }

    if (ImGui::BeginPopup("AddEntryType"))
    {
        const char* typeNames[] = { "Particle", "Sprite", "Trail", "Mesh", "Light", "Sound" };
        for (int t = 0; t < 6; t++)
        {
            bool enabled = (t == 0);
            if (!enabled) ImGui::BeginDisabled();
            if (ImGui::Selectable(typeNames[t]))
            {
                m_Effect->AddEntry(static_cast<EntryType>(t), 0.0f, 2.0f);
            }
            if (!enabled) ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    const char* typeNames[] = { "Particle", "Sprite", "Trail", "Mesh", "Light", "Sound" };
    int removeIndex = -1;

    for (int i = 0; i < m_Effect->GetEntryCount(); i++)
    {
        auto* entry = m_Effect->GetEntry(i);
        if (!entry) continue;

        ImGui::PushID(i);

        int typeIdx = static_cast<int>(entry->GetType());
        std::string label = "Entry " + std::to_string(i) + " [" + typeNames[typeIdx] + "]";

        if (ImGui::CollapsingHeader(label.c_str()))
        {
            if (ImGui::Button("Delete"))
                removeIndex = i;

            ImGui::DragFloat("Start Time", &entry->startTime, 0.1f, 0.0f, 10.0f);
            ImGui::DragFloat("Duration", &entry->duration, 0.1f, -1.0f, 10.0f);
            ImGui::Text("Playing: %s", entry->isPlaying ? "Yes" : "No");
            ImGui::Separator();

            entry->OnImGui();

            // テクスチャプレビュー
            if (entry->GetType() == EntryType::Particle && m_Texture && m_Texture->IsValid())
            {
                auto* pEntry = static_cast<VFXParticleEntry*>(entry);
                auto& e = pEntry->emitterData;

                ImGui::Separator();
                ImGui::Text("Texture Preview:");

                float cellW = 1.0f / e.atlasCols;
                float cellH = 1.0f / e.atlasRows;
                int col = e.atlasIndex % e.atlasCols;
                int row = e.atlasIndex / e.atlasCols;

                ImVec2 uv0(col * cellW, row * cellH);
                ImVec2 uv1((col + 1) * cellW, (row + 1) * cellH);

                ImGui::Image((ImTextureID)m_Texture->GetSRV(), ImVec2(64, 64), uv0, uv1);
            }
        }

        ImGui::PopID();
    }

    if (removeIndex >= 0)
    {
        m_Effect->RemoveEntry(removeIndex);
    }
}
void VFXEditor::DrawEntryInspector(int index)
{
    auto* entry = m_Effect->GetEntry(index);
    if (!entry) return;

    const char* typeNames[] = { "Particle", "Sprite", "Trail", "Mesh", "Light", "Sound" };
    int typeIdx = static_cast<int>(entry->GetType());

    std::string title = "Inspector - Entry " + std::to_string(index) + " [" + typeNames[typeIdx] + "]";
    ImGui::Begin(title.c_str());

    // 削除ボタン
    if (ImGui::Button("Delete Entry"))
    {
        m_Effect->RemoveEntry(index);
        m_SelectedEntry = -1;
        ImGui::End();
        return;
    }

    ImGui::DragFloat("Start Time", &entry->startTime, 0.1f, 0.0f, 10.0f);
    ImGui::DragFloat("Duration", &entry->duration, 0.1f, -1.0f, 10.0f);
    ImGui::Text("Playing: %s", entry->isPlaying ? "Yes" : "No");
    ImGui::Separator();

    // 各タイプ固有のImGui
    entry->OnImGui();

    // テクスチャプレビュー（Particleの場合）
    if (entry->GetType() == EntryType::Particle && m_Texture && m_Texture->IsValid())
    {
        auto* pEntry = static_cast<VFXParticleEntry*>(entry);
        auto& e = pEntry->emitterData;

        ImGui::Separator();
        ImGui::Text("Texture Preview:");

        float cellW = 1.0f / e.atlasCols;
        float cellH = 1.0f / e.atlasRows;
        int col = e.atlasIndex % e.atlasCols;
        int row = e.atlasIndex / e.atlasCols;

        ImVec2 uv0(col * cellW, row * cellH);
        ImVec2 uv1((col + 1) * cellW, (row + 1) * cellH);

        ImGui::Image((ImTextureID)m_Texture->GetSRV(), ImVec2(64, 64), uv0, uv1);
    }

    ImGui::End();
}
void VFXEditor::DrawMenuBar()
{
    if (ImGui::Button("Save"))
    {
        ImGui::OpenPopup("SavePopup");
    }

    ImGui::SameLine();

    if (ImGui::Button("Load"))
    {
        ImGui::OpenPopup("LoadPopup");
    }

    // Save
    if (ImGui::BeginPopup("SavePopup"))
    {
        ImGui::InputText("File Name", m_SaveFileName, sizeof(m_SaveFileName));

        if (ImGui::Button("Save##confirm"))
        {
            std::string path = "Assets/Data/VFXData/" + std::string(m_SaveFileName) + ".json";

            std::ifstream check(path);
            if (check.is_open())
            {
                check.close();
                m_ShowOverwriteConfirm = true;
            }
            else
            {
                m_Effect->SaveToFile(path);
                ImGui::CloseCurrentPopup();
            }
        }

        if (m_ShowOverwriteConfirm)
        {
            ImGui::Separator();
            ImGui::Text("File already exists. Overwrite?");
            if (ImGui::Button("Yes"))
            {
                std::string path = "Assets/Data/VFXData/" + std::string(m_SaveFileName) + ".json";
                m_Effect->SaveToFile(path);
                m_ShowOverwriteConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("No"))
            {
                m_ShowOverwriteConfirm = false;
            }
        }

        ImGui::EndPopup();
    }

    // Load
    if (ImGui::BeginPopup("LoadPopup"))
    {
        // Assets/VFX/内の.jsonファイルを列挙
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA("Assets/Data/VFXData/*.json", &fd);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (ImGui::Selectable(fd.cFileName))
                {
                    std::string path = "Assets/Data/VFXData/" + std::string(fd.cFileName);
                    m_Effect->Stop();
                    m_Effect->LoadFromFile(path);
                    ImGui::CloseCurrentPopup();
                }
            } while (FindNextFileA(hFind, &fd));
            FindClose(hFind);
        }
        else
        {
            ImGui::Text("No files found");
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();
}