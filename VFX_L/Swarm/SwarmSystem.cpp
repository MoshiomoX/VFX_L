// ============================================================
// SwarmSystem.cpp
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Particle/GPUParticleSystem.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Renderer/Renderer.h"   // DissolveCB
#include "Camera/CameraBase.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/SkinnedModel.h"
#include "Graphics/Light/PointLightManager.h"
#include "Manager/ResourceManager.h"
#include "ResourcePaths.h"
#include "World/GridWorld.h"
#include "VFX_Editor/VFXSpriteRenderer.h"   // VFXSpriteCameraCB
#include "VFX_Editor/VFXLiquidRenderer.h"   // 液溜まりの CameraCB / FrameCB（CPU の経路と同じ並び）

namespace
{
    // 雑魚モデルの倍率。KayKit はメートル（身長 ≒ 1.8）でカプセル（1.8）と同じなので 1
    constexpr float kEnemyModelScale = 1.0f;
}

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// 初期化
// ============================================================
bool SwarmSystem::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;

    if (!CreateBuffers(device)) return false;
    if (!m_Readback.Initialize(device)) return false;

    // 読み込み失敗しても続行する（未実装の CS があっても他は動くように）
    LoadShaders(device);

    m_PendingEnemies.reserve(Swarm::kMaxSpawnEnemyPerFrame);
    m_PendingProjectiles.reserve(Swarm::kMaxSpawnProjPerFrame);
    m_PendingRecycles.reserve(Swarm::kMaxSpawnEnemyPerFrame);
    std::cout << "[OK] SwarmSystem initialized (enemies "
        << Swarm::kMaxEnemies << ", projectiles "
        << Swarm::kMaxProjectiles << ")" << std::endl;
    return true;
}

void SwarmSystem::Shutdown()
{
    WaitFlowJob();
    m_Readback.Shutdown();
}

// ============================================================
// VFX レシピ表の構築（起動時に1回）
// ============================================================
bool SwarmSystem::BuildVFXTable()
{
    const bool ok = m_VFX.Build(m_Device, m_Particles);
    // 吸い寄せ中のオーブの尾。json が無い・読めない時は 0（尾が出ないだけ）
    m_OrbTrailVfx = m_VFX.IndexOf(VFXId::ExpOrbTrail);
    m_TornadoDustVfx = m_VFX.IndexOf(VFXId::TornadoDust);   // 砂旋の足元の砂埃
    return ok;
}

