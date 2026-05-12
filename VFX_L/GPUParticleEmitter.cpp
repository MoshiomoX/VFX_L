#include "GPUParticleEmitter.h"

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

    // 形状パラメータをemitTypeに応じてパッキング
    switch (emitType)
    {
    case EmitType::Point:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { 0, 0, 0 };
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Sphere:
        e.spreadAngle = 0.0f;
        e.shapeSize = { shape.radius, 0, 0 };
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Cone:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { shape.radius, 0, 0 };
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Box:
        e.spreadAngle = 0.0f;
        e.shapeSize = shape.boxExtents;
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Ring:
        e.spreadAngle = 0.0f;
        e.shapeSize = { shape.radius, shape.innerRadius, 0 };
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Disc:
        e.spreadAngle = shape.spreadAngle;
        e.shapeSize = { shape.radius, 0, 0 };
        e.meshVertexOffset = 0;
        e.meshVertexCount = 0;
        break;

    case EmitType::Mesh:
        e.spreadAngle = 0.0f;
        e.shapeSize = { 0, 0, 0 };
        e.meshVertexOffset = shape.meshVertexOffset;
        e.meshVertexCount = shape.meshVertexCount;
        break;
    }

    return e;
}