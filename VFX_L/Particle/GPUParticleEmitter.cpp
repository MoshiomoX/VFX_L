#include "Particle/GPUParticleEmitter.h"
#include "Particle/ParticleSheets.h"

GPUParticleEmitter::GPUParticleEmitter(int id)
    : m_ID(id)
{
}

void GPUParticleEmitter::Update(float deltaTime)
{
    if (!m_IsActive)
    {
        m_PendingEmitCount = 0;
        return;
    }

    m_EmitAccumulator += emitRate * deltaTime;
    m_PendingEmitCount = static_cast<int>(m_EmitAccumulator);
    m_EmitAccumulator -= static_cast<float>(m_PendingEmitCount);
}

GPUEmitter GPUParticleEmitter::ToGPU() const
{
    GPUEmitter e = {};

    e.position = position;
    e.emitType = static_cast<int>(emitType);
    e.direction = direction;
    e._pad0 = 0.0f;
    e.emitCount = m_PendingEmitCount;
    e.maxParticles = maxParticles;
    e.particleOffset = particleOffset;
    e.emitRate = emitRate;
    e.speedRange = speedRange;
    e.lifetimeRange = lifetimeRange;
    e.sizeRange = sizeRange;
    e.startColorMin = startColorMin;
    e.startColorMax = startColorMax;
    e.endColorMin = endColorMin;
    e.endColorMax = endColorMax;
    e.gravity = gravity;
    e.dragCoeff = dragCoeff;
    e.rotationRange = rotationRange;
    e.angularVelRange = angularVelRange;
    e.isActive = m_IsActive ? 1.0f : 0.0f;
    e.emitterID = m_ID;
    e.atlasRows = atlasRows;
    e.atlasCols = atlasCols;
    e.textureIndex = textureIndex;
    e.frameMode = frameMode;
    e.frameCount = frameCount;
    if (frameMode == (int)ParticleFrameMode::Legacy)
        e.atlasIndex = atlasAnimate ? -1 : atlasIndex;
    else
        e.atlasIndex = atlasIndex;
    // 1 番以降のテクスチャは格子の形が決まっている（説明 json）ので、手で入れた値は使わない
    if (textureIndex > 0)
        if (const auto* sheet = ParticleSheets::Get(textureIndex))
        {
            e.atlasRows = sheet->rows;
            e.atlasCols = sheet->cols;
        }
	e.colorKeyOffset = m_ColorKeyOffset;
    e.colorKeyCount = colorKeyCount;    

    // Mesh 発射用。Mesh 以外の形状では読まれない
    e.world = world;
    e.sourceId = -1;
    e.sourceCount = 0;
    e.edgeMode = 0;
    e.renderMode = (renderMode == 0)
        ? (alphaBlend ? ParticleRenderMode::kBillboardAlpha : 0)
        : ParticleRenderMode::Pack(meshSlot, meshGlow, meshFaceVelocity, meshForwardAxis);
    if (inheritVelocity) e.renderMode |= ParticleRenderMode::kInheritSourceVelocity;
    if (renderMode == 0 && toon)   // トゥーン（ビルボードだけ）
        e.renderMode |= ParticleRenderMode::PackToon(toonOutline, toonCut, toonBands, toonShade);


    // 形状パラメータをemitTypeに応じてパッキング
    switch (emitType)
    {
    case EmitType::Point:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { 0, 0, 0 };
        break;

    case EmitType::Sphere:
        e.spreadAngle = 0.0f;
        e.shapeSize = { shape.radius, 0, 0 };
        break;

    case EmitType::Cone:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { shape.radius, 0, 0 };
        break;

    case EmitType::Box:
        e.spreadAngle = 0.0f;
        e.shapeSize = shape.boxExtents;
        break;

    case EmitType::Ring:
        e.spreadAngle = 0.0f;
        e.shapeSize = { shape.radius, shape.innerRadius, 0 };
        break;

    case EmitType::Disc:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { shape.radius, 0, 0 };
        break;

    case EmitType::Mesh:
        e.spreadAngle = 0.0f;
        e.shapeSize = { 0, 0, 0 };
        e.sourceId = shape.sourceId;
        e.sourceCount = shape.sourceCount;
        e.edgeMode = shape.edgeMode;
        break;
    }

    return e;
}