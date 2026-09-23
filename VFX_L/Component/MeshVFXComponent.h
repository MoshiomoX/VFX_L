// ============================================================
// MeshVFXComponent.h
// 実体のモデル表面から粒子を出す VFX 実例（受击火花、燃焼消滅など）。
// テンプレートから CloneFrom した効果を 1 実体ごとに持つ。
//
//   Shape = Mesh で source 未指定の Particle entry は、
//   Attach 時に宿主の ModelComponent の Mesh へ束ねられる。
//   world は毎フレーム TransformComponent から更新され、
//   entry の followWorld がここを指す（heap に置いて住所を固定する）
// ============================================================
#pragma once
#include "VFX_Editor/VFXEffect.h"
#include <SimpleMath.h>
#include <memory>

class Model;

struct MeshVFXComponent
{
    std::shared_ptr<VFXEffect> effect;
    std::shared_ptr<DirectX::SimpleMath::Matrix> world;   // followWorld の指す先。pool の再配置に耐える

    int    sourceId = -1;        // GPUParticleSystem の発射源 id（MeshVFXSystem のキャッシュが所有）
    int    vertexCount = 0;
    Model* boundModel = nullptr; // 束ねたモデル。差し替わったら束ね直す
};
