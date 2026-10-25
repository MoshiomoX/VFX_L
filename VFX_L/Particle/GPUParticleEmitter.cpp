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

    // 負の発射率（Inspector の Ctrl+クリック・壊れた json）は 0 扱い（2026-10-25）。負の発射数が GPU へ行くと、
    // 発射 CS が uint に読んで約 40 億になり、最初の発射器が空きを全部奪っていた。
    // 溜まりすぎ（巨大な発射率・NaN）も int へ直す前に抑える
    constexpr float kMaxPerFrame = 1.0e6f;
    const float add = emitRate * deltaTime;
    if (add > 0.0f) m_EmitAccumulator += add;
    if (!(m_EmitAccumulator >= 0.0f)) m_EmitAccumulator = 0.0f;   // NaN もここで 0
    if (m_EmitAccumulator > kMaxPerFrame) m_EmitAccumulator = kMaxPerFrame;
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
    e.emitCount = (m_PendingEmitCount > 0) ? m_PendingEmitCount : 0;
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
    e.colorKeyCount = std::clamp(colorKeyCount, 0, (int)MAX_COLOR_KEYS);   // colorKeys[] の大きさを越えない

    // Mesh 発射用。Mesh 以外の形状では読まれない
    e.world = world;
    e.sourceId = -1;
    e.sourceCount = 0;
    e.edgeMode = 0;
    e.renderMode = (renderMode == 0)
        ? (alphaBlend ? ParticleRenderMode::kBillboardAlpha : 0)
        : ParticleRenderMode::Pack(meshSlot, meshGlow, meshFaceVelocity, meshForwardAxis);
    if (inheritVelocity) e.renderMode |= ParticleRenderMode::kInheritSourceVelocity;
    if (singleBody) e.renderMode |= ParticleRenderMode::kSingleBody;   // 弾の本体：常にちょうど 1 つ（2026-10-16）
    if (renderMode != 0 && meshGlow && meshBehind)   // 発光メッシュの裏面だけ = 本体の後ろの縁取り（2026-10-16）
        e.renderMode |= ParticleRenderMode::kMeshBehind;
    // 実際に詰めるトゥーン（流線と排他。流線が付いていればトゥーンの欄が true でも詰めない）
    const bool packToon = (renderMode == 0 && !streak && toon);
    if (renderMode == 0 && streak)   // 流線（ビルボードだけ。トゥーンとは排他、2026-10-20）
        e.renderMode |= ParticleRenderMode::PackStreak(streakStretch);
    else if (packToon)   // トゥーン（ビルボードだけ）
        e.renderMode |= ParticleRenderMode::PackToon(toonOutline, toonCut, toonBands, toonShade);
    // 画面の歪み（ビルボードだけ。bit 15 はトゥーンの線と同じ位置なのでトゥーンを詰めない時だけ）。
    // 判定は「実際に詰めたトゥーン」で見る（2026-10-25。流線 + トゥーンの欄が残っていると歪みが黙って消えていた）
    if (renderMode == 0 && !packToon && distortLevel > 0)
        e.renderMode |= ParticleRenderMode::PackDistort(distortLevel, distortOnly);
    if (windResponse > 0.0f)       // 世界の風を受ける（ParticleUpdateCS）
        e.renderMode |= ParticleRenderMode::PackWind(windResponse);


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