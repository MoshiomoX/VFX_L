// ============================================================
// StressTestTools.h
// 戦闘シーンの負荷テスト道具（Stress Test 面板）
//   ・投射物をばら撒く：生成先は GPU（SwarmSystem::SpawnProjectile）。
//     弾1つにつき emitter が1つ積まれ、粒子側の経路に負荷がかかる
//   ・自動補充：投射物の数を一定に保ち続けてプールを枯らした状態を維持する
//     （deadCount ガードが効いているかを確認できる唯一の状態）
//   ・既定値セット：毎回 slider を並べ直すと再現条件がぶれるので表にする
//   ・Mesh 発射の動作確認（仮設）：玩家の Mesh を発射源に登録し、世界行列を毎フレーム渡す
//   ・粒子 Flush の CPU 時間と emitter 数の記録（場面が測って渡す）
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "ECS/Entity.h"
#include "Particle/GPUParticleEmitter.h"
#include <memory>
#include <cstddef>

class SwarmSystem;
class GPUParticleSystem;
class CollisionSystem;
class ProjectileBillboardRenderer;
class Model;

class StressTestTools
{
public:
    // 補充と小分けの生成（UpdateGameplay の頭で）
    void Update(float dt, Registry& reg, Entity player, SwarmSystem& swarm);

    // Mesh 発射の動作確認（粒子の Flush の前に積む）
    void UpdateMeshEmitTest(float dt, Registry& reg, Entity player, GPUParticleSystem& particles);

    // 場面が測った値を記録する（Flush でクリアされる前の emitter 数・Flush の CPU 時間）
    void RecordEmitterStats(size_t pending, size_t dropped) { m_LastEmitterCount = pending; m_LastDropped = dropped; }
    void RecordFlushMs(double ms);
    double GetFlushMsAvg() const { return m_FlushMsAvg; }

    void DrawImGui(SwarmSystem& swarm, const CollisionSystem& collision,
        const GPUParticleSystem& particles, const ProjectileBillboardRenderer& billboards);

    // 投射物の自動補充（自己テストから。面板の Auto Refill と同じ）
    void SetAutoRefill(bool on, int target, int batch)
    {
        m_AutoRefill = on;
        m_RefillTarget = target;
        m_RefillBatch = batch;
    }

private:
    struct StressPreset
    {
        const char* name;
        const char* purpose;
        int   target;
        int   batch;
        bool  autoRefill;
    };
    static constexpr StressPreset kPresets[] = {
        { "P1 Starve 4000", "pool starvation: target 4000, batch 100", 4000, 100, true },
        { "P2 Starve 1024", "target 1024 (= emitter cap), batch 100",  1024, 100, true },
        { "P3 Refill 4000", "target 4000, batch 100",                  4000, 100, true },
        { "P4 Light 500",   "baseline: target 500, batch 50",          500,  50,  true },
        { "C1 Steady 500",  "target 500, batch 50",                    500,  50,  true },
        { "C2 Scale 2000",  "target 2000, batch 100",                  2000, 100, true },
        { "D1 Scale 1000",  "target 1000, batch 50",                   1000, 50,  true },
    };

    void SpawnProjectiles(Registry& reg, Entity player, SwarmSystem& swarm, int count);
    void ApplyPreset(const StressPreset& p, SwarmSystem& swarm);

    // ---- 生成 ----
    int  m_StressCount = 500;
    int  m_StressPending = 0;
    // ---- 自動補充 ----
    bool  m_AutoRefill = false;
    int   m_RefillTarget = 1000;
    int   m_RefillBatch = 100;
    float m_RefillTimer = 0.0f;
    float m_RefillInterval = 0.1f;
    int   m_LastPresetIndex = -1;

    // ---- Flush の CPU 時間（GPU を待っていないかの指標）----
    double m_FlushMs = 0.0;
    double m_FlushMsAvg = 0.0;
    double m_FlushMsPeak = 0.0;
    // ---- Emitter 統計 ----
    size_t m_LastEmitterCount = 0;
    size_t m_LastDropped = 0;

    // ---- Mesh 発射の動作確認（仮設。Editor / 実体への接続ができたら外す）----
    bool                   m_MeshEmitTest = false;
    float                  m_MeshEmitRate = 400.0f;
    int                    m_MeshEmitSourceId = -1;
    std::shared_ptr<Model> m_MeshEmitModel;   // 登録した源（RebuildVisual での差し替え検知）
    GPUParticleEmitter     m_MeshEmitter{ 7777 };
};
