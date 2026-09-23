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
#include "Graphics/Light/LightTypes.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <vector>

class Model;
class Material;
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
    void Render(CameraBase* camera, const LightBuffer& light);
    void Shutdown();

    // 地形は変わらないので起動時に1回だけ上げる。
    // Regenerate した時はもう一度呼ぶこと
    void UploadTerrain(const GridWorld& grid);

    // VFXDatabase から配方表を作る。ItemDatabase / VFXDatabase の後に1回
    bool BuildVFXTable();

    void RenderDebug(CameraBase* camera);
    // ---- CPU 側から生成を依頼する（Flush でまとめて反映）----
    void SpawnEnemy(const DirectX::SimpleMath::Vector3& pos, float hp, float moveSpeed);
    // motion  : SetMotions で上げた表の番号（0 = 直進）
    // mirror  : 曲線を左右反転して撃つ（交互撃ち・乱数撃ちは呼ぶ側が決める）
    // 曲線の型では vel の「速さ」だけが使われ、向きは曲線が決める。
    // 捕捉する敵は玩家に一番近い 1 体（武器が狙っているのと同じ相手）
    void SpawnProjectile(VFXId vfx,
        const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& vel,
        float damage, float radius, float lifetime,
        uint32_t motion = 0, bool mirror = false);

    // 運動表を丸ごと差し替える。添字がそのまま SpawnProjectile の motion。
    // 飛んでいる弾も次のステップから新しい値で動く（編集器で調整中の反映用）
    void SetMotions(const std::vector<Swarm::Motion>& motions);

    // ---- 範囲攻撃（爆発・法環）----
    // CPU から 1 個出す（Flush でまとめて反映）。
    // tickTimer = 0 なら出た最初のステップで 1 回目のダメージが入る。
    // 単発は tickInterval を duration より長くしておけば 1 回しか tick しない。
    // vfxType は 0 のままにする：CPU から出した範囲の見た目は CPU 側で VFX を再生する
    void SpawnArea(const Swarm::Area& area);

    // 雛形の表を丸ごと差し替える。添字が Motion::hitArea。0 番は「無し」なので中身は使われない。
    // 弾が命中した場所に GPU が自分で範囲を出す時に引く
    void SetAreaDefs(const std::vector<Swarm::AreaDef>& defs);

    // 範囲を全部消す
    void ClearAreas();

    // ---- 毎フレーム ----
    // UpdateGameplay の末尾、粒子の Flush より前に呼ぶ。
    // 中で固定ステップを回すので、渡すのは実 dt でよい。
    // totalTime は発射の乱数 seed 用
    void Flush(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive, float dt, float totalTime);

    // 雑魚の見た目（焼いた静的メッシュ。デバッグ表示用）
    std::shared_ptr<Model> GetEnemyModel() const { return m_EnemyModel; }

    // 弾・範囲の点光源を PointLightManager のリストへ追記する。
    // CPU 側の光（VFX の Light entry）を積み終えた後、描画の前に呼ぶ
    void CollectLights();

    // ---- 回読結果（1〜2 フレーム古い。用途上それで困らない）----
    const SwarmCounters& GetCounters() const { return m_Readback.Latest(); }
    Swarm::AICB& GetAIParams() { return m_CachedAICB; }
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
    // 溢れ分の湧き。GPU が遠い雑魚を1体選んでこの内容へ上書きする（枠を消費しない）


    void RecycleEnemy(const Vector3& pos, float hp, float moveSpeed);
    void SetRecycleMinDist(float d) { m_RecycleMinDist = d; }
    float ConsumeExp();

    // GPU 上の雑魚・弾・オーブを全部消す（地形の作り直し用）。
    // state を DEAD にするだけ。counter は触らない（累加値の差分が狂う）
    void KillAll();

    // 弾だけ消す（負荷テストのリセット用）
    void ClearProjectiles();

    // 玩家に一番近い雑魚（回読なので 1〜2 フレーム古い）。無ければ false
    bool GetNearestEnemy(Vector3& pos, Vector3& vel, float& dist) const
    {
        const auto& c = GetCounters();
        if (c.nearestDist >= 1e29f) return false;
        pos = { c.nearestPos[0], c.nearestPos[1], c.nearestPos[2] };
        vel = { c.nearestVel[0], c.nearestVel[1], c.nearestVel[2] };
        dist = c.nearestDist;
        return true;
    }
