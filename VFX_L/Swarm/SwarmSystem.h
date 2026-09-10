// ============================================================
// SwarmSystem.h
// 雑魚・投射物・経験値オーブを GPU 上で回すシステム。
//
// GPUParticleSystem と同じ形をしている:
//   CPU は「生成したい物」を溜め、Flush で一度だけ GPU へ流す。
//   位置や HP の真実は GPU 上にしか無く、CPU は読み戻さない。
//   例外は counter だけ（16 バイト、ring buffer で非同期に回読）。
//
// 粒子との違いは2つ:
//   1. 固定ステップを内部で回す（gameplay の結果が dt に依存しないように）
//   2. 少量の回読通道を持つ（湧き制御と玩家の被弾に要る）
//
// 弾は「核」でしかない（位置 + 判定）。見た目は全部粒子。
//   SwarmEmitCS が弾の位置から粒子を発射し、粒子システムが描く。
//   弾自身の描画経路は持たない。
//
// 対象外:
//   玩家と精英（EliteTag）は CPU の Registry に残る。
//   異構で少数、手感と調試が要る物は GPU に載せない。
//   接点は「玩家の位置を毎フレーム上げる」だけ。
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include "Swarm/GPUReadback.h"
#include "Swarm/SwarmVFXTable.h"
#include "VFX_Editor/VFXId.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

class ComputeShader;
class GPUParticleSystem;
class GridWorld;
class VertexShader;
class PixelShader;
class CameraBase;
class SwarmSystem
{
public:
    // 粒子システムを借りる（発射先）。Initialize の前に呼ぶ
    void SetParticleSystem(GPUParticleSystem* ps) { m_Particles = ps; }

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形は変わらないので起動時に1回だけ上げる。
    // Regenerate した時はもう一度呼ぶこと
    void UploadTerrain(const GridWorld& grid);

    // VFXDatabase から配方表を作る。ItemDatabase / VFXDatabase の後に1回
    bool BuildVFXTable();

    void RenderDebug(CameraBase* camera);
    // ---- CPU 側から生成を依頼する（Flush でまとめて反映）----
    void SpawnEnemy(const DirectX::SimpleMath::Vector3& pos, float hp, float moveSpeed);
    void SpawnProjectile(VFXId vfx,
        const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& vel,
        float damage, float radius, float lifetime);

    // ---- 毎フレーム ----
    // UpdateGameplay の末尾、粒子の Flush より前に呼ぶ。
    // 中で固定ステップを回すので、渡すのは実 dt でよい。
    // totalTime は発射の乱数 seed 用
    void Flush(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive, float dt, float totalTime);

    // ---- 回読結果（1〜2 フレーム古い。用途上それで困らない）----
    const SwarmCounters& GetCounters() const { return m_Readback.Latest(); }

    // 玩家が受けた累計ダメージを取り出して 0 に戻す
    float ConsumePlayerDamage();

    // ---- ImGui 表示用 ----
    const SwarmVFXTable& GetVFXTable() const { return m_VFX; }
    int GetPendingEnemySpawns() const { return (int)m_PendingEnemies.size(); }
    int GetPendingProjSpawns()  const { return (int)m_PendingProjectiles.size(); }
    int GetLastSubSteps()       const { return m_LastSubSteps; }
    double GetFlushMs()         const { return m_FlushMs; }
    uint32_t GetTotalRequested()  const { return m_TotalRequested; }
    uint32_t GetTotalDispatched() const { return m_TotalDispatched; }
    uint32_t GetTotalSteps()      const { return m_TotalSteps; }

private:
    // --- 生成 ---
    bool CreateBuffers(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);

    // --- Flush の内訳 ---
    void UploadFrameCB(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive);
    void UploadSpawns();          // 溜めた生成依頼を GPU へ
    void DispatchStep();          // 固定ステップ 1 回分の CS 群
    void DispatchEmit(float dt, float totalTime);   // 弾から粒子を発射
    void RequestReadback();       // counter の copy を発行

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    GPUParticleSystem* m_Particles = nullptr;

