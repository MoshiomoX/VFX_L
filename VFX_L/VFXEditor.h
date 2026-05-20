#pragma once
#include "VFXEffect.h"
#include "EntryType.h"
#include "Texture.h"
#include "GPUParticleSystem.h"
#include <memory>

class VFXEditor
{
public:
    void SetEffect(VFXEffect* effect) { m_Effect = effect; }
    void SetContext(const VFXContext& ctx) { m_Context = ctx; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
    void SetParticleSystem(GPUParticleSystem* system) { m_ParticleSystem = system; }

    void Draw();

private:
    void DrawSystemInfo();
    void DrawPlaybackControls();
    void DrawTimeline();
    void DrawEntryList();
    void DrawEntryInspector(int index);

    VFXEffect* m_Effect = nullptr;
    VFXContext m_Context;
    std::shared_ptr<Texture> m_Texture;
    GPUParticleSystem* m_ParticleSystem = nullptr;

    int m_SelectedEntry = -1;
    float m_TimelineScale = 50.0f;
};