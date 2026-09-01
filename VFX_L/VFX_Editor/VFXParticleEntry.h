#pragma once
#include "VFX_Editor/VFXEntry.h"
#include "Particle/GPUParticleEmitter.h"

class VFXParticleEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Particle; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;

    std::unique_ptr<VFXEntry> Clone() const override
    {
        auto copy = std::make_unique<VFXParticleEntry>();
        copy->startTime = startTime;
        copy->duration = duration;
        copy->isPlaying = false;
        copy->emitterData = emitterData;
        return copy;
    }
	json ToJson() const override;
	void FromJson(const json& j) override;

    GPUParticleEmitter emitterData;
    int runtimeID = -1;
};