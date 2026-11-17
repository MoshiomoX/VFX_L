// ============================================================
// SwarmSystemDispatch.cpp
// 固定ステップ以外の dispatch：パーティクル発射・Sprite・液面の追跡・死骸・ダメージ数字・点光源の収集。
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
#include "World/WindGrid.h"
#include "VFX_Editor/VFXSpriteRenderer.h"   // VFXSpriteCameraCB
#include "VFX_Editor/VFXLiquidRenderer.h"   // 液溜まりの CameraCB / FrameCB（CPU の経路と同じ並び）

using namespace DirectX::SimpleMath;
using Microsoft::WRL::ComPtr;

// ============================================================
// 弾から粒子を発射（Flush の最後、粒子の Flush より前）
// 弾の位置は今フレームの子ステップで確定済み。
// 粒子システムの particles / deadList を借りて直接書く
// ============================================================
void SwarmSystem::DispatchEmit(float dt, float totalTime)
{
    // 粒子の発射は粒子システム・発射 CS・VFX の表が揃っている時だけ。風源の表と範囲の連番画像はそれと関係なく毎フレーム回す
    const bool canEmit = m_Particles && m_EmitCS && m_VFX.GetRecipeSRV() && m_VFX.GetEmitterSRV();

    // b0 = GlobalCB（ParticleCommon の物）。dt と seed だけ要る
    GlobalCB gcb = {};
    gcb.deltaTime = dt;
    gcb.totalTime = totalTime;
    gcb.baseSeed = (uint32_t)(totalTime * 1000.0f) ^ 0x5bd1e995u;
    gcb.emitterCount = 0;

    if (canEmit)
    {
        // 予約を 0 に、空き数を最新に
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_EmitBudgetUAV.Get(), zero);
        m_Particles->RefreshDeadCount(m_Context);

        // WriteBuffer の index はレジスタ番号。
        // この CS では SwarmFrameCB を b2 に逃がしてある（b0/b1 は ParticleCommon）
        m_EmitCS->WriteBuffer(m_Context, 0, &gcb);
        m_EmitCS->WriteBuffer(m_Context, 2, &m_CachedFrameCB);
        m_EmitCS->WriteBuffer(m_Context, 3, m_CarryColors);   // 出口魔法の弾の色
        m_EmitCS->Bind(m_Context);
        m_EmitCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
        m_EmitCS->SetSRV(m_Context, "projTags", m_ProjTagSRV.Get());
        m_EmitCS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
        m_EmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
        m_EmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
        m_EmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
        m_EmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
        m_EmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
        m_EmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
        m_EmitCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);

        m_EmitCS->UnbindSRVs(m_Context);
        m_EmitCS->UnbindUAVs(m_Context);

        // ---- 範囲（GPU が弾の命中で出した物）からも発射する ----
        // 同じ発射コード。空きの予約（emitBudget）は上の弾の分に続けて積む
        if (m_AreaEmitCS)
        {
            m_AreaEmitCS->WriteBuffer(m_Context, 0, &gcb);
            m_AreaEmitCS->WriteBuffer(m_Context, 2, &m_CachedFrameCB);
            m_AreaEmitCS->Bind(m_Context);
            m_AreaEmitCS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
            m_AreaEmitCS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
            m_AreaEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
            m_AreaEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
            m_AreaEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
            m_AreaEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
            m_AreaEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
            m_AreaEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
            m_AreaEmitCS->BindUAVs(m_Context);

            m_Context->Dispatch((Swarm::kMaxAreas + 255) / 256, 1, 1);

            m_AreaEmitCS->UnbindSRVs(m_Context);
            m_AreaEmitCS->UnbindUAVs(m_Context);
        }

        // ---- 燃えている草（GrassFireField）。予約は上に続けて積む ----
        DispatchFireEmit(dt, totalTime);
        // ---- 砂旋の足元の砂埃 ----
        DispatchTornadoEmit(dt, totalTime);
    }
    // ---- 局所の風源の表（砂旋 + CPU の風源 → 草・落ち葉・粒子）----
    DispatchWindSources(dt);
    CopyWindSourcesForCPU();   // 出来た表を CPU へ写す（読むのは 2〜3 フレーム後）
    // 風の格子：今作った風源の表をモーターにして 1 歩（草・落ち葉・粒子などはこの後で読む）
    if (WindGrid* grid = WindGrid::Current()) grid->Step(m_WindSourceSRV.Get(), dt);

    // ---- 吸い寄せ中の経験値オーブの尾 ----
    // 弾・範囲の後に回す：空きが足りない時に削られるのは見た目だけのこちら
    if (canEmit && m_OrbEmitCS && orbLook.trail && m_OrbTrailVfx != 0)
    {
        OrbEmitCB ecb = { m_OrbTrailVfx, orbLook.trailMinSpeed, {} };
        m_OrbEmitCS->WriteBuffer(m_Context, 0, &gcb);
        m_OrbEmitCS->WriteBuffer(m_Context, 2, &m_CachedFrameCB);
        m_OrbEmitCS->WriteBuffer(m_Context, 3, &ecb);
        m_OrbEmitCS->Bind(m_Context);
        m_OrbEmitCS->SetSRV(m_Context, "orbs", m_OrbSRV.Get());
        m_OrbEmitCS->SetSRV(m_Context, "orbStates", m_OrbStateSRV.Get());
        m_OrbEmitCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
        m_OrbEmitCS->SetSRV(m_Context, "emitters", m_VFX.GetEmitterSRV());
        m_OrbEmitCS->SetSRV(m_Context, "deadCount", m_Particles->GetDeadCountSRV());
        m_OrbEmitCS->SetUAV(m_Context, "particles", m_Particles->GetParticleUAV());
        m_OrbEmitCS->SetUAV(m_Context, "deadList", m_Particles->GetDeadListUAV(), (UINT)-1);
        m_OrbEmitCS->SetUAV(m_Context, "emitBudget", m_EmitBudgetUAV.Get());
        m_OrbEmitCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxOrbs + 255) / 256, 1, 1);

        m_OrbEmitCS->UnbindSRVs(m_Context);
        m_OrbEmitCS->UnbindUAVs(m_Context);
    }

    DispatchSprites(dt);
}

