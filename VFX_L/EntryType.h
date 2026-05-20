#pragma once

class GPUParticleSystem;

enum class EntryType
{
    Particle = 0,
    Sprite = 1,
    Trail = 2,
    Mesh = 3,
    Light = 4,
    Sound = 5,
};

struct VFXContext
{
    GPUParticleSystem* particleSystem = nullptr;
    // 将来追加
    // LightManager* lightManager = nullptr;
    // SoundManager* soundManager = nullptr;
};