// ============================================================
// 生成依頼を溜める（実際に GPU へ入るのは Flush）
// ※state は本体に無い。スロットの生死は SpawnCS が
//   state buffer 側で InterlockedCompareExchange して立てる
// ============================================================
// ※種類は依頼の animIndex に入れて運ぶ（SpawnEnemyCS / RecycleCS が並行バッファへ移す）
void SwarmSystem::SpawnEnemy(const Vector3& pos, float hp, float moveSpeed, uint32_t kind, float yaw)
{
    if (m_PendingEnemies.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;

    Swarm::Enemy e = {};
    e.position = pos;
    e.yaw = yaw;   // SpawnEnemyCS は依頼をそのまま槽へ写すので残る（砂旋の飛ぶ向き）
    e.hp = Swarm::HpToFixed(hp);
    e.moveSpeed = moveSpeed;
    e.animIndex = kind;
    m_PendingEnemies.push_back(e);
}
void SwarmSystem::RecycleEnemy(const Vector3& pos, float hp, float moveSpeed, uint32_t kind)
{
    if (m_PendingRecycles.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;
    Swarm::Enemy e = {};
    e.position = pos;
    e.hp = Swarm::HpToFixed(hp);
    e.moveSpeed = moveSpeed;
    e.animIndex = kind;
    m_PendingRecycles.push_back(e);
}
void SwarmSystem::SpawnProjectile(VFXId vfx, const Vector3& pos, const Vector3& vel,
    float damage, float radius, float lifetime, uint32_t motion, bool mirror,
    uint32_t triggerTag, bool spawnAtPos, float areaDamageMul, float areaDurationMul, uint32_t roll, uint32_t chainCount, uint32_t fanIndex)
{
    if (m_PendingProjectiles.size() >= Swarm::kMaxSpawnProjPerFrame) return;

    Swarm::Projectile p = {};
    p.position = pos;
    p.damage = damage;
    p.velocity = vel;
    p.lifetime = lifetime;
    p.radius = radius;
    p.vfxType = m_VFX.IndexOf(vfx);
    // 表の外は直進へ落とす。bit31 は SpawnProjCS が剥がす
    p.motion = (motion < Swarm::kMaxMotions) ? motion : 0u;
    if (mirror) p.motion |= Swarm::kMotionFlipBit;
    p.motion |= (std::min) (fanIndex, Swarm::kMotionFanMask) << Swarm::kMotionFanShift;
    // 連鎖のルーン: 跳ぶ回数は依頼の pathT で渡す（SpawnProjCS が読んでから 0 / -hold に戻す）
    p.pathT = (float) (std::min) (chainCount, 255u);
    m_PendingProjectiles.push_back(p);
    m_PendingProjExtra.push_back(triggerTag);
    m_PendingProjExtra.push_back((spawnAtPos ? Swarm::kSpawnAtPos : 0u)
        | ((roll & Swarm::kSpawnRollMask) << Swarm::kSpawnRollShift)
        | Swarm::PackSpawnBoost(areaDamageMul, areaDurationMul));
    ++m_TotalRequested;
}

// ============================================================
// 運動表の差し替え
// 行数が足りない分は Straight（全 0）で埋める
// ============================================================
void SwarmSystem::SetMotions(const std::vector<Swarm::Motion>& motions)
{
    if (!m_MotionBuffer) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_MotionBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    auto* dst = static_cast<Swarm::Motion*>(mapped.pData);
    for (uint32_t i = 0; i < Swarm::kMaxMotions; ++i)
        dst[i] = (i < motions.size()) ? motions[i] : Swarm::Motion{};

    m_Context->Unmap(m_MotionBuffer.Get(), 0);
}

// ============================================================
// 範囲攻撃
// ============================================================
void SwarmSystem::SpawnArea(const Swarm::Area& area)
{
    if (m_PendingAreas.size() >= Swarm::kMaxSpawnAreaPerFrame) return;
    m_PendingAreas.push_back(area);
}

void SwarmSystem::SetAreaDefs(const std::vector<Swarm::AreaDef>& defs)
{
    if (!m_AreaDefBuffer) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_AreaDefBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;

    auto* dst = static_cast<Swarm::AreaDef*>(mapped.pData);
    for (uint32_t i = 0; i < Swarm::kMaxAreaDefs; ++i)
        dst[i] = (i < defs.size()) ? defs[i] : Swarm::AreaDef{};

    m_Context->Unmap(m_AreaDefBuffer.Get(), 0);
}

void SwarmSystem::ClearAreas()
{
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_AreaStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_AreaDirUAV.Get(), zero);   // 消えた範囲の向きを次の範囲に渡さない
    m_PendingAreas.clear();
}

// ============================================================
// Flush
// 粒子の Flush と同じ位置（UpdateGameplay の末尾）、粒子より前に呼ぶ。
// 違いは中で固定ステップを回すこと
// ============================================================
void SwarmSystem::Flush(const Vector3& playerPos, float playerRadius,
    bool playerAlive, float dt, float totalTime)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    m_AnimClock = totalTime;   // 雑魚の待機アニメの時計（歩き・攻撃は敵ごとの時計）

    // ============================================================
    // 1) 前フレームの counter を読む（ブロックしない）
    // killCount / playerDamage は GPU 上で永久に累積される。
    // 前回値との差分を取る（リードバックが失敗したフレームがあっても取りこぼさない）
    // ============================================================
    SwarmCounters c;
    if (m_Readback.TryRead(m_Context, c))
    {
        const uint32_t dmgDelta = c.playerDamage - m_LastDamageTotal;
        m_LastDamageTotal = c.playerDamage;
        const uint32_t expDelta = c.expTotal - m_LastExpTotal;
        m_LastExpTotal = c.expTotal;
        m_PendingExp += (float)expDelta * 0.01f;   // 固定小数 × 100 を戻す
        m_PendingPlayerDamage += (float)dmgDelta * 0.01f;   // 固定小数 × 100 を戻す

        m_LastKillCount = c.killCount;
    }
    ReadBossInfo();
    ReadEliteInfo();
    ReadPlayerHits();
    ReadAreaBirths();
    ReadBeamTargets();
    ReadWindSources();   // 風源の表の CPU の写し（マント・転がり草。SwarmSystemTornado.cpp）
    ReadTriggerEvents();
    ReadSplitEvents();
    ReadDamageTexts();
    ReadLifesteal();
    ReadCloseTarget();   // 優先度なしの一番近い敵（近距離の範囲魔法の狙い）
    if (m_MagnetTimer > 0.0f) m_MagnetTimer -= dt;

    // ---- 2) 定数と生成依頼を上げる ----
    UploadFrameCB(playerPos, playerRadius, playerAlive);
    UploadSpawns();
    if (playerAlive) UpdateFlowField(playerPos);   // 死んだら最後の場のまま（AI も止まる）

    // ============================================================
    // 3) 固定ステップ
    // dt が結果に影響しないようにする。
    // 上限を設けるのは、コマ落ち時に「遅い→子ステップが増える→
    // もっと遅い」の死のスパイラルに入らないため
    // ============================================================
    m_Accumulator += dt;
    if (m_Accumulator > Swarm::kFixedStep * Swarm::kMaxSubSteps)
        m_Accumulator = Swarm::kFixedStep * Swarm::kMaxSubSteps;

    m_LastSubSteps = 0;
    while (m_Accumulator >= Swarm::kFixedStep)
    {
        DispatchStep();
        m_Accumulator -= Swarm::kFixedStep;
        ++m_LastSubSteps;
    }

    // ---- 4) 弾から粒子を発射（粒子の Flush より前に済ませる）----
    // 見た目の話なので可変 dt でよい（固定ステップの外）
    DispatchEmit(dt, totalTime);

    // 光線の標的（敵の位置が決まった後）
    DispatchBeamTargets();

    // 液溜まりの追跡（新しく生まれた範囲の向きと年齢。範囲の数え下げが済んだ後、1 フレーム 1 回）
    DispatchLiquidTrack();

    // 死んだ敵の砕け散り（このフレームの固定ステップで死んだ物を環へ。描く一覧も作る）
    DispatchCorpses();
    // ダメージの数字（槽毎の hp の減り → 環）
    DispatchDamageText(dt);

    // ---- 5) counter の写しを発行（CopyResource だけ。待たない）----
    RequestReadback();
    // プレイヤーが受けた打撃の向きも同じ時に写す（counter と同じ遅れで届く）
    if (m_PlayerHitBuffer)
    {
        m_Context->CopyResource(m_PlayerHitStaging[m_PlayerHitStagingWrite].Get(), m_PlayerHitBuffer.Get());
        m_PlayerHitStagingFilled[m_PlayerHitStagingWrite] = true;
        m_PlayerHitStagingWrite = (m_PlayerHitStagingWrite + 1) % kBossStaging;
    }
    if (m_AreaBirthBuffer)
    {
        m_Context->CopyResource(m_AreaBirthStaging[m_AreaBirthStagingWrite].Get(), m_AreaBirthBuffer.Get());
        m_AreaBirthStagingFilled[m_AreaBirthStagingWrite] = true;
        m_AreaBirthStagingStale[m_AreaBirthStagingWrite] = false;   // KillAll の後の写し
        m_AreaBirthStagingWrite = (m_AreaBirthStagingWrite + 1) % kBossStaging;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    m_FlushMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// ============================================================
// 毎フレームの定数
// ※m_CachedFrameCB を丸ごと = {} で初期化しないこと。
//   gridOrigin / cellSize / gridW / gridD は UploadTerrain が
//   入れた値で、ここで消すと CS が格子を引けなくなる
// ============================================================
void SwarmSystem::UploadFrameCB(const Vector3& playerPos, float playerRadius,
    bool playerAlive)
{
    m_CachedFrameCB.playerPos = playerPos;
    m_CachedFrameCB.playerRadius = playerRadius;
    m_CachedFrameCB.step = Swarm::kFixedStep;
    m_CachedFrameCB.maxEnemies = Swarm::kMaxEnemies;
    m_CachedFrameCB.maxProjectiles = Swarm::kMaxProjectiles;
    m_CachedFrameCB.maxOrbs = Swarm::kMaxOrbs;
    m_CachedFrameCB.playerAlive = playerAlive ? 1u : 0u;
    m_CachedFrameCB.seed = ++m_FrameSeed;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (SUCCEEDED(m_Context->Map(m_FrameCB.Get(), 0,
        D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
    {
        memcpy(mapped.pData, &m_CachedFrameCB, sizeof(Swarm::FrameCB));
        m_Context->Unmap(m_FrameCB.Get(), 0);
    }
}
// ============================================================
// GPU 上の雑魚・弾・オーブを全部消す（地形の作り直し用）
// state を 0（DEAD）にするだけ。本体バッファは触らない。
// ※counter / emitBudget / accumulator は触らない。
//   killCount 等は永久累積で CPU が差分を取るため、
//   ここで 0 にすると次のリードバックで差分が狂う
// ============================================================
void SwarmSystem::KillAll()
{
    ++m_KillAllCount;   // 面の固定エリートは CPU が追っているので、消えたのを「倒された」と見ないように
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_EnemyStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_ProjStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_ProjTagUAV.Get(), zero);   // 消した弾を「消えた」と報告させない
    m_Context->ClearUnorderedAccessViewUint(m_OrbStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_AreaStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_AreaDirUAV.Get(), zero);
    // 「前は生きていた」も消す：全消しで全員が一度に砕け散らないように
    m_Context->ClearUnorderedAccessViewUint(m_EnemyPrevAliveUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_EnemyPrevHpUAV.Get(), zero);      // ダメージの数字：全消しで「倒した」数字を出さない
    m_Context->ClearUnorderedAccessViewUint(m_DamagePendingUAV.Get(), zero);
    m_PendingAreas.clear();

    // まだ GPU に上げていない依頼も捨てる（消した直後に湧き直さないように）
    m_PendingEnemies.clear();
    m_PendingRecycles.clear();
    m_PendingProjectiles.clear();
    m_PendingProjExtra.clear();

    // まだ読んでいない写し（staging に 2〜3 フレーム分）の中身は消す前の出来事：読めても読んだ位置 / 前回値を
    // 進めるだけで捨てる。消した後・地形を作り直した後に分裂体・蘇った鎧・砂旋・骸骨・誘発の隕石・数字・吸血の回復・
    // 範囲の音が湧かないように。累計は差分の基準だけ進むので跳ばない。CPU で取り出し待ちの物も捨てる
    for (int k = 0; k < kTriggerStaging; ++k)
    {
        m_TriggerStagingStale[k] = m_TriggerStagingFilled[k];
        m_SplitStagingStale[k] = m_SplitStagingFilled[k];
        m_DamageTextStagingStale[k] = m_DamageTextStagingFilled[k];
    }
    for (int k = 0; k < kLifestealStaging; ++k)
        m_LifestealStagingStale[k] = m_LifestealStagingFilled[k];
    for (int k = 0; k < kBossStaging; ++k)
        m_AreaBirthStagingStale[k] = m_AreaBirthStagingFilled[k];
    m_TriggerEvents.clear();
    m_SplitEvents.clear();
    m_DamageTexts.clear();
    m_LifestealPending = 0.0f;
    m_PendingShakes = 0;
    m_PendingAreaBirths = {};
}

// ============================================================
// 弾だけ消す（負荷テストのリセット用）
// ============================================================
void SwarmSystem::ClearProjectiles()
{
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_ProjStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_ProjTagUAV.Get(), zero);
    m_PendingProjectiles.clear();
    m_PendingProjExtra.clear();
}