// ============================================================
// 範囲の連番画像（Sprite entry）
// 1 回目：再生中の物を進め、寿命が来た物を消す（環の全枠）
// 2 回目：範囲の槽を見て、新しく生まれた範囲のレシピの Sprite を始める
// 範囲の数え下げ（timeLeft）は子ステップで確定済み。粒子の発射と同じく 1 フレーム 1 回
// ============================================================
void SwarmSystem::DispatchSprites(float dt)
{
    if (!m_SpriteCS || m_VFX.GetSpriteDefCount() == 0) return;

    struct { float dt; uint32_t mode; uint32_t poolSize; uint32_t areaCount; } cb =
        { dt, 0u, kMaxSprites, Swarm::kMaxAreas };

    for (uint32_t mode = 0; mode < 2; ++mode)
    {
        cb.mode = mode;
        m_SpriteCS->WriteBuffer(m_Context, 3, &cb);
        m_SpriteCS->Bind(m_Context);
        m_SpriteCS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
        m_SpriteCS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
        m_SpriteCS->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
        m_SpriteCS->SetSRV(m_Context, "spriteDefs", m_VFX.GetSpriteDefSRV());
        m_SpriteCS->SetUAV(m_Context, "sprites", m_SpriteUAV.Get());
        m_SpriteCS->SetUAV(m_Context, "areaSeen", m_AreaSeenUAV.Get());
        m_SpriteCS->SetUAV(m_Context, "spriteHead", m_SpriteHeadUAV.Get());
        m_SpriteCS->BindUAVs(m_Context);

        const uint32_t n = (mode == 0) ? kMaxSprites : Swarm::kMaxAreas;
        m_Context->Dispatch((n + 255) / 256, 1, 1);

        m_SpriteCS->UnbindSRVs(m_Context);
        m_SpriteCS->UnbindUAVs(m_Context);
    }
}

