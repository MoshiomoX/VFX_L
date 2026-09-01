// ============================================================
// ProjectileVFXSystem.h
// 投射物の VFX を追従させ、emitter を積む System。
// ※ GPUParticleSystem::Flush より前に実行する必要がある。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "SpellID.h"
#include <memory>
#include <vector>
#include <string>

class Registry;
class VFXEffect;
struct VFXContext;

class ProjectileVFXSystem
{
public:
    // 起動時に呼ぶ：ItemID ごとの VFX テンプレートを登録
    void RegisterVFX(ItemID id, const std::string& jsonPath);

    // 投射物生成直後に呼ぶ：その Entity に VFX 実例を付ける
    // テンプレートが無い ItemID なら何もしない（降級：特効なしで飛ぶ）
    void AttachVFX(Registry& reg, Entity e, ItemID id, const VFXContext& ctx);

    // 毎フレーム：位置を追従させて Update（emitter が積まれる）
    void Update(Registry& reg, float dt, const VFXContext& ctx);
    size_t GetSkippedCount() const { return m_SkippedCount; }

    size_t GetActiveVFXCount() const { return m_ActiveCount; }

private:
    std::shared_ptr<VFXEffect> GetTemplate(ItemID id) const;

    std::vector<std::pair<ItemID, std::shared_ptr<VFXEffect>>> m_Templates;
    size_t m_ActiveCount = 0;
    size_t m_SkippedCount = 0;
};