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
// 対象外:
//   玩家と精英（EliteTag）は CPU の Registry に残る。
//   異構で少数、手感と調試が要る物は GPU に載せない。
//   接点は「玩家の位置を毎フレーム上げる」だけ。
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include "Swarm/GPUReadback.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

class ComputeShader;
class VertexShader;
class PixelShader;
class CameraBase;
class GridWorld;
class Model;

class SwarmSystem
{
public:
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形は変わらないので起動時に1回だけ上げる。
    // Regenerate した時はもう一度呼ぶこと
    void UploadTerrain(const GridWorld& grid);

    // ---- CPU 側から生成を依頼する（Flush でまとめて反映）----
    void SpawnEnemy(const DirectX::SimpleMath::Vector3& pos, float hp, float moveSpeed);
    void SpawnProjectile(const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& vel,
        float damage, float radius, float lifetime);

    // ---- 毎フレーム ----
    // UpdateGameplay の末尾で呼ぶ（粒子の Flush と同じ位置）。
    // 中で固定ステップを回すので、渡すのは実 dt でよい
    void Flush(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive, float dt);

    // Render で呼ぶ。UAV を外して SRV として繋ぎ直す
    void Render(CameraBase* camera);

    // ---- 回読結果（1〜2 フレーム古い。用途上それで困らない）----
    const SwarmCounters& GetCounters() const { return m_Readback.Latest(); }

    // 玩家が受けた累計ダメージを取り出して 0 に戻す。
    // 固定小数（実値 × 100）で来るので割ってから返す
    float ConsumePlayerDamage();

    // ---- ImGui 表示用 ----
    int GetPendingEnemySpawns()  const { return (int)m_PendingEnemies.size(); }
    int GetPendingProjSpawns()   const { return (int)m_PendingProjectiles.size(); }
    int GetLastSubSteps()        const { return m_LastSubSteps; }
    double GetFlushMs()          const { return m_FlushMs; }

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
    void RequestReadback();       // counter の copy を発行

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    // ============================================================
    // 本体バッファ（UAV で CS が書き、SRV で VS が読む）
    // ※同じ資源を UAV と SRV に同時に繋げない。
    //   Flush の最後で必ず UAV を外すこと（粒子で踏んだのと同じ罠）
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

    // --- counter（CS が InterlockedAdd で書き、CPU が回読）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CounterBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CounterUAV;

    // --- 地形（起動時に1回。読み取り専用）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TerrainBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TerrainSRV;
    // ============================================================
    // 生死フラグ（本体とは別バッファ）
    // SM5.0 の原子操作は RWBuffer<uint> にしか使えないため、
    // スロットの取り合いに要る state だけを切り出している
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

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnCB;
    // ============================================================
    // 生成キュー
    // CPU が空きスロットを管理する。
    // ※consume buffer を使わない理由:
    //   CopyStructureCount は命令キューをフラッシュするため
    //   約 0.076ms の固定コストが乗る（粒子で計測済み）。
    //   粒子では 0.6% で無視できたが、gameplay を 1ms 以下に
    //   したい今回は無視できない。
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnEnemyBuffer;   // 生成依頼の一時置き場
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnEnemySRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnProjBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnProjSRV;

    std::vector<Swarm::Enemy>      m_PendingEnemies;
    std::vector<Swarm::Projectile> m_PendingProjectiles;

    // --- 定数バッファ ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FrameCB;
    Swarm::FrameCB m_CachedFrameCB;

    // ============================================================
    // CS 群
    // 順序は DispatchStep が決める。宣言順に意味は無い
    // ============================================================
    std::shared_ptr<ComputeShader> m_ClearCountersCS;  // 子ステップ頭で counter を 0 に
    std::shared_ptr<ComputeShader> m_SpawnProjCS;      // 生成依頼を投射物の空きスロットへ
    std::shared_ptr<ComputeShader> m_ProjMoveCS;       // 投射物の積分
    std::shared_ptr<ComputeShader> m_SpawnEnemyCS;     // Phase 3
    std::shared_ptr<ComputeShader> m_EnemyAICS;        // Phase 3: seek + separation + 回避
    std::shared_ptr<ComputeShader> m_HitCS;            // Phase 4: 弾 vs 敵
    std::shared_ptr<ComputeShader> m_ContactCS;        // Phase 4: 敵 vs 玩家
    std::shared_ptr<ComputeShader> m_OrbCS;            // Phase 4: 経験値オーブ
     
    // --- 描画（Phase 2/3 で埋める）---
    std::shared_ptr<VertexShader> m_EnemyVS;
    std::shared_ptr<PixelShader>  m_EnemyPS;
    std::shared_ptr<Model>        m_EnemyModel;

    // --- 固定ステップ ---
    float m_Accumulator = 0.0f;
    int   m_LastSubSteps = 0;
    uint32_t m_FrameSeed = 0;

    // --- 回読 ---
    GPUReadback m_Readback;
    float m_PendingPlayerDamage = 0.0f;   // 回読した分の未消費ぶん

    // --- 計測 ---
    double m_FlushMs = 0.0;

    uint32_t m_TotalRequested = 0;    // SpawnProjectile が呼ばれた総数
    uint32_t m_TotalDispatched = 0;   // SpawnProjCS を dispatch した総数
    uint32_t m_TotalSteps = 0;        // DispatchStep が走った総数
    
    uint32_t m_LastKillCount = 0;
    uint32_t m_LastDamageTotal = 0;
};