// ============================================================
// 液溜まりの追跡（Liquid entry）
// 範囲の槽 1 つに 1 スレッド。前フレームに空だった / 残り時間が増えた槽 = 新しい範囲。
// その時に、出した物が残した向き（areaDirs、ProjMoveCS が書く）を 1 回だけ受け取り、
// 最初に見た時の残り時間を覚える（年齢 = それ - 今の残り時間）。時計は要らない
// ============================================================
void SwarmSystem::DispatchAreaTrack()
{
    if (!m_AreaTrackCS) return;

    m_AreaTrackCS->Bind(m_Context);
    m_AreaTrackCS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
    m_AreaTrackCS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
    m_AreaTrackCS->SetUAV(m_Context, "areaDirs", m_AreaDirUAV.Get());
    m_AreaTrackCS->SetUAV(m_Context, "areaTrack", m_AreaTrackUAV.Get());
    m_AreaTrackCS->SetUAV(m_Context, "areaBirths", m_AreaBirthUAV.Get());   // 生まれた範囲の数（揺らす範囲・レシピ毎。新しい範囲を見つけた所で数える）
    m_AreaTrackCS->BindUAVs(m_Context);

    m_Context->Dispatch((Swarm::kMaxAreas + 63) / 64, 1, 1);

    m_AreaTrackCS->UnbindSRVs(m_Context);
    m_AreaTrackCS->UnbindUAVs(m_Context);
}

// ============================================================
// 死んだ敵の砕け散り
// 1) 敵の槽 1 つに 1 スレッド：前フレームは生きていて今は死んでいる槽 = 死んだ。最後の位置・向き・種類を
//    尸の環へ書き、足元に土煙の範囲を出す（切っている間も「前の生死」だけは追う）
// 2) 環の枠 1 つに 1 スレッド：まだ砕けている物を見た目毎の一覧へ → 各 submesh の InstanceCount
// ============================================================
void SwarmSystem::DispatchCorpses()
{
    if (!m_CorpseTrackCS || !m_CorpseListCS) return;

    CorpseTrackCB tcb = { m_AnimClock, corpse.enabled ? 1u : 0u, corpse.deathArea, 0u };
    m_CorpseTrackCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);   // プレイヤーの位置（飛ばす向き）・槽の数
    m_CorpseTrackCS->WriteBuffer(m_Context, 4, &tcb);
    m_CorpseTrackCS->WriteBuffer(m_Context, 5, &m_CachedKindCB);   // ガーゴイルの飛ぶ高さ（空中で死んでも地面から砕ける）
    m_CorpseTrackCS->Bind(m_Context);
    m_CorpseTrackCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_CorpseTrackCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_CorpseTrackCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_CorpseTrackCS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "prevAlive", m_EnemyPrevAliveUAV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "corpses", m_CorpseUAV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "corpseHead", m_CorpseHeadUAV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "areas", m_AreaUAV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "areaStates", m_AreaStateUAV.Get());
    m_CorpseTrackCS->SetUAV(m_Context, "enemyEvents", m_EnemyEventUAV.Get());   // スプリッター・呪いの鎧（1 回目）が死んだ所
    m_CorpseTrackCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_CorpseTrackCS->UnbindSRVs(m_Context);
    m_CorpseTrackCS->UnbindUAVs(m_Context);

    if (m_CorpseDrawArgs[0].empty()) return;   // 敵の網がまだ無い
    CorpseListCB lcb = { m_AnimClock, corpse.life, { 0.0f, 0.0f } };
    m_CorpseListCS->WriteBuffer(m_Context, 4, &lcb);
    m_CorpseListCS->Bind(m_Context);
    m_CorpseListCS->SetSRV(m_Context, "corpses", m_CorpseSRV.Get());
    m_CorpseListCS->SetUAV(m_Context, "mobCorpses", m_CorpseListUAV[Swarm::kDrawListMob].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "bomberCorpses", m_CorpseListUAV[Swarm::kDrawListBomber].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "ghostCorpses", m_CorpseListUAV[Swarm::kDrawListGhost].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "splitterCorpses", m_CorpseListUAV[Swarm::kDrawListSplitter].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "bruteCorpses", m_CorpseListUAV[Swarm::kDrawListBrute].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "chargerCorpses", m_CorpseListUAV[Swarm::kDrawListCharger].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "shieldCorpses", m_CorpseListUAV[Swarm::kDrawListShield].Get(), 0);
    m_CorpseListCS->SetUAV(m_Context, "burrowerCorpses", m_CorpseListUAV[Swarm::kDrawListBurrower].Get(), 0);
    m_CorpseListCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxCorpses + 63) / 64, 1, 1);
    m_CorpseListCS->UnbindSRVs(m_Context);
    m_CorpseListCS->UnbindUAVs(m_Context);
    DispatchCorpseList2(&lcb);   // Boss・砂嵐の術師・突撃爆弾兵・遺跡の敵（自分の皮で砕ける）
    DispatchCorpseList3(&lcb);   // 面の固定エリート

    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        for (auto& args : m_CorpseDrawArgs[k])
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_CorpseListUAV[k].Get());
    CopyCorpseDecoCounts();
}

