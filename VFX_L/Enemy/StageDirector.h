// ============================================================
// StageDirector.h
// 1 面の進行（Megabonk 風）。雑魚の湧き・難度そのものは MobSpawner、ここは時間と門で起きる出来事。
//   ・制限時間 stageTime（10 分）。HUD は残りを数え下ろす
//   ・決まった経過時間（3:00 / 8:00）にエリートを 1 体（GPU の雑魚の種類 kEnemyKindElite）。
//     HP は eliteHp × その時の難度の倍率。体格・接触ダメージ・経験値は SwarmSystem::GetBomberParams の elite*
//   ・時間切れの後は「最終ウェーブ」（Megabonk の Final Swarm）：湧きを finalSpawnRate に上げ、速さ × finalSpeedMul、
//     finalStepTime 秒毎に強さ × finalStepMul。同時の上限は最初の 2 分 finalCap、その後 finalCapLate。
//     MobSpawner の final* に書いて効かせる
//   ・Boss の門：開始時にプレイヤーから離れた所へ 1 つ。近づいて F で面の Boss（kEnemyKindBoss）を呼ぶ。
//     いつ呼んでもよい（遅いほど難度の倍率で HP が高い）。Boss を倒すと面をクリア
//     （Boss の生死は GPU からのリードバック SwarmSystem::GetBossInfo で見る）
// 時間は戦闘の経過秒（一時停止・バックパック・三択の間は進まない。HUD の計時と同じ）
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include <SimpleMath.h>
#include <cstdint>
#include <memory>
#include <vector>

class GridWorld;
class SwarmSystem;
class MobSpawner;
class Registry;
class InteractionSystem;
class Model;

class StageDirector
{
public:
    void Init();

    void Update(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float runTime, float dt,
        MobSpawner& mobs, SwarmSystem& swarm);

    // 門を置き直す（開始時・地形の作り直し）。center の周り portalMinDist〜portalMaxDist の平らな所。
    // preferred があればまずその近く（10 マス以内で一番近い平らな所。三層のフィールドでは洞窟の一番奥）。
    // 門の面は faceToward（無ければ center）の方を向く
    void SpawnPortal(Registry& reg, const GridWorld& grid, const DirectX::SimpleMath::Vector3& center, uint32_t seed,
        InteractionSystem& interaction, const DirectX::SimpleMath::Vector3* preferred = nullptr,
        const DirectX::SimpleMath::Vector3* faceToward = nullptr,
        const DirectX::SimpleMath::Vector4* fixed = nullptr);
    // fixed: 地図に置いてある門（MapData::Placement。xyz = 口の真ん中の地面、w = 向きの度）。あればその通りに置く
    // 使われた物が門なら Boss を呼ぶ（呼んだら true）。門は使えなくなる（見た目は残す）
    bool TryUsePortal(Registry& reg, Entity used, const GridWorld& grid, const DirectX::SimpleMath::Vector3& player,
        const MobSpawner& mobs, SwarmSystem& swarm, InteractionSystem& interaction);

    // 残り時間（0 未満にはしない）
    float Remaining(float runTime) const { return (stageTime - runTime > 0.0f) ? stageTime - runTime : 0.0f; }
    // 最終ウェーブの段（0 = まだ。1 から finalStepTime 秒毎に上がる）
    int FinalSwarmLevel() const { return m_FinalLevel; }

    // ---- Boss ----
    bool IsBossAlive() const { return m_Boss == BossState::Alive; }
    bool IsCleared() const { return m_Boss == BossState::Defeated; }
    float BossHpRatio() const { return m_BossHpRatio; }
    DirectX::SimpleMath::Vector3 BossPos() const { return m_BossPos; }
    Entity GetPortal() const { return m_Portal; }
    // 門の向き（度。TransformComponent の yaw = 局所 +Z が向く方）と、渦の中心（世界、門の口の真ん中）
    float GetPortalYaw() const { return m_PortalYaw; }
    // 拱の柱の衝突（シーンが下のマスを雑魚用に塞ぐ）
    const std::vector<Entity>& GetPortalPillars() const { return m_PortalPillars; }
    DirectX::SimpleMath::Vector3 GetPortalCenter() const { return m_PortalCenter; }

