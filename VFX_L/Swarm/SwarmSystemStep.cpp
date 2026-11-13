// ============================================================
// SwarmSystemStep.cpp
// 生成リクエストのアップロード（UploadSpawns）と固定ステップ 1 回分の dispatch（DispatchStep）。
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

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// 溜めた生成依頼を上げて、空きスロットへ流し込む
// ============================================================
void SwarmSystem::UploadSpawns()
{
    // ---- 投射物 ----
    if (!m_PendingProjectiles.empty() && m_SpawnProjCS)
    {
        // 1) 依頼を上げる
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(m_Context->Map(m_SpawnProjBuffer.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            memcpy(m.pData, m_PendingProjectiles.data(),
                sizeof(Swarm::Projectile) * m_PendingProjectiles.size());
            m_Context->Unmap(m_SpawnProjBuffer.Get(), 0);
        }
        if (SUCCEEDED(m_Context->Map(m_SpawnProjExtraBuffer.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            memcpy(m.pData, m_PendingProjExtra.data(), sizeof(uint32_t) * m_PendingProjExtra.size());
            m_Context->Unmap(m_SpawnProjExtraBuffer.Get(), 0);
        }

        // 2) 生成用の定数
        Swarm::SpawnCB scb;
        scb.requestCount = (uint32_t)m_PendingProjectiles.size();
        scb.scanStart = m_FrameSeed * 613u;   // 毎フレーム探索開始位置をずらす

        m_SpawnProjCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_SpawnProjCS->WriteBuffer(m_Context, 1, &scb);
        m_SpawnProjCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // 飛んでいる敵の高さ（着弾点を地面へ）

        // 3) dispatch
        m_SpawnProjCS->Bind(m_Context);
        m_SpawnProjCS->SetSRV(m_Context, "spawnRequests", m_SpawnProjSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "spawnExtra", m_SpawnProjExtraSRV.Get());
        // 曲線の型は生成時に標的を捕捉して path を組む。
        // counters には前ステップの「プレイヤーに一番近い雑魚」が残っている
        m_SpawnProjCS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 扇の 2 発目が選ぶ敵（埋まっている物などは選ばない）
        m_SpawnProjCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 着弾点が洞窟の中なら真上から落とす
        m_SpawnProjCS->SetUAV(m_Context, "projectiles", m_ProjUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "paths", m_PathUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projTags", m_ProjTagUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projBoost", m_ProjBoostUAV.Get());   // 出す範囲への倍率
        // 弾の帯：使い回す槽の環を空にする（前の弾の点から帯が伸びないように）
        m_SpawnProjCS->SetUAV(m_Context, "trailState", m_ProjTrailStateUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "trailAge", m_ProjTrailAgeUAV.Get());
        m_SpawnProjCS->BindUAVs(m_Context);

        m_Context->Dispatch((scb.requestCount + 63) / 64, 1, 1);

        m_SpawnProjCS->UnbindSRVs(m_Context);
        m_SpawnProjCS->UnbindUAVs(m_Context);

        ++m_TotalDispatched;
    }
    // ---- 範囲 ----
    if (!m_PendingAreas.empty() && m_SpawnAreaCS)
    {
        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(m_Context->Map(m_SpawnAreaBuffer.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            memcpy(m.pData, m_PendingAreas.data(),
                sizeof(Swarm::Area) * m_PendingAreas.size());
            m_Context->Unmap(m_SpawnAreaBuffer.Get(), 0);
        }

        Swarm::SpawnCB scb;
        scb.requestCount = (uint32_t)m_PendingAreas.size();
        scb.scanStart = m_FrameSeed * 211u;

        m_SpawnAreaCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_SpawnAreaCS->WriteBuffer(m_Context, 1, &scb);
        m_SpawnAreaCS->Bind(m_Context);
        m_SpawnAreaCS->SetSRV(m_Context, "spawnRequests", m_SpawnAreaSRV.Get());
        m_SpawnAreaCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
        m_SpawnAreaCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
        m_SpawnAreaCS->BindUAVs(m_Context);

        m_Context->Dispatch((scb.requestCount + 63) / 64, 1, 1);

        m_SpawnAreaCS->UnbindSRVs(m_Context);
        m_SpawnAreaCS->UnbindUAVs(m_Context);
    }
    m_PendingAreas.clear();

    // ---- 敵：新規 + 転送を1回で上げる ----
    // [0, newCount)          → SpawnEnemyCS が空きスロットへ
    // [newCount, total)      → RecycleCS が遠い活きスロットへ上書き
    if (!m_PendingEnemies.empty() || !m_PendingRecycles.empty())
    {
        m_EnemyUpload.clear();
        for (const auto& e : m_PendingEnemies)
        {
            if (m_EnemyUpload.size() >= Swarm::kMaxSpawnEnemyPerFrame) break;
            m_EnemyUpload.push_back(e);
        }
        const uint32_t newCount = (uint32_t)m_EnemyUpload.size();
        for (const auto& e : m_PendingRecycles)
        {
            if (m_EnemyUpload.size() >= Swarm::kMaxSpawnEnemyPerFrame) break;
            m_EnemyUpload.push_back(e);
        }
        const uint32_t recycleCount = (uint32_t)m_EnemyUpload.size() - newCount;

        D3D11_MAPPED_SUBRESOURCE m = {};
        if (SUCCEEDED(m_Context->Map(m_SpawnEnemyBuffer.Get(), 0,
            D3D11_MAP_WRITE_DISCARD, 0, &m)))
        {
            memcpy(m.pData, m_EnemyUpload.data(),
                sizeof(Swarm::Enemy) * m_EnemyUpload.size());
            m_Context->Unmap(m_SpawnEnemyBuffer.Get(), 0);
        }

        // 1) 新規
        if (newCount > 0 && m_SpawnEnemyCS)
        {
            Swarm::SpawnCB scb;
            scb.requestCount = newCount;
            scb.scanStart = m_FrameSeed * 1301u;

            m_SpawnEnemyCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
            m_SpawnEnemyCS->WriteBuffer(m_Context, 1, &scb);
            m_SpawnEnemyCS->Bind(m_Context);
            m_SpawnEnemyCS->SetSRV(m_Context, "spawnRequests", m_SpawnEnemySRV.Get());
            m_SpawnEnemyCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
            m_SpawnEnemyCS->SetUAV(m_Context, "enemyStates", m_EnemyStateUAV.Get());
            m_SpawnEnemyCS->SetUAV(m_Context, "enemyMaxHp", m_EnemyMaxHpUAV.Get());   // HP バーの分母
            m_SpawnEnemyCS->SetUAV(m_Context, "enemyExtra", m_EnemyExtraUAV.Get());   // 種類・導火線
            m_SpawnEnemyCS->SetUAV(m_Context, "enemySlow", m_EnemySlowUAV.Get());     // 新しい敵は減速なし
            m_SpawnEnemyCS->SetUAV(m_Context, "enemyPush", m_EnemyPushUAV.Get());     // 吹き飛ばしもなし
            m_SpawnEnemyCS->SetUAV(m_Context, "enemyPoison", m_EnemyPoisonUAV.Get()); // 毒素もなし
            m_SpawnEnemyCS->BindUAVs(m_Context);

            m_Context->Dispatch((newCount + 63) / 64, 1, 1);

            m_SpawnEnemyCS->UnbindSRVs(m_Context);
            m_SpawnEnemyCS->UnbindUAVs(m_Context);
        }

        // 2) 転送
        if (recycleCount > 0 && m_RecycleCS)
        {
            const UINT zero[4] = { 0, 0, 0, 0 };
            m_Context->ClearUnorderedAccessViewUint(m_RecycleClaimUAV.Get(), zero);

            Swarm::RecycleCB rcb;
            rcb.count = recycleCount;
            rcb.offset = newCount;
            rcb.minDistSq = m_RecycleMinDist * m_RecycleMinDist;

            m_RecycleCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
            m_RecycleCS->WriteBuffer(m_Context, 1, &rcb);
            m_RecycleCS->Bind(m_Context);
            m_RecycleCS->SetSRV(m_Context, "spawnRequests", m_SpawnEnemySRV.Get());
            m_RecycleCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
            m_RecycleCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
            m_RecycleCS->SetUAV(m_Context, "claim", m_RecycleClaimUAV.Get());
            m_RecycleCS->SetUAV(m_Context, "enemyMaxHp", m_EnemyMaxHpUAV.Get());     // 上書きした分の分母も差し替える
            m_RecycleCS->SetUAV(m_Context, "enemyExtra", m_EnemyExtraUAV.Get());     // 種類も差し替え、導火線は消える
            m_RecycleCS->SetUAV(m_Context, "enemySlow", m_EnemySlowUAV.Get());       // 減速も消える
            m_RecycleCS->SetUAV(m_Context, "enemyPush", m_EnemyPushUAV.Get());
            m_RecycleCS->SetUAV(m_Context, "enemyPoison", m_EnemyPoisonUAV.Get());
            // ダメージの数字：前の hp と溜めを忘れさせる印（前の敵との hp の差を数字に出さない）
            m_RecycleCS->SetUAV(m_Context, "prevHp", m_EnemyPrevHpUAV.Get());
            m_RecycleCS->BindUAVs(m_Context);

            m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

            m_RecycleCS->UnbindSRVs(m_Context);
            m_RecycleCS->UnbindUAVs(m_Context);
        }
    }

    m_PendingEnemies.clear();
    m_PendingRecycles.clear();
    m_PendingProjectiles.clear();
    m_PendingProjExtra.clear();
}


// ============================================================
// 固定ステップ1回ぶんの CS 群
// 順序はここが全て:
//   0 counter ゼロクリア → 0b 空間ハッシュ → 1 敵AI → 2 敵積分 → 2b 重なり解消 → 3 弾積分 → 3b 弾の帯の記録
//   → 4 命中(+オーブ落下) → 4b 誘発の出来事 → 4c 範囲の tick / ダメージ → 5 照準 → 6 接触 → 7 オーブ吸引・取得
// ============================================================
void SwarmSystem::DispatchStep()
{
    ++m_TotalSteps;

    // ---- 0) counter を 0 に ----
    if (m_ClearCountersCS)
    {
        m_ClearCountersCS->Bind(m_Context);
        m_ClearCountersCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_ClearCountersCS->BindUAVs(m_Context);
        m_Context->Dispatch(1, 1, 1);
        m_ClearCountersCS->UnbindUAVs(m_Context);
    }
    // ---- 0b) 雑魚の空間ハッシュ: 活きスロットをマスの桶へ ----
    // AI の分離と 2b) の押し出しが 3x3 マスだけ見るための表。
    // count を 0 に戻してから詰める（UAV clear は CPU から）
    const bool hashReady = m_EnemyBinCS && m_CellCountUAV && m_CellItemsUAV;
    if (hashReady)
    {
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_CellCountUAV.Get(), zero);

        m_EnemyBinCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyBinCS->Bind(m_Context);
        m_EnemyBinCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyBinCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_EnemyBinCS->SetUAV(m_Context, "cellCount", m_CellCountUAV.Get());
        m_EnemyBinCS->SetUAV(m_Context, "cellItems", m_CellItemsUAV.Get());
        m_EnemyBinCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
        m_EnemyBinCS->UnbindSRVs(m_Context);
        m_EnemyBinCS->UnbindUAVs(m_Context);
    }

    // ---- 1) 雑魚 AI: 速度を決める ----
    // position は読むだけ。全スレッドが同じスナップショットを見るために
    // 積分は次の dispatch に分けてある
    if (m_EnemyAICS)
    {
        m_EnemyAICS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyAICS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_EnemyAICS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // エリートの体格（プレイヤーの手前で止まる距離）
        m_EnemyAICS->Bind(m_Context);
        m_EnemyAICS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "terrain", MobTerrainSRV());   // 雑魚用（登れる障害物は通れる）
        m_EnemyAICS->SetSRV(m_Context, "cellCount", m_CellCountSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "cellItems", m_CellItemsSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "flowField", m_FlowSRV.Get());   // 巡路の向き表
        m_EnemyAICS->SetUAV(m_Context, "enemyExtra", m_EnemyExtraUAV.Get());   // 点火した自爆兵は止まる・突撃兵の段階を進める
        m_EnemyAICS->SetSRV(m_Context, "terrainHeight", MobHeightSRV());    // 崖・障害物の上面（登る向きでなければ壁と同じく止める）
        m_EnemyAICS->SetSRV(m_Context, "groundHeight", m_HeightSRV.Get());  // 障害物を含まない地面（砂旋が崖で消える）
        m_EnemyAICS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyAICS->SetUAV(m_Context, "enemySlow", m_EnemySlowUAV.Get());   // 毒の池の減速・凍結（数え下げる）
        m_EnemyAICS->SetUAV(m_Context, "castEvents", m_SplitUAV.Get());     // 砂嵐の術師の詠唱 → 分裂の環（CPU が砂旋を湧かせる）
        m_EnemyAICS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_EnemyAICS->UnbindSRVs(m_Context);
        m_EnemyAICS->UnbindUAVs(m_Context);
    }

    // ---- 2) 雑魚の積分 ----
    if (m_EnemyMoveCS)
    {
        // トゲ玉の転がり：体格 1 の玉が 1m で回る角度（見た目の半径・倍率は spikeBallLook）
        m_CachedBomberCB.spikeBallRollPerMeter = spikeBallLook.rollRate / (std::max)(spikeBallLook.radius, 1e-3f);
        m_EnemyMoveCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyMoveCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_EnemyMoveCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // 種類の体格（壁から体半径を離す）
        m_EnemyMoveCS->Bind(m_Context);
        m_EnemyMoveCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyMoveCS->SetSRV(m_Context, "terrainHeight", MobHeightSRV());   // 斜面・障害物の上面の高さ（雑魚用）
        m_EnemyMoveCS->SetSRV(m_Context, "terrain", MobTerrainSRV());       // 通行マス（壁への滑り・壁からの脱出。雑魚用）
        m_EnemyMoveCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 種類（幽霊は壁を素通り）
        m_EnemyMoveCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyMoveCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_EnemyMoveCS->SetUAV(m_Context, "enemyPush", m_EnemyPushUAV.Get());   // 突風の吹き飛ばし（積分に足して減らす。AI の速度の平滑化を通さない）
        m_EnemyMoveCS->SetUAV(m_Context, "enemyRoll", m_EnemyRollUAV.Get());   // トゲ玉の向き（実際に進んだ分だけ転がす）
        m_EnemyMoveCS->SetUAV(m_Context, "closeTarget", m_CloseTargetUAV.Get());   // 優先度なしの一番近い敵の競争
        m_EnemyMoveCS->SetUAV(m_Context, "enemyClimb", m_EnemyClimbUAV.Get());     // 壁を登っている状態
        m_EnemyMoveCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_EnemyMoveCS->UnbindSRVs(m_Context);
        m_EnemyMoveCS->UnbindUAVs(m_Context);
    }

    // ---- 2b) 重なり解消: 半径 2 個分より近い同士を位置で押し離す ----
    // 分離は速度項なので前列が止まると後列が突っ込む。ここで位置の拘束として畳む
    if (hashReady && m_EnemyPushCS)
    {
        m_EnemyPushCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyPushCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_EnemyPushCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // 種類の体格（壁から体半径を離す）
        m_EnemyPushCS->Bind(m_Context);
        m_EnemyPushCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "terrain", MobTerrainSRV());   // 雑魚用
        m_EnemyPushCS->SetSRV(m_Context, "cellCount", m_CellCountSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "cellItems", m_CellItemsSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "terrainHeight", MobHeightSRV());
        m_EnemyPushCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 種類（幽霊は壁・崖を無視）
        m_EnemyPushCS->SetSRV(m_Context, "enemyClimb", m_EnemyClimbSRV.Get());   // 壁を登っている雑魚は押し合いで動かさない
        m_EnemyPushCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyPushCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
        m_EnemyPushCS->UnbindSRVs(m_Context);
        m_EnemyPushCS->UnbindUAVs(m_Context);
    }
    // ---- 3) 投射物の積分 ----
    if (m_ProjMoveCS)
    {
        m_ProjMoveCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        uint32_t detonateCB[4] = { m_DetonateArea, 0u, 0u, 0u };   // 一斉起爆
        m_ProjMoveCS->WriteBuffer(m_Context, 1, detonateCB);
        m_DetonateArea = 0;
        m_ProjMoveCS->Bind(m_Context);
        m_ProjMoveCS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
        // 曲線・追尾用。雑魚の位置は 2) で確定済み（UAV は外してある）
        m_ProjMoveCS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 潜っている砂潜りは狙わない
        m_ProjMoveCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 斜面に潜ったら命中
        m_ProjMoveCS->SetUAV(m_Context, "paths", m_PathUAV.Get());
        // 寿命切れ・壁で範囲を出すプロファイル用
        m_ProjMoveCS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "projBoost", m_ProjBoostSRV.Get());   // 出す範囲の威力 / 持続の倍率
        m_ProjMoveCS->SetSRV(m_Context, "projTags", m_ProjTagSRV.Get());     // 吸血の印（tag の最上位 bit）
        m_ProjMoveCS->SetUAV(m_Context, "areaDirs", m_AreaDirUAV.Get());      // 出した範囲に飛んでいた向きを残す（液溜まりの飛び散る向き）
        m_ProjMoveCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
        m_ProjMoveCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
        m_ProjMoveCS->SetUAV(m_Context, "projectiles", m_ProjUAV.Get());
        m_ProjMoveCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_ProjMoveCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_ProjMoveCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);
        m_ProjMoveCS->UnbindSRVs(m_Context);
        m_ProjMoveCS->UnbindUAVs(m_Context);
    }
    // ---- 3b) 弾の帯：進んだ位置を槽ごとの環へ（Trail entry）----
    DispatchProjTrails();

    // ---- 4) 命中: 弾 × 雑魚 ----
   // 位置は今の子ステップで確定済み。hp は固定小数なので InterlockedAdd
    if (m_HitCS)
    {
        m_HitCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_HitCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_HitCS->WriteBuffer(m_Context, 2, &m_CachedOrbCB);
        m_HitCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // エリートの体格・経験値
        m_HitCS->Bind(m_Context);
        m_HitCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
        m_HitCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
        // 命中した場所に範囲を出すプロファイル用（UAV はこれで 8 本。D3D11.0 の上限）
        m_HitCS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
        m_HitCS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());
        m_HitCS->SetSRV(m_Context, "projBoost", m_ProjBoostSRV.Get());   // SRV なので UAV の上限には響かない
        m_HitCS->SetSRV(m_Context, "projTags", m_ProjTagSRV.Get());     // 吸血の印（tag の最上位 bit）
        // 空間ハッシュ（0b で詰めた物）から当たる候補を引く。作れていなければ null = 全ての槽を見る
        m_HitCS->SetSRV(m_Context, "cellCount", hashReady ? m_CellCountSRV.Get() : nullptr);
        m_HitCS->SetSRV(m_Context, "cellItems", hashReady ? m_CellItemsSRV.Get() : nullptr);
        m_HitCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
        m_HitCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
        m_HitCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_HitCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_HitCS->SetUAV(m_Context, "enemyStates", m_EnemyStateUAV.Get());
        m_HitCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_HitCS->SetUAV(m_Context, "orbs", m_OrbUAV.Get());
        m_HitCS->SetUAV(m_Context, "orbStates", m_OrbStateUAV.Get());
        m_HitCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);

        m_HitCS->UnbindSRVs(m_Context);
        m_HitCS->UnbindUAVs(m_Context);
    }

    // ---- 4b) 誘発: タグ付きの弾がこのステップで消えたら、その場所を環へ（命中の直後。弾の消え方 3 通りをまとめて拾う）----
    if (m_ProjEndCS)
    {
        m_ProjEndCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_ProjEndCS->Bind(m_Context);
        m_ProjEndCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
        m_ProjEndCS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
        m_ProjEndCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());
        m_ProjEndCS->SetUAV(m_Context, "projTags", m_ProjTagUAV.Get());
        m_ProjEndCS->SetUAV(m_Context, "triggerEvents", m_TriggerUAV.Get());
        m_ProjEndCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);
        m_ProjEndCS->UnbindSRVs(m_Context);
        m_ProjEndCS->UnbindUAVs(m_Context);
    }

    // ---- 4c) 範囲攻撃: 時計を進める → tick した範囲の中の雑魚へダメージ ----
    // 命中の直後に置く：弾の命中で生まれた爆発が、そのステップのうちに炸裂する
    if (m_AreaTickCS && m_AreaDamageCS)
    {
        m_AreaTickCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_AreaTickCS->WriteBuffer(m_Context, 3, &m_CachedBeamCB);   // 光線の起点 / 終点
        m_AreaTickCS->Bind(m_Context);
        m_AreaTickCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
        m_AreaTickCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
        m_AreaTickCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_AreaTickCS->SetUAV(m_Context, "areaEnds", m_AreaEndUAV.Get());
        m_AreaTickCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxAreas + 255) / 256, 1, 1);
        m_AreaTickCS->UnbindUAVs(m_Context);

        // tick した範囲が 1 つも無いステップは、各スレッドが counter を 1 回読んで帰る
        m_FireClock += m_CachedFrameCB.step;
        m_CachedBomberCB.fireClock = m_FireClock;
        m_AreaDamageCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_AreaDamageCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_AreaDamageCS->WriteBuffer(m_Context, 2, &m_CachedOrbCB);
        m_AreaDamageCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // エリートの体格・経験値、燃えている草
        m_AreaDamageCS->Bind(m_Context);
        m_AreaDamageCS->SetSRV(m_Context, "g_FireMap", m_FireMapSRV.Get());
        m_AreaDamageCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
        m_AreaDamageCS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
        m_AreaDamageCS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
        m_AreaDamageCS->SetSRV(m_Context, "areaEnds", m_AreaEndSRV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "enemyStates", m_EnemyStateUAV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "orbs", m_OrbUAV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "orbStates", m_OrbStateUAV.Get());
        m_AreaDamageCS->SetUAV(m_Context, "enemySlow", m_EnemySlowUAV.Get());   // 減速の範囲が tick したら書く
        m_AreaDamageCS->SetUAV(m_Context, "enemyPush", m_EnemyPushUAV.Get());   // 吹き飛ばす範囲（突風）が tick したら書く
        m_AreaDamageCS->SetUAV(m_Context, "enemyPoison", m_EnemyPoisonUAV.Get()); // 毒素：範囲の tick で盛り、毎ステップ削る（毒沼）
        m_AreaDamageCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
        m_AreaDamageCS->UnbindSRVs(m_Context);
        m_AreaDamageCS->UnbindUAVs(m_Context);
    }
    // ---- 5) 照準: 最近傍の key → 位置/速度/距離 ----
    if (m_AimResolveCS)
    {
        m_AimResolveCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_AimResolveCS->Bind(m_Context);
        m_AimResolveCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_AimResolveCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_AimResolveCS->SetUAV(m_Context, "closeTarget", m_CloseTargetUAV.Get());   // 優先度なしの一番近い敵の位置
        m_AimResolveCS->BindUAVs(m_Context);
        m_Context->Dispatch(1, 1, 1);
        m_AimResolveCS->UnbindSRVs(m_Context);
        m_AimResolveCS->UnbindUAVs(m_Context);
    }
    // ---- 6) 接触: 雑魚 × プレイヤー ----
   // ダメージは counter に固定小数で累積。無敵時間の判定は CPU のステートマシン。
   // 自爆兵はここで点火・導火線・爆発（スロットを DEAD にして見た目の範囲を出す）
    if (m_ContactCS)
    {
        m_ContactCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_ContactCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_ContactCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);
        m_ContactCS->Bind(m_Context);
        m_ContactCS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());
        m_ContactCS->SetSRV(m_Context, "enemySlow", m_EnemySlowSRV.Get());   // 凍った敵は殴らない
        m_ContactCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_ContactCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_ContactCS->SetUAV(m_Context, "enemyStates", m_EnemyStateUAV.Get());
        m_ContactCS->SetUAV(m_Context, "enemyExtra", m_EnemyExtraUAV.Get());
        m_ContactCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
        m_ContactCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
        m_ContactCS->SetUAV(m_Context, "playerHits", m_PlayerHitUAV.Get());   // 打撃の向き（ノックバック）
        m_ContactCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_ContactCS->UnbindSRVs(m_Context);
        m_ContactCS->UnbindUAVs(m_Context);
    }
    // ---- 7) 経験値オーブ: 吸引・取得 ----
   // HitCS がこのステップで落としたオーブも含めて動かす。
   // 取得分は counter に固定小数で累積（永久累積、CPU が差分）
    if (m_OrbCS)
    {
        m_OrbCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        // 磁石: 効いている間は吸い寄せの半径を場全体に（一度吸われた球は離さないので短い間で足りる）
        Swarm::OrbCB orbCB = m_CachedOrbCB;
        if (m_MagnetTimer > 0.0f) orbCB.attractRadius = 1.0e4f;
        m_OrbCS->WriteBuffer(m_Context, 2, &orbCB);
        m_OrbCS->Bind(m_Context);
        m_OrbCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 台地の上では高く浮かぶ
        m_OrbCS->SetUAV(m_Context, "orbs", m_OrbUAV.Get());
        m_OrbCS->SetUAV(m_Context, "orbStates", m_OrbStateUAV.Get());
        m_OrbCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_OrbCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxOrbs + 255) / 256, 1, 1);

        m_OrbCS->UnbindSRVs(m_Context);
        m_OrbCS->UnbindUAVs(m_Context);
    }

}
