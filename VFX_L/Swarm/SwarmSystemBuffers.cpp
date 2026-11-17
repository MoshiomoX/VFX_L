// ============================================================
// SwarmSystemBuffers.cpp
// GPU のバッファと view をまとめて作る（CreateBuffers）。
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
// バッファ生成
// ============================================================
bool SwarmSystem::CreateBuffers(ID3D11Device* device)
{
    // ---- 構造化バッファ（UAV + SRV 両方）----
    auto makeStructured = [&](UINT stride, UINT count,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11UnorderedAccessView>& uav,
        ComPtr<ID3D11ShaderResourceView>& srv,
        const char* name) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = stride * count;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = stride;

            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " buffer failed" << std::endl;
                return false;
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
            ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = count;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " UAV failed" << std::endl;
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = count;
            if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " SRV failed" << std::endl;
                return false;
            }
            return true;
        };

    if (!makeStructured(sizeof(Swarm::Enemy), Swarm::kMaxEnemies,
        m_EnemyBuffer, m_EnemyUAV, m_EnemySRV, "enemy")) return false;

    // 生成時の hp（HP バーの分母）。Enemy 本体に場所が無いので横に持つ
    if (!makeStructured(sizeof(uint32_t), Swarm::kMaxEnemies,
        m_EnemyMaxHpBuffer, m_EnemyMaxHpUAV, m_EnemyMaxHpSRV, "enemyMaxHp")) return false;

    // 種類と自爆兵の導火線（Swarm::EnemyExtra）。これも本体の横に持つ
    if (!makeStructured(sizeof(Swarm::EnemyExtra), Swarm::kMaxEnemies,
        m_EnemyExtraBuffer, m_EnemyExtraUAV, m_EnemyExtraSRV, "enemyExtra")) return false;

    // 敵の状態（float4 = 毒の池の減速の残り秒・強さ + 凍結の残り秒・長さ / 再凍結までの秒）。これも本体の横に持つ
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxEnemies,
        m_EnemyStatusBuffer, m_EnemyStatusUAV, m_EnemyStatusSRV, "enemyStatus")) return false;
    // 吹き飛ばし（突風。float4 = 速度 xz・残り秒）
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxEnemies,
        m_EnemyPushBuffer, m_EnemyPushUAV, m_EnemyPushSRV, "enemyPush")) return false;
    // 毒素（毒沼。float2 = 残りダメージ・毎秒の削り）
    if (!makeStructured(sizeof(float) * 2, Swarm::kMaxEnemies,
        m_EnemyPoisonBuffer, m_EnemyPoisonUAV, m_EnemyPoisonSRV, "enemyPoison")) return false;
    // 被弾の記録（Swarm::EnemyHitInfo）
    if (!makeStructured(sizeof(Swarm::EnemyHitInfo), Swarm::kMaxEnemies,
        m_EnemyHitInfoBuffer, m_EnemyHitInfoUAV, m_EnemyHitInfoSRV, "enemyHitInfo")) return false;
    // トゲ玉の向き（四元数）。0 = まだ転がっていない（下で 0 に消す）
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxEnemies,
        m_EnemyRollBuffer, m_EnemyRollUAV, m_EnemyRollSRV, "enemyRoll")) return false;
    // 壁を登っている状態（float4 = 登っている秒・登る先の上面・向き xz）。MoveCS が書き、PushCS が読む
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxEnemies,
        m_EnemyClimbBuffer, m_EnemyClimbUAV, m_EnemyClimbSRV, "enemyClimb")) return false;

    if (!makeStructured(sizeof(Swarm::Projectile), Swarm::kMaxProjectiles,
        m_ProjBuffer, m_ProjUAV, m_ProjSRV, "projectile")) return false;

    // ---- 投射物の運動：path は弾と同じ数、motion 表は CPU から書く ----
    if (!makeStructured(sizeof(Swarm::ProjPath), Swarm::kMaxProjectiles,
        m_PathBuffer, m_PathUAV, m_PathSRV, "projPath")) return false;
    // 連鎖のルーン・貫通の状態（Swarm::ProjChain）
    if (!makeStructured(sizeof(Swarm::ProjChain), Swarm::kMaxProjectiles,
        m_ProjChainBuffer, m_ProjChainUAV, m_ProjChainSRV, "projChain")) return false;
    {
        // 全行 0 = Straight。SetMotions が呼ばれなくても全弾が直進で動く
        std::vector<Swarm::Motion> zero(Swarm::kMaxMotions);
        for (auto& z : zero) z = Swarm::Motion{};

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Swarm::Motion) * Swarm::kMaxMotions;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(Swarm::Motion);

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = zero.data();
        if (FAILED(device->CreateBuffer(&bd, &init, &m_MotionBuffer)))
        {
            std::cout << "[Error] SwarmSystem: motion buffer failed" << std::endl;
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = Swarm::kMaxMotions;
        if (FAILED(device->CreateShaderResourceView(m_MotionBuffer.Get(), &sd, &m_MotionSRV)))
        {
            std::cout << "[Error] SwarmSystem: motion SRV failed" << std::endl;
            return false;
        }
    }

    if (!makeStructured(sizeof(Swarm::Orb), Swarm::kMaxOrbs,
        m_OrbBuffer, m_OrbUAV, m_OrbSRV, "orb")) return false;

    // ============================================================
    // 生死フラグ（typed buffer, R32_UINT）
    // structured ではなく typed。RWBuffer<uint> に対応するのはこちら。
    // MiscFlags を付けない（structured にすると RWBuffer<uint> と噛み合わない）
    // ============================================================
    auto makeState = [&](UINT count,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11UnorderedAccessView>& uav,
        ComPtr<ID3D11ShaderResourceView>& srv,
        const char* name) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = sizeof(uint32_t) * count;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " state buffer failed" << std::endl;
                return false;
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
            ud.Format = DXGI_FORMAT_R32_UINT;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = count;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " state UAV failed" << std::endl;
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_R32_UINT;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = count;
            if (FAILED(device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " state SRV failed" << std::endl;
                return false;
            }
            return true;
        };

    if (!makeState(Swarm::kMaxEnemies, m_EnemyStateBuffer, m_EnemyStateUAV, m_EnemyStateSRV, "enemy")) return false;
    if (!makeState(Swarm::kMaxProjectiles, m_ProjStateBuffer, m_ProjStateUAV, m_ProjStateSRV, "proj"))  return false;
    if (!makeState(Swarm::kMaxOrbs, m_OrbStateBuffer, m_OrbStateUAV, m_OrbStateSRV, "orb"))   return false;
    // 誘発タグ（生死と同じ R32_UINT の並行バッファ）
    if (!makeState(Swarm::kMaxProjectiles, m_ProjTagBuffer, m_ProjTagUAV, m_ProjTagSRV, "projTag")) return false;
    // 出す範囲への倍率（同じく R32_UINT。0 = 1 倍）
    if (!makeState(Swarm::kMaxProjectiles, m_ProjBoostBuffer, m_ProjBoostUAV, m_ProjBoostSRV, "projBoost")) return false;

    // ---- 範囲攻撃 ----
    if (!makeStructured(sizeof(Swarm::Area), Swarm::kMaxAreas,
        m_AreaBuffer, m_AreaUAV, m_AreaSRV, "area")) return false;
    // Liquid entry:範囲を出した物が飛んでいた向き（ProjMoveCS が書く）と、
    // 追跡（SwarmAreaTrackCS: 向き・最初に見た時の残り時間・前フレームの残り時間）
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxAreas,
        m_AreaDirBuffer, m_AreaDirUAV, m_AreaDirSRV, "areaDir")) return false;
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxAreas,
        m_AreaTrackBuffer, m_AreaTrackUAV, m_AreaTrackSRV, "areaTrack")) return false;
    if (!makeState(Swarm::kMaxAreas, m_AreaStateBuffer, m_AreaStateUAV, m_AreaStateSRV, "area")) return false;
    if (!makeStructured(sizeof(DirectX::SimpleMath::Vector4), Swarm::kMaxAreas,
        m_AreaEndBuffer, m_AreaEndUAV, m_AreaEndSRV, "areaEnd")) return false;

    // ---- 範囲の連番画像：再生中の環と、範囲の槽ごとの「前に見た timeLeft」----
    if (!makeStructured(sizeof(Swarm::SpriteInstance), kMaxSprites,
        m_SpriteBuffer, m_SpriteUAV, m_SpriteSRV, "sprite")) return false;
    if (!makeStructured(sizeof(uint32_t), Swarm::kMaxAreas,
        m_AreaSeenBuffer, m_AreaSeenUAV, m_AreaSeenSRV, "areaSeen")) return false;

    // ---- 弾の帯（Trail entry）：槽ごとの位置の環と状態 ----
    if (!makeStructured(sizeof(float) * 3, Swarm::kMaxProjectiles * kTrailPoints,
        m_ProjTrailPoints, m_ProjTrailPointsUAV, m_ProjTrailPointsSRV, "projTrailPoints")) return false;
    if (!makeStructured(sizeof(uint32_t), Swarm::kMaxProjectiles,
        m_ProjTrailState, m_ProjTrailStateUAV, m_ProjTrailStateSRV, "projTrailState")) return false;
    if (!makeStructured(sizeof(float), Swarm::kMaxProjectiles,
        m_ProjTrailAge, m_ProjTrailAgeUAV, m_ProjTrailAgeSRV, "projTrailAge")) return false;

    // ---- RAW UAV（counter と発射予約。両方とも InterlockedAdd 用）----
    auto makeRaw = [&](UINT bytes,
        ComPtr<ID3D11Buffer>& buf,
        ComPtr<ID3D11UnorderedAccessView>& uav,
        const char* name) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = bytes;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " raw buffer failed" << std::endl;
                return false;
            }

            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
            ud.Format = DXGI_FORMAT_R32_TYPELESS;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
            ud.Buffer.NumElements = bytes / 4;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav)))
            {
                std::cout << "[Error] SwarmSystem: " << name << " raw UAV failed" << std::endl;
                return false;
            }
            return true;
        };

    if (!makeRaw(sizeof(SwarmCounters), m_CounterBuffer, m_CounterUAV, "counter")) return false;
    if (!makeRaw(16, m_EmitBudget, m_EmitBudgetUAV, "emitBudget")) return false;
    if (!makeRaw(16, m_RecycleClaim, m_RecycleClaimUAV, "recycleClaim")) return false;
    if (!makeRaw(16, m_SpriteHead, m_SpriteHeadUAV, "spriteHead")) return false;
    // ---- 死んだ敵の砕け散り：槽毎の前フレームの生死、死体の環とその通し番号 ----
    if (!makeState(Swarm::kMaxEnemies, m_EnemyPrevAliveBuffer, m_EnemyPrevAliveUAV, m_EnemyPrevAliveSRV, "prevAlive")) return false;
    if (!makeStructured(sizeof(Swarm::Corpse), Swarm::kMaxCorpses,
        m_CorpseBuffer, m_CorpseUAV, m_CorpseSRV, "corpse")) return false;
    if (!makeRaw(16, m_CorpseHead, m_CorpseHeadUAV, "corpseHead")) return false;
    if (!makeRaw(sizeof(Swarm::BossInfo), m_BossInfoBuffer, m_BossInfoUAV, "bossInfo")) return false;
    // 局所の風源の表（砂旋 → 草・落ち葉・粒子）
    if (!makeStructured(sizeof(Swarm::WindSource), Swarm::kMaxWindSources,
        m_WindSourceBuffer, m_WindSourceUAV, m_WindSourceSRV, "windSources")) return false;
    if (!makeRaw(16, m_WindSourceCount, m_WindSourceCountUAV, "windSourceCount")) return false;
    if (!CreateWindSourceReadback(device)) return false;   // CPU のマント・転がり草が読む写し
    if (!CreateLifestealReadback(device)) return false;    // 吸血の累計
    if (!CreateNearestTargetReadback(device)) return false;  // 優先度なしの一番近い敵
    {
        // CPU の風源（突風）：毎フレーム Map で書く DYNAMIC の structured buffer
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Swarm::WindSource) * Swarm::kMaxWindSources;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(Swarm::WindSource);
        if (FAILED(m_Device->CreateBuffer(&bd, nullptr, &m_WindBurstBuffer))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_UNKNOWN;
        sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sd.Buffer.NumElements = Swarm::kMaxWindSources;
        if (FAILED(m_Device->CreateShaderResourceView(m_WindBurstBuffer.Get(), &sd, &m_WindBurstSRV))) return false;
    }
    {
        // Boss の様子のリードバック（counter と同じく 3 枚を輪転）
        if (!m_BossStaging.Create(device, sizeof(Swarm::BossInfo))) return false;
        m_BossInfo = {};
    }
    {
        // プレイヤーが受けた打撃の向き（ノックバック用）。永久に累積するので作った時に 1 度だけ 0 にする
        if (!makeRaw(sizeof(Swarm::PlayerHitInfo), m_PlayerHitBuffer, m_PlayerHitUAV, "playerHits")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_PlayerHitUAV.Get(), zero);
        if (!m_PlayerHitStaging.Create(device, sizeof(Swarm::PlayerHitInfo))) return false;
        m_LastPlayerHits = {};
        m_PendingPlayerHits = {};
    }
    {
        // 生まれた範囲の数（累計。[0] 揺らす範囲、[1 + レシピ] レシピ毎）。作った時に 1 度だけ 0 にする
        const UINT bytes = (UINT)(sizeof(uint32_t) * kAreaBirthSlots);
        if (!makeRaw(bytes, m_AreaBirthBuffer, m_AreaBirthUAV, "areaBirths")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_AreaBirthUAV.Get(), zero);
        if (!m_AreaBirthStaging.Create(device, bytes)) return false;
        m_LastAreaBirths = {};
        m_PendingShakes = 0;
        m_PendingAreaBirths = {};
    }
    {
        // 光線の標的（チャンネル毎 32B）。slot = 0xFFFFFFFF・serial 0 で始める（どの光線の答えでもない）
        const UINT bytes = sizeof(Swarm::BeamTarget) * Swarm::kMaxBeams;
        if (!makeRaw(bytes, m_BeamTargetBuffer, m_BeamTargetUAV, "beamTargets")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_BeamTargetUAV.Get(), zero);
        if (!m_BeamTargetStaging.Create(device, bytes)) return false;
        m_CachedBeamTargetCB = {};
        for (auto& t : m_BeamTargets) t = {};
    }
    {
        // 誘発の環（先頭 16B = 総数 + 16B × kMaxTriggerEvents）と、そのリードバック
        const UINT bytes = 16u + sizeof(Swarm::TriggerEvent) * Swarm::kMaxTriggerEvents;
        if (!makeRaw(bytes, m_TriggerBuffer, m_TriggerUAV, "trigger")) return false;

        if (!m_TriggerStaging.Create(device, bytes)) return false;
        m_TriggerRead = 0;
        m_TriggerEvents.clear();
    }
    {
        // 敵の出来事の環（先頭 16B = 総数 + 16B × kMaxEnemyEvents）と、そのリードバック
        const UINT bytes = 16u + sizeof(Swarm::EnemyEvent) * Swarm::kMaxEnemyEvents;
        if (!makeRaw(bytes, m_EnemyEventBuffer, m_EnemyEventUAV, "enemyEvents")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_EnemyEventUAV.Get(), zero);

        if (!m_EnemyEventStaging.Create(device, bytes)) return false;
        m_EnemyEventRead = 0;
        m_EnemyEvents.clear();
    }
    {
        // ダメージの数字：槽毎の前フレームの hp・溜め、環（先頭 16B = 総数 + 16B × kMaxDamageTexts）と staging
        if (!makeState(Swarm::kMaxEnemies, m_EnemyPrevHpBuffer, m_EnemyPrevHpUAV, m_EnemyPrevHpSRV, "prevHp")) return false;
        if (!makeStructured(sizeof(float) * 4, Swarm::kMaxEnemies,
            m_DamagePendingBuffer, m_DamagePendingUAV, m_DamagePendingSRV, "damagePending")) return false;
        const UINT bytes = 16u + sizeof(Swarm::DamageText) * Swarm::kMaxDamageTexts;
        if (!makeRaw(bytes, m_DamageTextBuffer, m_DamageTextUAV, "damageText")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_DamageTextUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EnemyPrevHpUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_DamagePendingUAV.Get(), zero);

        if (!m_DamageTextStaging.Create(device, bytes)) return false;
        m_DamageTextRead = 0;
        m_DamageTexts.clear();
    }
    {
        // 地面の警告の輪（CPU が毎フレーム書く。動的の StructuredBuffer）
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(WarnCircle) * kMaxWarnCircles;
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride = sizeof(WarnCircle);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_WarnCircleBuffer))) return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Format = DXGI_FORMAT_UNKNOWN;
        sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        sv.Buffer.NumElements = kMaxWarnCircles;
        if (FAILED(device->CreateShaderResourceView(m_WarnCircleBuffer.Get(), &sv, &m_WarnCircleSRV))) return false;
        m_WarnCircleCount = 0;
    }
    // ---- 定数バッファ ----
    // ※ComputeShader::WriteBuffer が反射から自前の CB を持つ場合は未使用。
    //   将来 VS 側で直接使うことを想定して残しておく
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(Swarm::FrameCB);
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_FrameCB))) return false;

        bd.ByteWidth = sizeof(Swarm::SpawnCB);
        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_SpawnCB))) return false;
    }

    // ---- 生成キュー（CPU が毎フレーム書くので DYNAMIC）----
    auto makeUpload = [&](UINT stride, UINT count,
        ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = stride * count;
            bd.Usage = D3D11_USAGE_DYNAMIC;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = stride;

            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = count;
            return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sd, &srv));
        };

    if (!makeUpload(sizeof(Swarm::EnemySpawn), Swarm::kMaxSpawnEnemyPerFrame,
        m_SpawnEnemyBuffer, m_SpawnEnemySRV)) return false;
    if (!makeUpload(sizeof(Swarm::Projectile), Swarm::kMaxSpawnProjPerFrame,
        m_SpawnProjBuffer, m_SpawnProjSRV)) return false;
    if (!makeUpload(sizeof(Swarm::SpawnProjExtra), Swarm::kMaxSpawnProjPerFrame,
        m_SpawnProjExtraBuffer, m_SpawnProjExtraSRV)) return false;
    if (!makeUpload(sizeof(Swarm::Area), Swarm::kMaxSpawnAreaPerFrame,
        m_SpawnAreaBuffer, m_SpawnAreaSRV)) return false;
    if (!makeUpload(sizeof(Swarm::AreaDef), Swarm::kMaxAreaDefs,
        m_AreaDefBuffer, m_AreaDefSRV)) return false;
    SetAreaDefs({});   // dynamic buffer の初期内容は未定義。全行を既定値で埋めておく

    // ============================================================
    // state を全部 DEAD にする
    // DEFAULT usage のバッファは初期内容が未定義。
    // ゴミが入っていると全スロットが alive に見えて一発も湧かない
    // （しかもエラーは出ない）
    // ============================================================
    {
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_EnemyStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_ProjStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_ProjTagUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_ProjBoostUAV.Get(), zero);   // 0 = 1 倍
        m_Context->ClearUnorderedAccessViewUint(m_AreaDirUAV.Get(), zero);     // w = 0: 新しい向きは無い
        m_Context->ClearUnorderedAccessViewUint(m_AreaTrackUAV.Get(), zero); // w = 0: 空
        m_Context->ClearUnorderedAccessViewUint(m_TriggerUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_OrbStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_AreaStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_CounterUAV.Get(), zero);
        {
            // 全部 0 だと「杖の狙い = 槽 0 の (0,0,0)、距離 0」に読め、最初の固定ステップより前のリードバックで
            // 杖が足元へ撃ってしまう。既定値（鍵 = 空、距離 = 1e30）を入れる
            const SwarmCounters init;
            m_Context->UpdateSubresource(m_CounterBuffer.Get(), 0, nullptr, &init, 0, 0);
        }
        m_Context->ClearUnorderedAccessViewUint(m_EmitBudgetUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EnemyMaxHpUAV.Get(), zero);   // 0 = VS 側で 1 扱い
        m_Context->ClearUnorderedAccessViewUint(m_EnemyExtraUAV.Get(), zero);   // 雑魚・未点火
        m_Context->ClearUnorderedAccessViewUint(m_EnemyStatusUAV.Get(), zero);    // 減速なし（0.0f）
        m_Context->ClearUnorderedAccessViewUint(m_EnemyPushUAV.Get(), zero);    // 吹き飛ばしなし
        m_Context->ClearUnorderedAccessViewUint(m_EnemyPoisonUAV.Get(), zero);  // 毒素なし
        m_Context->ClearUnorderedAccessViewUint(m_EnemyHitInfoUAV.Get(), zero); // 被弾の記録なし
        m_Context->ClearUnorderedAccessViewUint(m_ProjChainUAV.Get(), zero);    // 連鎖なし
        m_Context->ClearUnorderedAccessViewUint(m_EnemyRollUAV.Get(), zero);    // トゲ玉はまだ転がっていない
        m_Context->ClearUnorderedAccessViewUint(m_EnemyClimbUAV.Get(), zero);   // 誰も壁を登っていない
        m_Context->ClearUnorderedAccessViewUint(m_SpriteUAV.Get(), zero);       // alive = 0
        m_Context->ClearUnorderedAccessViewUint(m_SpriteHeadUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EnemyPrevAliveUAV.Get(), zero);   // 全員「前は死んでいた」
        m_Context->ClearUnorderedAccessViewUint(m_CorpseUAV.Get(), zero);           // birth 0 = 空き
        m_Context->ClearUnorderedAccessViewUint(m_CorpseHeadUAV.Get(), zero);
        const UINT unseen[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
        m_Context->ClearUnorderedAccessViewUint(m_AreaSeenUAV.Get(), unseen);
    }

    return true;
}
