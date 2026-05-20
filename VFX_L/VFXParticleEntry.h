#pragma once
#include "VFXEntry.h"
#include "GPUParticleEmitter.h"

class VFXParticleEntry : public VFXEntry
{
public:
    EntryType GetType() const override { return EntryType::Particle; }
    void OnPlay(const VFXContext& ctx) override;
    void OnStop(const VFXContext& ctx) override;
    void OnUpdate(float dt, const VFXContext& ctx) override;
    void OnImGui() override;

    GPUParticleEmitter emitterData;
    int runtimeID = -1;
};