// ============================================================
// ダメージの数字。槽毎に hp の減りを溜め、merge 秒で 1 件にして環へ。死んだ槽は残りを「倒した」で出す
// ============================================================
void SwarmSystem::DispatchDamageText(float dt)
{
    if (!m_DamageTextCS || !m_DamageTextBuffer) return;
    DamageTextCB cb = { dt, damageText.merge, damageText.enabled ? 1u : 0u, damageText.poisonMerge };
    m_DamageTextCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_DamageTextCS->WriteBuffer(m_Context, 2, &m_CachedAICB);       // 体の半径（頭の高さ）
    m_DamageTextCS->WriteBuffer(m_Context, 3, &m_CachedKindCB);   // 体格
    m_DamageTextCS->WriteBuffer(m_Context, 4, &cb);
    m_DamageTextCS->Bind(m_Context);
    m_DamageTextCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_DamageTextCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_DamageTextCS->SetSRV(m_Context, "enemyPoison", m_EnemyPoisonSRV.Get());
    m_DamageTextCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    m_DamageTextCS->SetSRV(m_Context, "enemyHitInfo", m_EnemyHitInfoSRV.Get());   // 数字の色・吸血の印
    m_DamageTextCS->SetUAV(m_Context, "prevHp", m_EnemyPrevHpUAV.Get());
    m_DamageTextCS->SetUAV(m_Context, "pending", m_DamagePendingUAV.Get());
    m_DamageTextCS->SetUAV(m_Context, "dmgEvents", m_DamageTextUAV.Get());
    if (m_LifestealUAV) m_DamageTextCS->SetUAV(m_Context, "lifesteal", m_LifestealUAV.Get());   // 吸血の印の敵の HP の減り
    m_DamageTextCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
    m_DamageTextCS->UnbindSRVs(m_Context);
    m_DamageTextCS->UnbindUAVs(m_Context);
}

// ============================================================
// 点光源の収集
// CPU 側のリストを先に上げてから、活きている弾と範囲の分を CS で追記する。
// 弾・範囲の位置は GPU にしか無いので、ここでやるしかない。
// Flush の後（弾の位置が今フレームの物になってから）、描画の前に呼ぶ
// ============================================================
void SwarmSystem::CollectLights()
{
    auto& lights = PointLightManager::Get();
    lights.Upload(m_Context);   // CPU 分 + 数。同フレーム 2 回目なら何もしない
    if (!m_LightCollectCS || !m_VFX.GetRecipeSRV() || !m_VFX.GetLightSRV()) return;

    auto run = [&](ComputeShader* cs, const char* srcName, const char* stateName,
        ID3D11ShaderResourceView* src, ID3D11ShaderResourceView* state, UINT count)
        {
            cs->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
            cs->Bind(m_Context);
            cs->SetSRV(m_Context, srcName, src);
            cs->SetSRV(m_Context, stateName, state);
            cs->SetSRV(m_Context, "recipes", m_VFX.GetRecipeSRV());
            cs->SetSRV(m_Context, "lightDefs", m_VFX.GetLightSRV());
            cs->SetUAV(m_Context, "outLights", lights.GetLightUAV());
            cs->SetUAV(m_Context, "outCount", lights.GetCountUAV());
            cs->BindUAVs(m_Context);
            m_Context->Dispatch((count + 255) / 256, 1, 1);
            cs->UnbindSRVs(m_Context);
            cs->UnbindUAVs(m_Context);
        };

    run(m_LightCollectCS.get(), "projectiles", "projStates",
        m_ProjSRV.Get(), m_ProjStateSRV.Get(), Swarm::kMaxProjectiles);
    if (m_AreaLightCollectCS)
        run(m_AreaLightCollectCS.get(), "areas", "areaStates",
            m_AreaSRV.Get(), m_AreaStateSRV.Get(), Swarm::kMaxAreas);
}
