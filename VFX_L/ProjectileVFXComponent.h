// ============================================================
// ProjectileVFXComponent.h
// 投射物に追従する VFX 実例。
// テンプレートから CloneFrom で複製したものを1発ごとに持つ。
// ※ VFXEffect は再生状態を持つため共有できない（実例が必要）。
// ============================================================
#pragma once
#include "VFXEffect.h"
#include <memory>

struct ProjectileVFXComponent
{
    std::shared_ptr<VFXEffect> effect;

    // json の position を相対オフセットとして扱うため、
    // 投射物中心からのさらなる微調整が必要ならここで足す
    DirectX::SimpleMath::Vector3 offset = { 0.0f, 0.0f, 0.0f };
};