    // Enemies パネルの「Stage」の段
    void DrawImGui(SwarmSystem& swarm, float runTime);

    // 最後に起きた出来事（自動テストの記録用）。無ければ空
    const char* ConsumeEvent() { const char* e = m_Event; m_Event = nullptr; return e; }

    // ---- 調整値 ----
    float stageTime = 600.0f;                    // 秒（Megabonk の 1 面 10 分）
    std::vector<float> eliteTimes = { 180.0f, 480.0f };   // エリートを出す経過秒（3:00 / 8:00）
    float eliteHp = 250.0f;                      // 倍率 1 の時の HP（Megabonk の小ボス）
    float eliteSpeed = 3.2f;                     // m/秒（雑魚 3.5 より少し遅い）
    float eliteRingMin = 18.0f;                  // プレイヤーから m（画面の中に歩いて来るのが見える距離）
    float eliteRingMax = 24.0f;

    float finalSpawnRate = 20.0f;   // 最終ウェーブの湧き（体/秒）
    float finalSpeedMul = 1.3f;     // 最終ウェーブで湧く雑魚の速さ
    float finalStepTime = 30.0f;    // 秒毎に 1 段（Megabonk は 30〜40 秒毎に強くなる）
    float finalStepMul = 1.5f;      // 1 段毎の強さの倍率
    int   finalCap = 400;           // 最初の 2 分の同時上限（Megabonk と同じ）
    int   finalCapLate = 300;       // 2 分より後
    float finalGhostRate = 2.0f;    // 最終ウェーブの幽霊（体/秒。時間切れの直後）。雑魚の湧きとは別枠、上限は共通
    float finalGhostStepMul = 1.5f; // 1 段毎の幽霊の数の倍率（2 → 3 → 4.5 …）

    float bossHp = 8000.0f;         // 倍率 1 の時の HP（Megabonk 1 面の Boss 8000〜10000）
    float bossSpeed = 2.8f;         // m/秒
    float bossRingMin = 12.0f;      // 呼んだ時のプレイヤーからの距離（m）
    float bossRingMax = 16.0f;
    float portalMinDist = 35.0f;    // 門を置く距離（開始時のプレイヤーから m）
    float portalMaxDist = 55.0f;
    float portalHeight = 5.6f;      // 門の高さ（m。モデルの包囲箱から倍率を出す。石の拱 3.53m → ×1.59、幅 4.9m）

private:
    enum class BossState { None, Summoned, Alive, Defeated };

    bool SpawnElite(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, float hp, SwarmSystem& swarm);
    bool SpawnBoss(const GridWorld& grid, const DirectX::SimpleMath::Vector3& player, SwarmSystem& swarm);

    size_t m_NextElite = 0;       // eliteTimes の次に待つ番号
    int    m_ElitesSpawned = 0;
    bool   m_DebugElite = false;  // パネルのボタン。次の Update で 1 体
    int    m_FinalLevel = 0;
    int    m_NormalCap = -1;      // 最終ウェーブに入る前の同時上限（戻す時用。-1 = まだ覚えていない）
    const char* m_Event = nullptr;

    std::shared_ptr<Model> m_PortalModel;
    Entity m_Portal = EntityTraits::NULL_ENTITY;
    std::vector<Entity> m_PortalPillars;   // 拱の 2 本の柱の衝突（プレイヤーが柱を抜けないように）
    float m_PortalYaw = 0.0f;
    DirectX::SimpleMath::Vector3 m_PortalCenter;
    BossState m_Boss = BossState::None;
    float  m_BossSpawnHp = 0.0f;  // 呼んだ時に決めた HP（出直しても同じ）
    float  m_BossWait = 0.0f;     // 呼んでからリードバックに現れるまでの秒（長すぎたら湧かせ直す）
    float  m_BossHpRatio = 0.0f;
    DirectX::SimpleMath::Vector3 m_BossPos;
    DirectX::SimpleMath::Vector3 m_SummonPos;   // 呼んだ時のプレイヤーの位置（湧かせ直す時に使う）
    bool   m_DebugBoss = false;   // パネルのボタン
};
