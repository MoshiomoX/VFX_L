// ============================================================
// MeshVFXSystem.h
// 実体のモデル表面から粒子を出す VFX を付ける / 追従させる System。
// ProjectileVFXSystem の Mesh 発射版。
//
//   Attach     : テンプレートを複製し、Shape=Mesh で source 未指定の
//                Particle entry を宿主の Mesh（頂点 + index の raw view）へ束ねる
//   StartBurn  : DissolveComponent を付けて Attach（燃焼消滅の開始）
//   Update     : 世界行列の追従、溶解の進行、縁の粒子の依頼、
//                消滅し終えた実体の破棄
//
// ※ GPUParticleSystem::Flush より前に Update すること。
// ※ 発射源の登録は Model ごとに 1 回（同じ胶囊を使う精英は 1 枠を共有）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "VFX_Editor/VFXId.h"
#include <memory>
#include <vector>
#include <string>

class Registry;
class VFXEffect;
class Model;
class Texture;
class GPUParticleSystem;
struct VFXContext;
struct MeshVFXComponent;

class MeshVFXSystem
{
public:
    // 起動時：VFXId ごとのテンプレートを登録
    void RegisterVFX(VFXId id, const std::string& jsonPath);

    // 実体に VFX 実例を付ける。ModelComponent が無い / テンプレートが無いなら false
    bool Attach(Registry& reg, Entity e, VFXId id, const VFXContext& ctx);
    void Detach(Registry& reg, Entity e);

    // 燃焼消滅の開始：DissolveComponent（既定 noise）+ Attach
    bool StartBurn(Registry& reg, Entity e, VFXId id, const VFXContext& ctx, float duration);

    // 毎フレーム
    void Update(Registry& reg, float dt, const VFXContext& ctx);

    size_t GetActiveCount() const { return m_ActiveCount; }

private:
    std::shared_ptr<VFXEffect> GetTemplate(VFXId id) const;

    // Model の第 0 SubMesh を発射源として登録（キャッシュ。id を返す、失敗は -1）
    int  AcquireSource(const std::shared_ptr<Model>& model, GPUParticleSystem* ps, int& vertexCount);
    // 効果の中の Mesh 発射 entry を宿主の源へ束ねる
    void BindEntries(MeshVFXComponent& comp);

    struct SourceCache
    {
        std::shared_ptr<Model> model;   // 生かしておく（id が指す buffer が消えないように）
        int sourceId = -1;
        int vertexCount = 0;
    };
    std::vector<SourceCache> m_Sources;

    std::vector<std::pair<VFXId, std::shared_ptr<VFXEffect>>> m_Templates;
    std::shared_ptr<Texture> m_DefaultNoise;   // StartBurn の既定（Perlin 生成）
    size_t m_ActiveCount = 0;
};
