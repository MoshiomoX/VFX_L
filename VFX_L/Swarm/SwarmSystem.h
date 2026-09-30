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
#include "Swarm/FlowField.h"
#include "VFX_Editor/VFXId.h"
#include "Graphics/Light/LightTypes.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <vector>

class Model;
class Material;
class Texture;
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

    // GPU の範囲（弾の命中）が出した連番絵（Sprite entry）。不透明物の後・粒子の前に呼ぶ
    void RenderSprites(CameraBase* camera);

    // ---- 雑魚の頭上の HP バー（Render の最後、雑魚とオーブの後に描く）----
    // 大きさは世界の寸法（遠いほど小さい。モデルと同じ比率）。全員に常に出す
    struct HpBarStyle
    {
        bool  enabled = true;
        float width = 0.9f;       // m
        float height = 0.1f;      // m
        float offset = 1.0f;      // 位置（カプセルの中心）から上へ m。雑魚は高さ 1.6m（頭頂 +0.7）
        float border = 0.015f;    // 縁の太さ m
        DirectX::SimpleMath::Vector4 fill = { 0.85f, 0.20f, 0.20f, 1.0f };   // HUD の HP と同じ赤
        DirectX::SimpleMath::Vector4 back = { 0.08f, 0.08f, 0.10f, 0.75f };  // 減った分
        DirectX::SimpleMath::Vector4 edge = { 0.0f, 0.0f, 0.0f, 0.9f };
    };
    HpBarStyle hpBar;

    // ---- 雑魚の足元の丸い影（Render の中、雑魚とオーブの後・警告の輪の前）----
    // 雑魚は太陽の影図（ShadowMap）に入れないので、代わりに地面を丸く暗くする。
    // 板の四隅はそれぞれ高さ場に載せる（高台の坂でも地面に沿う）
    struct BlobShadowStyle
    {
        bool  enabled = true;
        float radius = 0.65f;     // m（体の幅 0.5m より少し大きく。真下は体に隠れる）
        float strength = 0.6f;    // 中心の暗さ 0..1
        float softness = 0.6f;    // 半径のうち外側の何割でぼかすか
        float lift = 0.03f;       // 地面から浮かせる m（Z ファイト避け）
        float clamp = 0.3f;       // 四隅の高さを足元から何 m までに抑えるか（崖の縁で板が伸びない）
    };
    BlobShadowStyle blobShadow;

    // ---- 点火した自爆兵の足元の警告の輪（Render の中、雑魚とオーブの後・HP バーの前）----
    // 外周 = 爆発半径（BomberCB.blastRadius）、中の円盤が導火線に合わせて中心から育ち、外周に届くと爆発。
    // 色は straight alpha（シェーダーが premultiply する）
    struct BomberRingStyle
    {
        bool  enabled = true;
        float edgeWidth = 0.08f;  // 外周の太さ m
        float lift = 0.03f;       // 地面から浮かせる m（Z ファイト避け）
        DirectX::SimpleMath::Vector4 fill = { 0.90f, 0.12f, 0.08f, 0.35f };  // 育つ円盤
        DirectX::SimpleMath::Vector4 edge = { 0.95f, 0.15f, 0.10f, 0.85f };  // 外周
        DirectX::SimpleMath::Vector4 back = { 0.90f, 0.12f, 0.08f, 0.10f };  // 外周の内側でまだ育っていない所
    };
    BomberRingStyle bomberRing;

    // ---- 隕石（DROP の弾）が落ちる所の警告の輪（自爆兵の輪の後に描く）----
    // 形と動きは自爆兵の輪と同じ（PS も共用）。外周 = 弾の hitArea の半径、
    // 中の円盤が落下（pathT）に合わせて育ち、外周に届くと着弾。
    // 色だけ橙黄: 赤は「敵の危険」、こちらは自分の技
    BomberRingStyle dropRing = { true, 0.08f, 0.03f,
        { 1.00f, 0.60f, 0.10f, 0.35f },    // 育つ円盤
        { 1.00f, 0.78f, 0.25f, 0.85f },    // 外周
        { 1.00f, 0.60f, 0.10f, 0.10f } };  // 外周の内側でまだ育っていない所

    // ---- 経験値オーブの見た目（SwarmOrbVS / SwarmOrbPS / SwarmOrbEmitCS）----
    // 自発光の宝石（双角錐）。待機中は浮き沈み・自転・脈動（スロット毎に位相をずらす）。
    // 吸い寄せられると長軸を玩家へ傾けて引き伸ばし、pullColor へ寄って明るくなり、
    // ExpOrbTrail.json の粒子を尾に出す。色は linear HDR（Bloom の閾値は 1）
    struct OrbLookStyle
    {
        float scale = 1.0f;             // 網の倍率（網は赤道の半径 0.13m、高さ 0.4m）
        float bobHeight = 0.08f;        // 浮き沈みの振幅 m
        float bobSpeed = 2.5f;          // rad/s
        float spinSpeed = 2.0f;         // 長軸まわりの自転 rad/s
        float pulseAmount = 0.08f;      // 大きさの脈動 1 ± これ
        float pulseSpeed = 4.0f;        // rad/s
        float fullPullSpeed = 12.0f;    // 吸い寄せの速さがこれ（m/s）で見た目の変化が最大
        float stretchPerSpeed = 0.06f;  // 1 m/s 毎に伸びる割合（体積は保つ）
        float stretchMax = 0.8f;        // 伸びの上限（1 + これ 倍）
        float tiltMax = 1.3f;           // 長軸を玩家へ傾ける最大角 rad
        float pullGlow = 1.8f;          // 吸い寄せ中の明るさの倍率
        DirectX::SimpleMath::Vector4 idleColor = { 0.20f, 0.65f, 1.00f, 1.0f };
        DirectX::SimpleMath::Vector4 pullColor = { 0.55f, 0.95f, 1.00f, 1.0f };
        float emissive = 1.6f;          // 本体の明るさ（色 × これ）
        float facet = 0.55f;            // 面の陰影（0 = 一様に光る / 1 = 太陽の向きで面毎に明暗）
        float rimGain = 1.2f;           // 縁の光（fresnel）
        float rimPower = 2.5f;
        float glintGain = 1.5f;         // 太陽の鋭いきらめき
        float glintPower = 24.0f;
        DirectX::SimpleMath::Vector4 rimColor = { 0.60f, 0.90f, 1.00f, 1.0f };
        bool  trail = true;             // 吸い寄せ中の尾（粒子）
        float trailMinSpeed = 3.0f;     // m/s。これより速く吸い寄せられている物だけ尾を出す
    };
    OrbLookStyle orbLook;

    // 雑魚の歩きアニメの再生速度（部品アニメがある時だけ効く。移動速度と足の運びを合わせる調整用）
    float enemyWalkAnimRate = 1.0f;

    // ---- CPU 側から生成を依頼する（Flush でまとめて反映）----
    // kind: Swarm::kEnemyKindMob / kEnemyKindBomber
    void SpawnEnemy(const DirectX::SimpleMath::Vector3& pos, float hp, float moveSpeed,
        uint32_t kind = Swarm::kEnemyKindMob);
    // motion  : SetMotions で上げた表の番号（0 = 直進）
    // mirror  : 曲線を左右反転して撃つ（交互撃ち・乱数撃ちは呼ぶ側が決める）
    // 曲線の型では vel の「速さ」だけが使われ、向きは曲線が決める。
    // 捕捉する敵は玩家に一番近い 1 体（武器が狙っているのと同じ相手）
    // triggerTag : この弾が消えたら誘発できる高級魔法（杖の spells の添字の bit）。0 = 無し
    // spawnAtPos : Drop 型だけ。pos を着弾点にする（誘発の隕石。最寄りの敵を捕捉しない）
    void SpawnProjectile(VFXId vfx,
        const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& vel,
        float damage, float radius, float lifetime,
        uint32_t motion = 0, bool mirror = false,
        uint32_t triggerTag = 0, bool spawnAtPos = false);

    // 回読で届いた誘発（タグ付きの弾が消えた場所）を全部取り出す。2〜3 フレーム古い
    void ConsumeTriggerEvents(std::vector<Swarm::TriggerEvent>& out)
    {
        out.insert(out.end(), m_TriggerEvents.begin(), m_TriggerEvents.end());
        m_TriggerEvents.clear();
    }

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
    // Boss の数・HP・位置（描画の CompactCS が書くので 2〜3 フレーム古い）
    const Swarm::BossInfo& GetBossInfo() const { return m_BossInfo; }
    Swarm::AICB& GetAIParams() { return m_CachedAICB; }
    // 自爆兵の定数（次の固定ステップ / 次の描画から効く）。blastArea は呼ぶ側が AreaProfileDB から入れる
    Swarm::BomberCB& GetBomberParams() { return m_CachedBomberCB; }
    // 玩家が受けた累計ダメージを取り出して 0 に戻す
    float ConsumePlayerDamage();
    // 磁石: seconds の間、場の経験値オーブを全部吸い寄せ始める（OrbCB の吸い寄せ半径を場全体にする）
    void MagnetAllOrbs(float seconds = 0.3f) { m_MagnetTimer = (std::max)(m_MagnetTimer, seconds); }
    // TEMP-TEST: 敵の池と状態を丸ごと読み戻す（Map で止まる。自測の検証だけ。毎フレーム呼ばない）
    bool DebugReadEnemies(std::vector<Swarm::Enemy>& outEnemies, std::vector<uint32_t>& outStates);
    // 光線（胶囊型の範囲）: チャンネル ch の起点 / 終点 / 半径を次の固定ステップから効かせる。
    // 範囲そのものは SpawnArea（flags に kAreaCapsule | ch << kAreaBeamShift）で出す。
    // active = false にすると GPU 側の範囲が次のステップで消える
    void SetBeam(uint32_t ch, const DirectX::SimpleMath::Vector3& start, const DirectX::SimpleMath::Vector3& end,
        float radius, bool active);

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


    void RecycleEnemy(const Vector3& pos, float hp, float moveSpeed,
        uint32_t kind = Swarm::kEnemyKindMob);
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
    void DispatchSprites(float dt);                  // 範囲の連番絵：古い物を進めて、生まれた範囲の分を始める
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

    // 雑魚の種類と自爆兵の導火線（Swarm::EnemyExtra、スロットと同じ添字）。
    // SpawnEnemyCS / RecycleCS が書き、ContactCS が導火線を進める。AI・Compact・VS が読む
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyExtraBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyExtraUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyExtraSRV;

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
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaEndBuffer;      // 槽ごとの胶囊の終点（float4。AreaTickCS が書く）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaEndUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaEndSRV;
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

    // 弾スロット毎の誘発タグ（Swarm::TriggerEvent の説明）。SpawnProjCS が書き、ProjEndCS が消す
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjTagBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjTagUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjTagSRV;

    // 誘発の環（ProjEndCS が書く）→ staging 3 枚で回読。総数は GPU 上で永久に累加、CPU は読んだ所まで覚える
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_TriggerBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TriggerUAV;
    static constexpr int kTriggerStaging = 3;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TriggerStaging[kTriggerStaging];
    bool m_TriggerStagingFilled[kTriggerStaging] = {};
    int  m_TriggerStagingWrite = 0;
    uint32_t m_TriggerRead = 0;                         // 読み終えた総数
    std::vector<Swarm::TriggerEvent> m_TriggerEvents;   // 読んだが、まだ誰も取り出していない物
    void ReadTriggerEvents();   // 一番古い staging を読めたら新しい分を m_TriggerEvents へ（Flush の頭）

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

    // ---- 範囲の連番絵（SwarmSprite.hlsli）----
    // 再生中の表は環（満杯なら一番古い物から上書き）。範囲が消えても再生は続く。
    // areaSeen = 範囲の槽ごとに前のフレームの timeLeft（asuint）。0xFFFFFFFF = 空・未見
    static constexpr uint32_t kMaxSprites = 1024;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SpriteBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SpriteUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_SpriteSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AreaSeenBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaSeenUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaSeenSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SpriteHead;       // 環の書き込み位置（RAW）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SpriteHeadUAV;
    std::shared_ptr<ComputeShader> m_SpriteCS;
    std::shared_ptr<VertexShader>  m_SpriteVS;
    std::shared_ptr<PixelShader>   m_SpritePS;

    // --- 地形（起動時に1回。読み取り専用）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TerrainBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TerrainSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HeightBuffer;      // 高さ場（GridWorld::Heights）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;

    // --- 雑魚の空間ハッシュ（地形と同じ格子。毎ステップ BinCS が詰め直す）---
    // cellCount[cell] = そのマスの活き数、cellItems[cell*CAP + k] = スロット番号。
    // AI の分離と PushCS の押し出しは 3x3 マスしか見ない（全対全をやめた）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CellCountBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CellCountUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CellCountSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CellItemsBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CellItemsUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CellItemsSRV;
    static constexpr uint32_t kBucketCap = 32;   // = SwarmCommon.hlsli の SWARM_BUCKET_CAP

    // --- 巡路（流れ場）---
    // CPU の FlowField が玩家のマスへの向きをマス毎に持ち、GPU の AI が読む。
    // 玩家のマスが変わった時だけ作り直して Map で上げる（地形は静的）
    FlowField m_Flow;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FlowBuffer;      // float2 × マス数（DYNAMIC）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_FlowSRV;
    void UpdateFlowField(const DirectX::SimpleMath::Vector3& playerPos);

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
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnProjExtraBuffer;   // 依頼と同じ添字の uint2（誘発タグ, kSpawnAtPos）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnProjExtraSRV;
    std::vector<uint32_t> m_PendingProjExtra;                      // 2 個ずつ（m_PendingProjectiles と並ぶ）

    std::vector<Swarm::Enemy>      m_PendingEnemies;
    std::vector<Swarm::Projectile> m_PendingProjectiles;

    // --- 定数バッファ ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FrameCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnCB;
    Swarm::FrameCB m_CachedFrameCB;
    Swarm::AICB m_CachedAICB;
    Swarm::OrbCB m_CachedOrbCB;
    Swarm::BomberCB m_CachedBomberCB;
    Swarm::BeamCB m_CachedBeamCB = {};   // 光線の起点 / 終点（SetBeam）

    // ============================================================
    // CS 群
    // 順序は DispatchStep / DispatchEmit が決める。宣言順に意味は無い
    // ============================================================
    std::shared_ptr<ComputeShader> m_ClearCountersCS;  // 子ステップ頭で counter を 0 に
    std::shared_ptr<ComputeShader> m_SpawnProjCS;      // 生成依頼を投射物の空きスロットへ
    std::shared_ptr<ComputeShader> m_ProjMoveCS;       // 投射物の積分
    std::shared_ptr<ComputeShader> m_ProjEndCS;        // タグ付きの弾が消えた場所を誘発の環へ（命中の直後）
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
    std::shared_ptr<PixelShader>  m_OrbPS;         // 自発光の宝石（SwarmOrbPS）
    std::shared_ptr<Material>     m_OrbMaterial;
    std::shared_ptr<Model>        m_OrbModel;      // 双角錐（PrimitiveBuilder::CreateBipyramid）
    // 吸い寄せ中のオーブから粒子（SwarmEmitCS と同じ発射。配方は VFXId::ExpOrbTrail 固定）
    std::shared_ptr<ComputeShader> m_OrbEmitCS;
    uint32_t m_OrbTrailVfx = 0;                    // 配方表の番号。0 = 無し（json が読めなかった）

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
    std::shared_ptr<ComputeShader> m_EnemyBinCS;         // 空間ハッシュ詰め（ステップ先頭）
    std::shared_ptr<ComputeShader> m_EnemyPushCS;        // 重なり解消（積分の後）

    // --- 雑魚描画の間接引数 ---
    // 4096 槽を毎フレーム全部 DrawInstanced すると頂点数がモデル × 4096 になる
    // （Minion 8.6k 頂点で 3500 万）。活きスロットだけ描くために
    // CompactCS → 種類毎の一覧、CopyStructureCount → args[種類][submesh].InstanceCount。
    // 種類はメッシュが同じで貼図だけ違うので、一覧毎に 1 回ずつ描く。
    // aliveList（全種類）は HP バー用
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AliveListBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AliveListUAV;   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AliveListSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_KindListBuffer[Swarm::kEnemyKinds];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_KindListUAV[Swarm::kEnemyKinds];   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_KindListSRV[Swarm::kEnemyKinds];
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_EnemyDrawArgs[Swarm::kEnemyKinds];  // submesh 毎（IndexCount が違う）
    bool CreateEnemyDrawArgs(ID3D11Device* device);
    std::shared_ptr<Texture> m_BomberAlbedo;   // 自爆兵の貼図（雑魚と同じメッシュ用）。null = 雑魚と同じ貼図

    // --- Boss の様子（CompactCS が書く 32B → staging 3 枚で回読。counter と同じ流儀で待たない）---
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_BossInfoBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_BossInfoUAV;
    static constexpr int kBossStaging = 3;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BossStaging[kBossStaging];
    bool m_BossStagingFilled[kBossStaging] = {};
    int  m_BossStagingWrite = 0;
    Swarm::BossInfo m_BossInfo;
    float m_MagnetTimer = 0.0f;   // MagnetAllOrbs の残り秒（> 0 の間 OrbMoveCS の吸い寄せ半径を場全体に）
    void ReadBossInfo();   // 一番古い staging を読めたら m_BossInfo を更新（Flush の頭）

    // --- 雑魚の部品アニメ（部品を節点で動かすモデルだけ。Kenney Blocky）---
    // 表: [クリップ][フレーム][部品] の行列。行列は「焼いた姿勢の部品 → そのフレームの部品」の差分
    //     （頂点は焼いた姿勢で入っているので、VS はこれを掛けるだけで動く）。
    // クリップ: 0 待機 / 1 歩き / 2 近接攻撃。どれを出すかは VS が敵の状態から決める
    struct EnemyAnimCB
    {
        uint32_t part = 0;           // 今描いている submesh（描画毎に書き換える）
        uint32_t partCount = 0;
        uint32_t enabled = 0;        // 0 = 表が無い → VS は従来の procedural な揺れ
        uint32_t _pad = 0;
        uint32_t clipStart[4] = {};  // 表の中の最初のフレーム番号
        uint32_t clipFrames[4] = {};
        float    clipLength[4] = {}; // 秒
        float    time = 0.0f;        // 待機に使う時計（秒）
        float    walkRate = 1.0f;    // 歩きの再生速度
        float    _pad2[2] = {};
    };
    static_assert(sizeof(EnemyAnimCB) == 80, "EnemyAnimCB layout mismatch");
    EnemyAnimCB m_EnemyAnim;
    const char* m_AnimClips[3] = { "", "", "" };   // 待機 / 歩き / 攻撃 のクリップ名（BuildEnemyModel が入れる）
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_PartAnimBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_PartAnimSRV;
    float m_AnimClock = 0.0f;
    // 焼いた姿勢（bakeClip の bakeFrac）と rootTransform から表を作る。失敗したら enabled = 0 のまま
    bool BuildEnemyPartAnim(ID3D11Device* device, const char* modelPath,
        const char* bakeClip, float bakeFrac, const DirectX::SimpleMath::Matrix& rootTransform,
        size_t partCount);

    // --- 雑魚の HP バー ---
    // 生成時の hp（固定小数、スロット毎）。SpawnEnemyCS / RecycleCS が書き、バーの VS が割る。
    // Enemy 本体（48B）に場所が無いので横に持つ
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_EnemyMaxHpBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyMaxHpUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyMaxHpSRV;
    // DrawInstancedIndirect: { 6 頂点, InstanceCount = 活き数（CopyStructureCount）, 0, 0 }
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HpBarArgs;
    std::shared_ptr<VertexShader> m_HpBarVS;
    std::shared_ptr<PixelShader>  m_HpBarPS;
    void RenderHpBars(CameraBase* camera);

    // --- 雑魚の足元の丸い影（間接引数は HP バーと同じ: 6 頂点 x 活き数）---
    std::shared_ptr<VertexShader> m_BlobShadowVS;
    std::shared_ptr<PixelShader>  m_BlobShadowPS;
    void RenderBlobShadows(CameraBase* camera);

    // --- 自爆兵の警告の輪 ---
    // DrawInstancedIndirect: { 6 頂点, InstanceCount = 自爆兵の一覧の長さ, 0, 0 }。点火していない分は VS が捨てる
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BomberRingArgs;
    std::shared_ptr<VertexShader> m_BomberRingVS;
    std::shared_ptr<PixelShader>  m_BomberRingPS;
    void RenderBomberRings(CameraBase* camera);

    // --- 隕石の警告の輪 ---
    // DrawInstanced(6, kMaxProjectiles): 弾の全スロット。DROP でない・死んだ分は VS が捨てる。
    // PS は m_BomberRingPS を共用（b0 の並びが同じ）
    std::shared_ptr<VertexShader> m_DropRingVS;
    void RenderDropRings(CameraBase* camera);

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
    // SwarmEnemyHpBarVS / PS の b0（row_major なので Transpose しない）
    struct HpBarCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        float width, height, offset, border;
        DirectX::SimpleMath::Vector4 fill;
        DirectX::SimpleMath::Vector4 back;
        DirectX::SimpleMath::Vector4 edge;
    };
    static_assert(sizeof(HpBarCB) == 192, "HpBarCB layout mismatch");
    // SwarmBlobShadowVS / PS の b0（row_major なので Transpose しない）
    struct BlobShadowCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        float radius, strength, lift, softness;
        float clamp, _pad[3];
    };
    static_assert(sizeof(BlobShadowCB) == 160, "BlobShadowCB layout mismatch");
    // SwarmBomberRingVS / PS の b0（row_major なので Transpose しない）
    struct BomberRingCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        DirectX::SimpleMath::Vector4 fill;
        DirectX::SimpleMath::Vector4 edge;
        DirectX::SimpleMath::Vector4 back;
        float edgeWidth, lift, _pad[2];
    };
    static_assert(sizeof(BomberRingCB) == 192, "BomberRingCB layout mismatch");
    // SwarmOrbVS の b4（orbLook の動きの分）
    struct OrbLookCB
    {
        float time, scale, bobHeight, bobSpeed;
        float spinSpeed, pulseAmount, pulseSpeed, fullPullSpeed;
        float stretchPerSpeed, stretchMax, tiltMax, pullGlow;
        DirectX::SimpleMath::Vector4 idleColor;
        DirectX::SimpleMath::Vector4 pullColor;
    };
    static_assert(sizeof(OrbLookCB) == 80, "OrbLookCB layout mismatch");
    // SwarmOrbPS の b1（b0 は LightBuffer）
    struct OrbShadeCB
    {
        float emissive, facet, rimGain, rimPower;
        float glintGain, glintPower, _pad[2];
        DirectX::SimpleMath::Vector4 rimColor;
    };
    static_assert(sizeof(OrbShadeCB) == 48, "OrbShadeCB layout mismatch");
    // SwarmOrbEmitCS の b3
    struct OrbEmitCB
    {
        uint32_t vfx;
        float    minSpeed;
        uint32_t _pad[2];
    };
    static_assert(sizeof(OrbEmitCB) == 16, "OrbEmitCB layout mismatch");
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