private:
    // --- 生成 ---
    bool CreateBuffers(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);
    // 雑魚の見た目: 骨付き FBX の 1 フレームを焼いた静的メッシュ（駄目ならカプセル）
    std::shared_ptr<Model> BuildEnemyModel(ID3D11Device* device);

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

    // --- 投射物の運動 ---
    // path   : 投射物と同じ添字。GPU が組んだベジェ（CPU は触らない）
    // motion : 運動表。CPU から上げる（読み取り専用）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_PathBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_PathUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_PathSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_MotionBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_MotionSRV;

    // --- 範囲攻撃 ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaStateSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaDefBuffer;      // 雛形の表（CPU から書く）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaDefSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnAreaBuffer;    // 生成依頼
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_SpawnAreaSRV;
    std::vector<Swarm::Area> m_PendingAreas;

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
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HeightBuffer;      // 高さ場（GridWorld::Heights）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;

    // --- VFX 配方表（起動時に1回。読み取り専用）---
    SwarmVFXTable m_VFX;

    std::shared_ptr<VertexShader> m_DebugVS;
    std::shared_ptr<PixelShader>  m_DebugPS;
    std::shared_ptr<VertexShader> m_DebugEnemyVS;
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
    Swarm::AICB m_CachedAICB;
    Swarm::OrbCB m_CachedOrbCB;

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
    std::shared_ptr<ComputeShader> m_OrbCS;            // Phase 4: 経験値オーブの吸引・取得（DispatchStep の第 7 段）
    std::shared_ptr<ComputeShader> m_EnemyMoveCS;
    // ---- 雑魚描画 ----
    std::shared_ptr<VertexShader> m_EnemyVS;      // SwarmEnemyVS（buffer から位置と向きを読む）
    std::shared_ptr<PixelShader>  m_EnemyPS;      // Shader/PS.hlsl をそのまま使う
    std::shared_ptr<Material>     m_EnemyMaterial;// VS/PS + 既定テクスチャの束ね役
    std::shared_ptr<Model>        m_EnemyModel;   // 雑魚共通のカプセル

    // --- 経験値オーブの本描画 ---
    std::shared_ptr<VertexShader> m_OrbVS;
    std::shared_ptr<Material>     m_OrbMaterial;   // PS は雑魚と共用
    std::shared_ptr<Model>        m_OrbModel;

    std::shared_ptr<ComputeShader> m_RecycleCS;
    std::vector<Swarm::Enemy> m_PendingRecycles;
    std::vector<Swarm::Enemy> m_EnemyUpload;     // 新規 + 転送を連結した一時領域
    float m_RecycleMinDist = 35.0f;

    // ---- 範囲攻撃 ----
    std::shared_ptr<ComputeShader> m_SpawnAreaCS;    // CPU の依頼を空きスロットへ
    std::shared_ptr<ComputeShader> m_AreaTickCS;     // 時計を進める・玩家に追従・tick の判定（命中の直後）
    std::shared_ptr<ComputeShader> m_AreaDamageCS;   // tick した範囲の中の雑魚へダメージ
    std::shared_ptr<ComputeShader> m_AreaEmitCS;     // GPU が出した範囲（弾の命中）から粒子を発射
    std::shared_ptr<ComputeShader> m_LightCollectCS;     // 弾の点光源を PointLightManager へ追記
    std::shared_ptr<ComputeShader> m_AreaLightCollectCS; // 範囲の分
    std::shared_ptr<ComputeShader> m_EnemyCompactCS;     // 活きスロットの一覧（描画の instance 数）

    // --- 雑魚描画の間接引数 ---
    // 4096 槽を毎フレーム全部 DrawInstanced すると頂点数がモデル × 4096 になる
    // （Minion 8.6k 頂点で 3500 万）。活きスロットだけ描くために
    // CompactCS → aliveList、CopyStructureCount → args[submesh].InstanceCount
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AliveListBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AliveListUAV;   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AliveListSRV;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_EnemyDrawArgs;  // submesh 毎（IndexCount が違う）
    bool CreateEnemyDrawArgs(ID3D11Device* device);

    std::shared_ptr<ComputeShader> m_AimResolveCS;
   // Phase 4: 最寄りの雑魚を回読用に書き出す
    // 転送の「誰が何番目を取ったか」用。dispatch 前に 0 にする
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_RecycleClaim;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_RecycleClaimUAV;
    struct EnemyRenderCB
    {
        DirectX::SimpleMath::Matrix world = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
    };
    // --- 固定ステップ ---
    float m_Accumulator = 0.0f;
    int   m_LastSubSteps = 0;
    uint32_t m_FrameSeed = 0;

    // --- 回読 ---
    GPUReadback m_Readback;
    float    m_PendingPlayerDamage = 0.0f;   // 回読した分の未消費ぶん
    uint32_t m_LastKillCount = 0;            // 累計 counter の前回値（差分用）
    uint32_t m_LastDamageTotal = 0;


    uint32_t m_LastExpTotal = 0;
    float    m_PendingExp = 0.0f;


    // --- 計測 ---
    double   m_FlushMs = 0.0;
    uint32_t m_TotalRequested = 0;
    uint32_t m_TotalDispatched = 0;
    uint32_t m_TotalSteps = 0;
};