    // ============================================================
    // 本体バッファ（UAV で CS が書き、SRV で他の CS が読む）
    // ※同じ資源を UAV と SRV に同時に繋げない。
    //   各 dispatch の後で必ず UAV を外すこと
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemySRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjSRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_OrbBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_OrbUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_OrbSRV;

    // ============================================================
    // 生死フラグ（本体とは別バッファ）
    // SM5.0 の原子操作は RWBuffer<uint> にしか使えないため、
    // スロットの取り合いに要る state だけを切り出している。
    // 本体と同じ index が同じ個体を指す
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyStateSRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjStateSRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_OrbStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_OrbStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_OrbStateSRV;

    // --- counter（CS が InterlockedAdd で書き、CPU が回読）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CounterBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CounterUAV;

    // --- 発射予約（RAW UAV。毎フレーム 0 に戻す）---
    // 1スレッドが k 個発射する時、線程番号では deadCount と比べられないので
    // 原子的に予約して超過分を諦める。粒子 EmitCS の護欄と同じ思想、別の形
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EmitBudget;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EmitBudgetUAV;

    // --- 地形（起動時に1回。読み取り専用）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TerrainBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TerrainSRV;

    // --- VFX 配方表（起動時に1回。読み取り専用）---
    SwarmVFXTable m_VFX;

    std::shared_ptr<VertexShader> m_DebugVS;
    std::shared_ptr<PixelShader>  m_DebugPS;

    struct DebugCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
    };
    // ============================================================
    // 生成キュー
    // CPU が空きスロットを管理する。
    // ※consume buffer を使わない理由:
    //   CopyStructureCount は命令キューをフラッシュするため
    //   約 0.076ms の固定コストが乗る（粒子で計測済み）
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnEnemyBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnEnemySRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnProjBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnProjSRV;

    std::vector<Swarm::Enemy>      m_PendingEnemies;
    std::vector<Swarm::Projectile> m_PendingProjectiles;

    // --- 定数バッファ ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FrameCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnCB;
    Swarm::FrameCB m_CachedFrameCB;

    // ============================================================
    // CS 群
    // 順序は DispatchStep / DispatchEmit が決める。宣言順に意味は無い
    // ============================================================
    std::shared_ptr<ComputeShader> m_ClearCountersCS;  // 子ステップ頭で counter を 0 に
    std::shared_ptr<ComputeShader> m_SpawnProjCS;      // 生成依頼を投射物の空きスロットへ
    std::shared_ptr<ComputeShader> m_ProjMoveCS;       // 投射物の積分
    std::shared_ptr<ComputeShader> m_EmitCS;           // 弾から粒子を発射
    std::shared_ptr<ComputeShader> m_SpawnEnemyCS;     // Phase 3
    std::shared_ptr<ComputeShader> m_EnemyAICS;        // Phase 3: seek + separation + 回避
    std::shared_ptr<ComputeShader> m_HitCS;            // Phase 4: 弾 vs 敵
    std::shared_ptr<ComputeShader> m_ContactCS;        // Phase 4: 敵 vs 玩家
    std::shared_ptr<ComputeShader> m_OrbCS;            // Phase 4: 経験値オーブ

    // --- 固定ステップ ---
    float m_Accumulator = 0.0f;
    int   m_LastSubSteps = 0;
    uint32_t m_FrameSeed = 0;

    // --- 回読 ---
    GPUReadback m_Readback;
    float    m_PendingPlayerDamage = 0.0f;   // 回読した分の未消費ぶん
    uint32_t m_LastKillCount = 0;            // 累計 counter の前回値（差分用）
    uint32_t m_LastDamageTotal = 0;

    // --- 計測 ---
    double   m_FlushMs = 0.0;
    uint32_t m_TotalRequested = 0;
    uint32_t m_TotalDispatched = 0;
    uint32_t m_TotalSteps = 0;
};