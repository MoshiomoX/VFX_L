#pragma once
#include <string>
#include <vector>
#include <SimpleMath.h>

class GPUParticleSystem;
class VFXMeshRenderer;

// ============================================================
// シーンが用意した Mesh 発射源（参照モデルの submesh 等）。
// Particle entry の Inspector で「Source」として選べる。
// 世界行列はシーンの refWorld を毎フレーム追う
// ============================================================
struct VFXRefEmitSource
{
    std::string name;        // 表示名（submesh 名）
    int         sourceId = -1;
    int         vertexCount = 0;
};
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
    VFXMeshRenderer* meshRenderer = nullptr;

    // シーンが持つ参照モデルの発射源（Editor 用。無ければ null）。
    // 実体はシーンの物なので、寿命はシーンに従う
    const std::vector<VFXRefEmitSource>* refSources = nullptr;
    const DirectX::SimpleMath::Matrix*   refWorld = nullptr;
    // 将来追加
    // LightManager* lightManager = nullptr;
    // SoundManager* soundManager = nullptr;
};