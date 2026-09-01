#pragma once
#include "VFX_Editor/VFXEffect.h"
#include "VFX_Editor/EntryType.h"
#include "Graphics/Material/Texture.h"
#include "Particle/GPUParticleSystem.h"
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
    void DrawMenuBar();
    
    char m_SaveFileName[128] = "NewEffect";
    VFXEffect* m_Effect = nullptr;
    VFXContext m_Context;
    std::shared_ptr<Texture> m_Texture;
    GPUParticleSystem* m_ParticleSystem = nullptr;

    int m_SelectedEntry = -1;
    float m_TimelineScale = 50.0f;
    bool m_ShowOverwriteConfirm = false;

};