// ============================================================
// SwarmSystem.cpp
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
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

    // 毒の池の減速（残り秒・強さ）。これも本体の横に持つ
    if (!makeStructured(sizeof(float) * 2, Swarm::kMaxEnemies,
        m_EnemySlowBuffer, m_EnemySlowUAV, m_EnemySlowSRV, "enemySlow")) return false;

    if (!makeStructured(sizeof(Swarm::Projectile), Swarm::kMaxProjectiles,
        m_ProjBuffer, m_ProjUAV, m_ProjSRV, "projectile")) return false;

    // ---- 投射物の運動：path は弾と同じ数、motion 表は CPU から書く ----
    if (!makeStructured(sizeof(Swarm::ProjPath), Swarm::kMaxProjectiles,
        m_PathBuffer, m_PathUAV, m_PathSRV, "projPath")) return false;
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
    // Liquid entry（2026-10-02）: 範囲を出した物が飛んでいた向き（ProjMoveCS が書く）と、
    // 追跡（SwarmLiquidTrackCS: 向き・最初に見た時の残り時間・前フレームの残り時間）
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxAreas,
        m_AreaDirBuffer, m_AreaDirUAV, m_AreaDirSRV, "areaDir")) return false;
    if (!makeStructured(sizeof(float) * 4, Swarm::kMaxAreas,
        m_LiquidTrackBuffer, m_LiquidTrackUAV, m_LiquidTrackSRV, "liquidTrack")) return false;
    if (!makeState(Swarm::kMaxAreas, m_AreaStateBuffer, m_AreaStateUAV, m_AreaStateSRV, "area")) return false;
    if (!makeStructured(sizeof(DirectX::SimpleMath::Vector4), Swarm::kMaxAreas,
        m_AreaEndBuffer, m_AreaEndUAV, m_AreaEndSRV, "areaEnd")) return false;

    // ---- 範囲の連番画像：再生中の環と、範囲の槽ごとの「前に見た timeLeft」----
    if (!makeStructured(sizeof(Swarm::SpriteInstance), kMaxSprites,
        m_SpriteBuffer, m_SpriteUAV, m_SpriteSRV, "sprite")) return false;
    if (!makeStructured(sizeof(uint32_t), Swarm::kMaxAreas,
        m_AreaSeenBuffer, m_AreaSeenUAV, m_AreaSeenSRV, "areaSeen")) return false;

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
    // ---- 死んだ敵の砕け散り：槽毎の前フレームの生死、尸の環とその通し番号 ----
    if (!makeState(Swarm::kMaxEnemies, m_EnemyPrevAliveBuffer, m_EnemyPrevAliveUAV, m_EnemyPrevAliveSRV, "prevAlive")) return false;
    if (!makeStructured(sizeof(Swarm::Corpse), Swarm::kMaxCorpses,
        m_CorpseBuffer, m_CorpseUAV, m_CorpseSRV, "corpse")) return false;
    if (!makeRaw(16, m_CorpseHead, m_CorpseHeadUAV, "corpseHead")) return false;
    if (!makeRaw(sizeof(Swarm::BossInfo), m_BossInfoBuffer, m_BossInfoUAV, "bossInfo")) return false;
    {
        // Boss の様子のリードバック（GPUReadback と同じく 3 枚を輪転）
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = sizeof(Swarm::BossInfo);
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kBossStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_BossStaging[i]))) return false;
            m_BossStagingFilled[i] = false;
        }
        m_BossStagingWrite = 0;
        m_BossInfo = {};
    }
    {
        // プレイヤーが受けた打撃の向き（ノックバック用）。永久に累積するので作った時に 1 度だけ 0 にする
        if (!makeRaw(sizeof(Swarm::PlayerHitInfo), m_PlayerHitBuffer, m_PlayerHitUAV, "playerHits")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_PlayerHitUAV.Get(), zero);
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = sizeof(Swarm::PlayerHitInfo);
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kBossStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_PlayerHitStaging[i]))) return false;
            m_PlayerHitStagingFilled[i] = false;
        }
        m_PlayerHitStagingWrite = 0;
        m_LastPlayerHits = {};
        m_PendingPlayerHits = {};
    }
    {
        // 生まれた範囲の数（累計。[0] 揺らす範囲、[1 + レシピ] レシピ毎）。作った時に 1 度だけ 0 にする
        const UINT bytes = (UINT)(sizeof(uint32_t) * kAreaBirthSlots);
        if (!makeRaw(bytes, m_AreaBirthBuffer, m_AreaBirthUAV, "areaBirths")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_AreaBirthUAV.Get(), zero);
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bytes;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kBossStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_AreaBirthStaging[i]))) return false;
            m_AreaBirthStagingFilled[i] = false;
        }
        m_AreaBirthStagingWrite = 0;
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
        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bytes;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kBossStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_BeamTargetStaging[i]))) return false;
            m_BeamTargetStagingFilled[i] = false;
        }
        m_BeamTargetStagingWrite = 0;
        m_CachedBeamTargetCB = {};
        for (auto& t : m_BeamTargets) t = {};
    }
    {
        // 誘発の環（先頭 16B = 総数 + 16B × kMaxTriggerEvents）と、そのリードバック
        const UINT bytes = 16u + sizeof(Swarm::TriggerEvent) * Swarm::kMaxTriggerEvents;
        if (!makeRaw(bytes, m_TriggerBuffer, m_TriggerUAV, "trigger")) return false;

        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bytes;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kTriggerStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_TriggerStaging[i]))) return false;
            m_TriggerStagingFilled[i] = false;
        }
        m_TriggerStagingWrite = 0;
        m_TriggerRead = 0;
        m_TriggerEvents.clear();
    }
    {
        // 分裂の環（先頭 16B = 総数 + 16B × kMaxSplitEvents。2026-10-03）と、そのリードバック
        const UINT bytes = 16u + sizeof(Swarm::SplitEvent) * Swarm::kMaxSplitEvents;
        if (!makeRaw(bytes, m_SplitBuffer, m_SplitUAV, "split")) return false;
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_SplitUAV.Get(), zero);

        D3D11_BUFFER_DESC sd = {};
        sd.ByteWidth = bytes;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (int i = 0; i < kTriggerStaging; ++i)
        {
            if (FAILED(device->CreateBuffer(&sd, nullptr, &m_SplitStaging[i]))) return false;
            m_SplitStagingFilled[i] = false;
        }
        m_SplitStagingWrite = 0;
        m_SplitRead = 0;
        m_SplitEvents.clear();
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

    if (!makeUpload(sizeof(Swarm::Enemy), Swarm::kMaxSpawnEnemyPerFrame,
        m_SpawnEnemyBuffer, m_SpawnEnemySRV)) return false;
    if (!makeUpload(sizeof(Swarm::Projectile), Swarm::kMaxSpawnProjPerFrame,
        m_SpawnProjBuffer, m_SpawnProjSRV)) return false;
    if (!makeUpload(sizeof(uint32_t) * 2, Swarm::kMaxSpawnProjPerFrame,
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
        m_Context->ClearUnorderedAccessViewUint(m_LiquidTrackUAV.Get(), zero); // w = 0: 空
        m_Context->ClearUnorderedAccessViewUint(m_TriggerUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_OrbStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_AreaStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_CounterUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EmitBudgetUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EnemyMaxHpUAV.Get(), zero);   // 0 = VS 側で 1 扱い
        m_Context->ClearUnorderedAccessViewUint(m_EnemyExtraUAV.Get(), zero);   // 雑魚・未点火
        m_Context->ClearUnorderedAccessViewUint(m_EnemySlowUAV.Get(), zero);    // 減速なし（0.0f）
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

// ============================================================
// 地形の格子表を上げる（起動時と Regenerate の時だけ）
// ============================================================
// ============================================================
// 通行マップだけを上げ直す（2026-10-03）。箱・門の柱は GPU の雑魚には格子でしか見えないので、
// シーンがその下のマスを塞いだ / 箱が開いて戻した時に呼ぶ
// ============================================================
void SwarmSystem::RefreshWalkable(const GridWorld& grid)
{
    const int w = grid.Width();
    const int d = grid.Depth();
    if (!m_Device || w <= 0 || d <= 0) return;

    std::vector<uint32_t> data((size_t)w * d);
    std::vector<uint8_t> walk((size_t)w * d);
    std::vector<float> hgt((size_t)w * d);
    for (int z = 0; z < d; ++z)
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t)z * w + x;
            walk[i] = grid.IsWalkable(x, z) ? 1 : 0;
            data[i] = walk[i];
            const auto c = grid.CellToWorld(x, z);
            hgt[i] = grid.SampleHeight(c.x, c.z);
        }

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(data.size() * sizeof(uint32_t));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(uint32_t);
    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data.data();
    ComPtr<ID3D11Buffer> buf;
    ComPtr<ID3D11ShaderResourceView> srv;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)data.size();
    if (FAILED(m_Device->CreateBuffer(&bd, &init, &buf))
        || FAILED(m_Device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
    {
        std::cout << "[Error] SwarmSystem: walkable refresh failed" << std::endl;
        return;
    }
    m_TerrainBuffer = buf;
    m_TerrainSRV = srv;

    WaitFlowJob();   // 古い通行マップで作っている途中の物は捨てる
    m_Flow.SetGrid(w, d, walk, hgt, grid.Heights(), GridWorld::kHeightSub);
    m_FlowWorker.SetGrid(w, d, walk, hgt, grid.Heights(), GridWorld::kHeightSub);
    m_FlowRequestX = m_FlowRequestZ = -1;
}

void SwarmSystem::UploadTerrain(const GridWorld& grid)
{
    const int w = grid.Width();
    const int d = grid.Depth();
    if (w <= 0 || d <= 0) return;

    // uint8_t → uint32_t へ展開する（HLSL の StructuredBuffer<uint> に合わせる）
    std::vector<uint32_t> data((size_t)w * d);
    for (int z = 0; z < d; ++z)
        for (int x = 0; x < w; ++x)
            data[(size_t)z * w + x] = grid.IsWalkable(x, z) ? 1u : 0u;

    // 作り直す（格子の寸法が変わる可能性があるので使い回さない）
    m_TerrainSRV.Reset();
    m_TerrainBuffer.Reset();

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(data.size() * sizeof(uint32_t));
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(uint32_t);

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = data.data();

    if (FAILED(m_Device->CreateBuffer(&bd, &init, &m_TerrainBuffer)))
    {
        std::cout << "[Error] SwarmSystem: terrain buffer failed" << std::endl;
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_UNKNOWN;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sd.Buffer.NumElements = (UINT)data.size();
    m_Device->CreateShaderResourceView(m_TerrainBuffer.Get(), &sd, &m_TerrainSRV);

    // ---- 高さ場（斜面。MoveCS が y を決めるのに引く）----
    // 解像度は GridWorld::kHeightSub = SwarmCommon.hlsli の SWARM_HEIGHT_SUB
    {
        m_HeightSRV.Reset();
        m_HeightBuffer.Reset();
        const auto& hs = grid.Heights();
        if (!hs.empty())
        {
            D3D11_BUFFER_DESC hb = {};
            hb.ByteWidth = (UINT)(hs.size() * sizeof(float));
            hb.Usage = D3D11_USAGE_IMMUTABLE;
            hb.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            hb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            hb.StructureByteStride = sizeof(float);
            D3D11_SUBRESOURCE_DATA hi = {};
            hi.pSysMem = hs.data();
            if (SUCCEEDED(m_Device->CreateBuffer(&hb, &hi, &m_HeightBuffer)))
            {
                D3D11_SHADER_RESOURCE_VIEW_DESC hd = {};
                hd.Format = DXGI_FORMAT_UNKNOWN;
                hd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                hd.Buffer.NumElements = (UINT)hs.size();
                m_Device->CreateShaderResourceView(m_HeightBuffer.Get(), &hd, &m_HeightSRV);
            }
            else
                std::cout << "[Error] SwarmSystem: height buffer failed" << std::endl;
        }
    }

    // ---- 空間ハッシュ（マス数 × 固定容量。格子の寸法が変わるのでここで作り直す）----
    {
        auto makeUintBuf = [&](UINT count, Microsoft::WRL::ComPtr<ID3D11Buffer>& buf,
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& uav,
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& srv) -> bool
            {
                buf.Reset(); uav.Reset(); srv.Reset();
                D3D11_BUFFER_DESC bd2 = {};
                bd2.ByteWidth = sizeof(uint32_t) * count;
                bd2.Usage = D3D11_USAGE_DEFAULT;
                bd2.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
                bd2.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
                bd2.StructureByteStride = sizeof(uint32_t);
                if (FAILED(m_Device->CreateBuffer(&bd2, nullptr, &buf))) return false;
                D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
                ud.Format = DXGI_FORMAT_UNKNOWN;
                ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                ud.Buffer.NumElements = count;
                if (FAILED(m_Device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;
                D3D11_SHADER_RESOURCE_VIEW_DESC sd2 = {};
                sd2.Format = DXGI_FORMAT_UNKNOWN;
                sd2.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
                sd2.Buffer.NumElements = count;
                return SUCCEEDED(m_Device->CreateShaderResourceView(buf.Get(), &sd2, &srv));
            };
        const UINT cells = (UINT)(w * d);
        if (!makeUintBuf(cells, m_CellCountBuffer, m_CellCountUAV, m_CellCountSRV) ||
            !makeUintBuf(cells * kBucketCap, m_CellItemsBuffer, m_CellItemsUAV, m_CellItemsSRV))
            std::cout << "[Error] SwarmSystem: spatial hash buffers failed" << std::endl;
    }

    // ---- 巡路: 通行マップとマス中心の高さを写し、向き表の buffer を作る ----
    {
        std::vector<uint8_t> walk((size_t)w * d);
        std::vector<float>   hgt((size_t)w * d);
        for (int z = 0; z < d; ++z)
            for (int x = 0; x < w; ++x)
            {
                walk[(size_t)z * w + x] = grid.IsWalkable(x, z) ? 1 : 0;
                const auto c = grid.CellToWorld(x, z);
                hgt[(size_t)z * w + x] = grid.SampleHeight(c.x, c.z);
            }
        WaitFlowJob();   // 古い格子で作っている途中の物は捨てる
        m_Flow.SetGrid(w, d, walk, hgt, grid.Heights(), GridWorld::kHeightSub);
        m_FlowWorker.SetGrid(w, d, walk, hgt, grid.Heights(), GridWorld::kHeightSub);
        m_FlowRequestX = m_FlowRequestZ = -1;

        m_FlowSRV.Reset();
        m_FlowBuffer.Reset();
        D3D11_BUFFER_DESC fb = {};
        fb.ByteWidth = (UINT)(sizeof(float) * 2 * w * d);
        fb.Usage = D3D11_USAGE_DYNAMIC;
        fb.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        fb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        fb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        fb.StructureByteStride = sizeof(float) * 2;
        if (SUCCEEDED(m_Device->CreateBuffer(&fb, nullptr, &m_FlowBuffer)))
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC fd = {};
            fd.Format = DXGI_FORMAT_UNKNOWN;
            fd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            fd.Buffer.NumElements = (UINT)(w * d);
            m_Device->CreateShaderResourceView(m_FlowBuffer.Get(), &fd, &m_FlowSRV);
        }
        else
            std::cout << "[Error] SwarmSystem: flow buffer failed" << std::endl;
    }

    // CS が格子を引くのに要る値も控えておく
    m_CachedFrameCB.gridOrigin = { grid.OriginX(), 0.0f, grid.OriginZ() };
    m_CachedFrameCB.cellSize = GridWorld::kCellSize;
    m_CachedFrameCB.gridW = (uint32_t)w;
    m_CachedFrameCB.gridD = (uint32_t)d;

    std::cout << "[Swarm] terrain uploaded: " << w << "x" << d
        << " (height " << grid.HeightW() << "x" << grid.HeightD() << ")" << std::endl;
}

// ============================================================
// VFX レシピ表の構築（起動時に1回）
// ============================================================
bool SwarmSystem::BuildVFXTable()
{
    const bool ok = m_VFX.Build(m_Device, m_Particles);
    // 吸い寄せ中のオーブの尾。json が無い・読めない時は 0（尾が出ないだけ）
    m_OrbTrailVfx = m_VFX.IndexOf(VFXId::ExpOrbTrail);
    return ok;
}

// ============================================================
// 生成依頼を溜める（実際に GPU へ入るのは Flush）
// ※state は本体に無い。スロットの生死は SpawnCS が
//   state buffer 側で InterlockedCompareExchange して立てる
// ============================================================
// ※種類は依頼の animIndex に入れて運ぶ（SpawnEnemyCS / RecycleCS が並行バッファへ移す）
void SwarmSystem::SpawnEnemy(const Vector3& pos, float hp, float moveSpeed, uint32_t kind)
{
    if (m_PendingEnemies.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;

    Swarm::Enemy e = {};
    e.position = pos;
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
    uint32_t triggerTag, bool spawnAtPos, float areaDamageMul, float areaDurationMul, uint32_t roll)
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

// 光線の起点 / 終点（次の固定ステップから AreaTickCS がカプセル型の範囲へ写す）
void SwarmSystem::SetBeam(uint32_t ch, const Vector3& start, const Vector3& end, float radius, bool active)
{
    if (ch >= Swarm::kMaxBeams) return;
    m_CachedBeamCB.start[ch] = { start.x, start.y, start.z, radius };
    m_CachedBeamCB.end[ch] = { end.x, end.y, end.z, active ? 1.0f : 0.0f };
}

// 光線の標的の依頼（次の Flush の DispatchBeamTargets で効く）
void SwarmSystem::SetBeamTarget(uint32_t ch, uint32_t cmd, const Vector3& origin, const Vector3& dir,
    float length, const Vector3& seek, uint32_t serial)
{
    if (ch >= Swarm::kMaxBeams) return;
    auto asF = [](uint32_t u) { float f; memcpy(&f, &u, sizeof(f)); return f; };
    m_CachedBeamTargetCB.origin[ch] = { origin.x, origin.y, origin.z, asF(cmd) };
    m_CachedBeamTargetCB.dir[ch] = { dir.x, dir.y, dir.z, length };
    m_CachedBeamTargetCB.seek[ch] = { seek.x, seek.y, seek.z, asF(serial) };
}

bool SwarmSystem::GetBeamTarget(uint32_t ch, uint32_t serial, Vector3& pos) const
{
    if (ch >= Swarm::kMaxBeams) return false;
    const Swarm::BeamTarget& t = m_BeamTargets[ch];
    if (t.serial != serial || t.valid == 0) return false;
    pos = { t.pos[0], t.pos[1], t.pos[2] };
    return true;
}

// ============================================================
// 光線の標的を決める（1 チャンネル 1 グループ）→ staging へ写す。使っている光線が無ければ何もしない
// ============================================================
void SwarmSystem::DispatchBeamTargets()
{
    if (!m_BeamTargetCS || !m_BeamTargetBuffer) return;
    bool any = false;
    for (uint32_t ch = 0; ch < Swarm::kMaxBeams; ++ch)
    {
        uint32_t cmd;
        memcpy(&cmd, &m_CachedBeamTargetCB.origin[ch].w, sizeof(cmd));
        any |= (cmd != Swarm::kBeamTargetIdle);
    }
    if (!any) return;

    m_BeamTargetCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
    m_BeamTargetCS->WriteBuffer(m_Context, 4, &m_CachedBeamTargetCB);
    m_BeamTargetCS->Bind(m_Context);
    m_BeamTargetCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BeamTargetCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BeamTargetCS->SetUAV(m_Context, "beamTargets", m_BeamTargetUAV.Get());
    m_BeamTargetCS->BindUAVs(m_Context);
    m_Context->Dispatch(Swarm::kMaxBeams, 1, 1);
    m_BeamTargetCS->UnbindSRVs(m_Context);
    m_BeamTargetCS->UnbindUAVs(m_Context);

    m_Context->CopyResource(m_BeamTargetStaging[m_BeamTargetStagingWrite].Get(), m_BeamTargetBuffer.Get());
    m_BeamTargetStagingFilled[m_BeamTargetStagingWrite] = true;
    m_BeamTargetStagingWrite = (m_BeamTargetStagingWrite + 1) % kBossStaging;
}

// 一番古い staging を読む（待たない）。読めなければ前回の答えのまま
void SwarmSystem::ReadBeamTargets()
{
    const int readIndex = m_BeamTargetStagingWrite;
    if (!m_BeamTargetStagingFilled[readIndex] || !m_BeamTargetStaging[readIndex]) return;
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_BeamTargetStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    memcpy(m_BeamTargets, mapped.pData, sizeof(m_BeamTargets));
    m_Context->Unmap(m_BeamTargetStaging[readIndex].Get(), 0);
    m_BeamTargetStagingFilled[readIndex] = false;   // 同じ答えを 2 度読まない（光線が止まった後に古い物が残らない）
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
// 巡路の更新
// プレイヤーのマスが変わった時だけ Dial 法で距離場を作り直し、向き表を Map で上げる
//（地形は静的）。作り直しは別スレッド：
//   1) 走っている作り直しが終わっていれば、結果を m_Flow へ貰って GPU へ上げる
//   2) 走っている物が無く、プレイヤーのマスが最後に作らせたマスと違えば、次を作らせる
// GPU の向き表は 1〜2 フレーム古いマスの物になるが、雑魚は遠くの大回りに使うだけ
// （プレイヤーの近くは直進）なので困らない
// ============================================================
void SwarmSystem::UpdateFlowField(const Vector3& playerPos)
{
    if (!m_FlowBuffer || m_Flow.Width() <= 0) return;

    if (m_FlowJob.valid() && m_FlowJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        if (m_FlowJob.get())
        {
            m_Flow.TakeResult(m_FlowWorker);
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (SUCCEEDED(m_Context->Map(m_FlowBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
            {
                const auto& dirs = m_Flow.Directions();
                memcpy(mapped.pData, dirs.data(), sizeof(Vector2) * dirs.size());
                m_Context->Unmap(m_FlowBuffer.Get(), 0);
            }
        }
    }
    if (m_FlowJob.valid()) return;   // まだ作っている

    const int gx = (int)std::floor((playerPos.x - m_CachedFrameCB.gridOrigin.x) / m_CachedFrameCB.cellSize);
    const int gz = (int)std::floor((playerPos.z - m_CachedFrameCB.gridOrigin.z) / m_CachedFrameCB.cellSize);

    // 地形は静的なので、場は目標マスだけで決まる。プレイヤーがマスを跨いだ時だけ作り直す
    if (gx == m_FlowRequestX && gz == m_FlowRequestZ) return;
    m_FlowRequestX = gx;
    m_FlowRequestZ = gz;

    m_FlowWorker.CopySettings(m_Flow);   // パネルで変えた探索範囲など
    m_FlowJob = std::async(std::launch::async, [this, gx, gz]
        {
            auto tf0 = std::chrono::high_resolution_clock::now();   // TEMP-TEST
            const bool built = m_FlowWorker.Build(gx, gz);
            { static int n = 0; if (n++ % 30 == 0) std::cout << "[flow] build ms=" << std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tf0).count() << " target=(" << gx << "," << gz << ")" << std::endl; }   // TEMP-TEST
            return built;
        });
}

void SwarmSystem::WaitFlowJob()
{
    if (m_FlowJob.valid()) m_FlowJob.get();
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
    ReadPlayerHits();
    ReadAreaBirths();
    ReadBeamTargets();
    ReadTriggerEvents();
    ReadSplitEvents();
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

        // 3) dispatch
        m_SpawnProjCS->Bind(m_Context);
        m_SpawnProjCS->SetSRV(m_Context, "spawnRequests", m_SpawnProjSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "spawnExtra", m_SpawnProjExtraSRV.Get());
        // 曲線の型は生成時に標的を捕捉して path を組む。
        // counters には前ステップの「プレイヤーに一番近い雑魚」が残っている
        m_SpawnProjCS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_SpawnProjCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 着弾点が洞窟の中なら真上から落とす
        m_SpawnProjCS->SetUAV(m_Context, "projectiles", m_ProjUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "paths", m_PathUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projTags", m_ProjTagUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projBoost", m_ProjBoostUAV.Get());   // 出す範囲への倍率
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
//   0 counter ゼロクリア → 0b 空間ハッシュ → 1 敵AI → 2 敵積分 → 2b 重なり解消 → 3 弾積分 → 4 命中(+オーブ落下)
//   → 5 照準 → 6 接触 → 7 オーブ吸引・取得
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
        m_EnemyAICS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "cellCount", m_CellCountSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "cellItems", m_CellItemsSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "flowField", m_FlowSRV.Get());   // 巡路の向き表
        m_EnemyAICS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 点火した自爆兵は止まる
        m_EnemyAICS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());    // 崖は壁と同じく止める
        m_EnemyAICS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyAICS->SetUAV(m_Context, "enemySlow", m_EnemySlowUAV.Get());   // 毒の池の減速（速度に掛けて数え下げる）
        m_EnemyAICS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_EnemyAICS->UnbindSRVs(m_Context);
        m_EnemyAICS->UnbindUAVs(m_Context);
    }

    // ---- 2) 雑魚の積分 ----
    if (m_EnemyMoveCS)
    {
        m_EnemyMoveCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyMoveCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_EnemyMoveCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // 種類の体格（壁から体半径を離す）
        m_EnemyMoveCS->Bind(m_Context);
        m_EnemyMoveCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyMoveCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 斜面の高さ
        m_EnemyMoveCS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());       // 通行マス（壁への滑り・壁からの脱出）
        m_EnemyMoveCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 種類（幽霊は壁を素通り）
        m_EnemyMoveCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyMoveCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
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
        m_EnemyPushCS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "cellCount", m_CellCountSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "cellItems", m_CellItemsSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());
        m_EnemyPushCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());   // 種類（幽霊は壁・崖を無視）
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
        m_ProjMoveCS->Bind(m_Context);
        m_ProjMoveCS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
        // 曲線・追尾用。雑魚の位置は 2) で確定済み（UAV は外してある）
        m_ProjMoveCS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "terrainHeight", m_HeightSRV.Get());   // 斜面に潜ったら命中
        m_ProjMoveCS->SetUAV(m_Context, "paths", m_PathUAV.Get());
        // 寿命切れ・壁で範囲を出すプロファイル用
        m_ProjMoveCS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());
        m_ProjMoveCS->SetSRV(m_Context, "projBoost", m_ProjBoostSRV.Get());   // 出す範囲の威力 / 持続の倍率
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

        // tick した範囲が 1 つも無いステップは、各スレッドが counter を 1 リードバックんで帰る
        m_AreaDamageCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_AreaDamageCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_AreaDamageCS->WriteBuffer(m_Context, 2, &m_CachedOrbCB);
        m_AreaDamageCS->WriteBuffer(m_Context, 3, &m_CachedBomberCB);   // エリートの体格・経験値
        m_AreaDamageCS->Bind(m_Context);
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

// ============================================================
// 弾から粒子を発射（Flush の最後、粒子の Flush より前）
// 弾の位置は今フレームの子ステップで確定済み。
// 粒子システムの particles / deadList を借りて直接書く
// ============================================================
void SwarmSystem::DispatchEmit(float dt, float totalTime)
{
    if (!m_Particles || !m_EmitCS) return;
    if (!m_VFX.GetRecipeSRV() || !m_VFX.GetEmitterSRV()) return;

    // 予約を 0 に、空き数を最新に
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_EmitBudgetUAV.Get(), zero);
    m_Particles->RefreshDeadCount(m_Context);

    // b0 = GlobalCB（ParticleCommon の物）。dt と seed だけ要る
    struct GlobalCB { float dt; float total; uint32_t seed; int emitterCount; } gcb;
    gcb.dt = dt;
    gcb.total = totalTime;
    gcb.seed = (uint32_t)(totalTime * 1000.0f) ^ 0x5bd1e995u;
    gcb.emitterCount = 0;

    // WriteBuffer の index はレジスタ番号。
    // この CS では SwarmFrameCB を b2 に逃がしてある（b0/b1 は ParticleCommon）
    m_EmitCS->WriteBuffer(m_Context, 0, &gcb);
    m_EmitCS->WriteBuffer(m_Context, 2, &m_CachedFrameCB);
    m_EmitCS->Bind(m_Context);
    m_EmitCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
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

    // ---- 吸い寄せ中の経験値オーブの尾 ----
    // 弾・範囲の後に回す：空きが足りない時に削られるのは見た目だけのこちら
    if (m_OrbEmitCS && orbLook.trail && m_OrbTrailVfx != 0)
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
// 液溜まりの追跡（Liquid entry、2026-10-02）
// 範囲の槽 1 つに 1 スレッド。前フレームに空だった / 残り時間が増えた槽 = 新しい範囲。
// その時に、出した物が残した向き（areaDirs、ProjMoveCS が書く）を 1 回だけ受け取り、
// 最初に見た時の残り時間を覚える（年齢 = それ - 今の残り時間）。時計は要らない
// ============================================================
void SwarmSystem::DispatchLiquidTrack()
{
    if (!m_LiquidTrackCS) return;

    m_LiquidTrackCS->Bind(m_Context);
    m_LiquidTrackCS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
    m_LiquidTrackCS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
    m_LiquidTrackCS->SetUAV(m_Context, "areaDirs", m_AreaDirUAV.Get());
    m_LiquidTrackCS->SetUAV(m_Context, "liquidTrack", m_LiquidTrackUAV.Get());
    m_LiquidTrackCS->SetUAV(m_Context, "areaBirths", m_AreaBirthUAV.Get());   // 生まれた範囲の数（揺らす範囲・レシピ毎。新しい範囲を見つけた所で数える）
    m_LiquidTrackCS->BindUAVs(m_Context);

    m_Context->Dispatch((Swarm::kMaxAreas + 63) / 64, 1, 1);

    m_LiquidTrackCS->UnbindSRVs(m_Context);
    m_LiquidTrackCS->UnbindUAVs(m_Context);
}

// ============================================================
// 死んだ敵の砕け散り（2026-10-02）
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
    m_CorpseTrackCS->SetUAV(m_Context, "splitEvents", m_SplitUAV.Get());   // スプリッターが死んだ所（2026-10-03）
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
    m_CorpseListCS->BindUAVs(m_Context);
    m_Context->Dispatch((Swarm::kMaxCorpses + 63) / 64, 1, 1);
    m_CorpseListCS->UnbindSRVs(m_Context);
    m_CorpseListCS->UnbindUAVs(m_Context);

    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        for (auto& args : m_CorpseDrawArgs[k])
            if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_CorpseListUAV[k].Get());
}

// ============================================================
// 砕け散る部品を描く。雑魚と同じ網・同じ PS・同じテクスチャの切り替え（自爆兵は自分のテクスチャ、幽霊は半透明）。
// VS だけ SwarmCorpseVS に差し替え、submesh（部品）毎に CorpseCB.part を入れて間接描画
// ============================================================
void SwarmSystem::RenderCorpses(const Matrix& view, const Matrix& proj)
{
    if (!corpse.enabled || !m_CorpseVS || !m_EnemyMaterial || !m_EnemyModel || m_CorpseDrawArgs[0].empty()) return;

    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    EnemyRenderCB cb;
    cb.view = view;
    cb.proj = proj;
    // Material::Bind は VS も入れ直すので、その後で砕け散りの VS に差し替える
    auto bindVS = [&]()
        {
            m_EnemyMaterial->Bind(m_Context);
            m_Context->PSSetSamplers(0, 1, &samp);
            m_CorpseVS->Bind(m_Context);
            m_CorpseVS->WriteBuffer(m_Context, 0, &cb);
            m_CorpseVS->WriteBuffer(m_Context, 2, &m_CachedAICB);     // 体の大きさ
            m_CorpseVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB); // エリート・Boss の倍率、幽霊の見た目
            m_CorpseVS->SetSRV(m_Context, "corpses", m_CorpseSRV.Get());
        };
    bindVS();

    CorpseCB ccb;
    ccb.partCount = m_EnemyAnim.partCount;
    ccb.time = m_AnimClock;
    ccb.life = corpse.life;
    ccb.gravity = corpse.gravity;
    ccb.fling = corpse.fling;
    ccb.up = corpse.up;
    ccb.spin = corpse.spin;
    ccb.bounce = corpse.bounce;
    ccb.flash = corpse.flash;
    ccb.flashTime = corpse.flashTime;
    ccb.shrinkStart = corpse.shrinkStart;
    for (int p = 0; p < 8; ++p) ccb.pivot[p] = m_PartPivots[p];

    const auto& subs = m_EnemyModel->GetSubMeshes();
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
    {
        if (k == Swarm::kDrawListGhost)
        {
            bindVS();   // 雑魚のテクスチャに戻す
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().AlphaBlend(), bf, 0xFFFFFFFF);
        }
        else if (Texture* albedo = ListAlbedo(k))
            m_EnemyPS->SetTexture(m_Context, 0, albedo);
        else if (k != Swarm::kDrawListMob)
            bindVS();   // 自分のテクスチャが無い種類は雑魚のテクスチャに戻す
        m_CorpseVS->SetSRV(m_Context, "corpseList", m_CorpseListSRV[k].Get());

        for (size_t i = 0; i < subs.size() && i < m_CorpseDrawArgs[k].size(); ++i)
        {
            if (!subs[i].mesh || !m_CorpseDrawArgs[k][i]) continue;
            ccb.part = (uint32_t)i;
            m_CorpseVS->WriteBuffer(m_Context, 4, &ccb);
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_CorpseDrawArgs[k][i].Get(), 0);
        }
        if (k == Swarm::kDrawListGhost)
        {
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().Opaque(), bf, 0xFFFFFFFF);
        }
    }
    // 次のフレームの Compute が UAV として使うので外す
    m_CorpseVS->UnbindSRVs(m_Context);
}

// ============================================================
// 液溜まり（GPU の範囲の Liquid entry）
// 範囲の全槽を地面の格子 1 枚ずつ（LIQUID_GRID^2 の四角、頂点バッファ無し）。
// 死んだ槽・レシピに液体が無い範囲は VS が潰す。PS は CPU の経路と同じ VFXLiquidPS。
// 深度テストあり・書き込み無し、乗算済み alpha（外側の光は alpha 0 = 加算）。
// 雑魚・オーブの後、足元の丸い影の前（雑魚の影が液面に落ちる）。点光源はまだ繋がっている
// ============================================================
void SwarmSystem::RenderLiquids(CameraBase* camera, const LightBuffer& light)
{
    if (!liquids || !m_LiquidVS || !m_LiquidPS || !m_HeightSRV) return;
    if (m_VFX.GetLiquidDefCount() == 0 || !m_VFX.GetLiquidDefSRV() || !m_VFX.GetRecipeLiquidSRV()) return;

    VFXLiquidRenderer::CameraCB cam;
    cam.viewProj = camera->GetViewMatrix() * camera->GetProjectionMatrix();
    cam.hasTerrain = 1u;
    VFXLiquidRenderer::FrameCB frame;
    frame.time = m_AnimClock;
    LightBuffer l = light;   // cameraPosition は Render が入れ直した物

    m_LiquidVS->WriteBuffer(m_Context, 0, &cam);
    m_LiquidVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);   // 地形の格子
    m_LiquidPS->WriteBuffer(m_Context, 0, &l);
    m_LiquidPS->WriteBuffer(m_Context, 1, &frame);

    m_LiquidVS->SetSRV(m_Context, "areas", m_AreaSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "areaStates", m_AreaStateSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "liquidTrack", m_LiquidTrackSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());
    m_LiquidVS->SetSRV(m_Context, "recipeLiquid", m_VFX.GetRecipeLiquidSRV());
    m_LiquidVS->SetSRV(m_Context, "liquidDefs", m_VFX.GetLiquidDefSRV());
    m_LiquidPS->SetSRV(m_Context, "liquidDefs", m_VFX.GetLiquidDefSRV());

    m_LiquidVS->Bind(m_Context);
    m_LiquidPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    constexpr UINT kVerts = 16 * 16 * 6;   // LiquidCommon.hlsli の LIQUID_VERTS
    m_Context->DrawInstanced(kVerts, Swarm::kMaxAreas, 0, 0);

    // 次のフレームの Compute が UAV として使うので外す（liquidTrack・areas）
    m_LiquidVS->UnbindSRVs(m_Context);
    m_LiquidPS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 範囲の連番画像を描く。環の全枠を 6 頂点ずつ描き、空の枠は VS が潰す
// 混合は乗算済み alpha（加算の物は alpha 0）。深度は読むだけ
// ============================================================
void SwarmSystem::RenderSprites(CameraBase* camera)
{
    if (!camera || !m_SpriteVS || !m_SpritePS || m_VFX.GetSpriteDefCount() == 0) return;
    if (!m_VFX.GetSpriteArraySRV() || !m_VFX.GetSpriteDefSRV()) return;

    VFXSpriteCameraCB cb = VFXSpriteCameraCB::From(camera);
    m_SpriteVS->WriteBuffer(m_Context, 0, &cb);
    m_SpriteVS->Bind(m_Context);
    m_SpritePS->Bind(m_Context);

    ID3D11ShaderResourceView* vsSRV[2] = { m_SpriteSRV.Get(), m_VFX.GetSpriteDefSRV() };
    m_Context->VSSetShaderResources(0, 2, vsSRV);
    ID3D11ShaderResourceView* psSRV = m_VFX.GetSpriteArraySRV();
    m_Context->PSSetShaderResources(0, 1, &psSRV);
    ID3D11SamplerState* samp = RenderStates::Get().PointClamp();
    m_Context->PSSetSamplers(0, 1, &samp);

    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    RenderStates::Get().ApplyAlphaBlend(m_Context);

    m_Context->DrawInstanced(6, kMaxSprites, 0, 0);

    // 次のフレームで CS が UAV として使うので必ず外す
    ID3D11ShaderResourceView* nulls[2] = {};
    m_Context->VSSetShaderResources(0, 2, nulls);
    m_Context->PSSetShaderResources(0, 1, nulls);
    RenderStates::Get().Restore(m_Context);
}

// ============================================================
// counter の写しを発行（待たない）
// ============================================================
void SwarmSystem::RequestReadback()
{
    m_Readback.RequestCopy(m_Context, m_CounterBuffer.Get());

    // 誘発の環も写す（読むのは 2 フレーム後の Flush）
    if (m_TriggerBuffer)
    {
        m_Context->CopyResource(m_TriggerStaging[m_TriggerStagingWrite].Get(), m_TriggerBuffer.Get());
        m_TriggerStagingFilled[m_TriggerStagingWrite] = true;
        m_TriggerStagingWrite = (m_TriggerStagingWrite + 1) % kTriggerStaging;
    }
    // 分裂の環も
    if (m_SplitBuffer)
    {
        m_Context->CopyResource(m_SplitStaging[m_SplitStagingWrite].Get(), m_SplitBuffer.Get());
        m_SplitStagingFilled[m_SplitStagingWrite] = true;
        m_SplitStagingWrite = (m_SplitStagingWrite + 1) % kTriggerStaging;
    }
}

// ============================================================
// 誘発の環を読む（一番古い staging、待たない）。総数は GPU で永久に累積されるので、
// 読めなかったフレームがあっても次に読めた時に取りこぼさない（環の長さを超えて溜まった分だけ捨てる）
// ============================================================
void SwarmSystem::ReadTriggerEvents()
{
    const int readIndex = m_TriggerStagingWrite;   // 次に書く = 一番古い
    if (!m_TriggerStagingFilled[readIndex] || !m_TriggerStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_TriggerStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;

    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
    const auto* ring = reinterpret_cast<const Swarm::TriggerEvent*>(bytes + 16);

    uint32_t from = m_TriggerRead;
    if (total - from > Swarm::kMaxTriggerEvents) from = total - Swarm::kMaxTriggerEvents;   // 溢れた分は捨てる
    for (uint32_t n = from; n != total; ++n)
        m_TriggerEvents.push_back(ring[n % Swarm::kMaxTriggerEvents]);
    m_TriggerRead = total;

    m_Context->Unmap(m_TriggerStaging[readIndex].Get(), 0);

    // 誰も取り出さない（杖が無いシーン）でも溜まり続けないように
    if (m_TriggerEvents.size() > Swarm::kMaxTriggerEvents)
        m_TriggerEvents.erase(m_TriggerEvents.begin(), m_TriggerEvents.end() - Swarm::kMaxTriggerEvents);
}

// ============================================================
// 分裂の環を読む（誘発の環と同じ作り。一番古い staging、待たない）
// ============================================================
void SwarmSystem::ReadSplitEvents()
{
    const int readIndex = m_SplitStagingWrite;   // 次に書く = 一番古い
    if (!m_SplitStagingFilled[readIndex] || !m_SplitStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_SplitStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;

    const auto* bytes = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t total = *reinterpret_cast<const uint32_t*>(bytes);
    const auto* ring = reinterpret_cast<const Swarm::SplitEvent*>(bytes + 16);

    uint32_t from = m_SplitRead;
    if (total - from > Swarm::kMaxSplitEvents) from = total - Swarm::kMaxSplitEvents;   // 溢れた分は捨てる
    for (uint32_t n = from; n != total; ++n)
        m_SplitEvents.push_back(ring[n % Swarm::kMaxSplitEvents]);
    m_SplitRead = total;

    m_Context->Unmap(m_SplitStaging[readIndex].Get(), 0);

    if (m_SplitEvents.size() > Swarm::kMaxSplitEvents)
        m_SplitEvents.erase(m_SplitEvents.begin(), m_SplitEvents.end() - Swarm::kMaxSplitEvents);
}

// ============================================================
// 地面の警告の輪（CPU から毎フレーム）
// ============================================================
void SwarmSystem::SetWarnCircles(const WarnCircle* circles, int count)
{
    m_WarnCircleCount = 0;
    if (!m_WarnCircleBuffer || count <= 0) return;
    count = (std::min)(count, kMaxWarnCircles);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    if (FAILED(m_Context->Map(m_WarnCircleBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
    memcpy(mapped.pData, circles, sizeof(WarnCircle) * count);
    m_Context->Unmap(m_WarnCircleBuffer.Get(), 0);
    m_WarnCircleCount = count;
}

// ============================================================
// Boss の様子を読む（GPUReadback::TryRead と同じ。一番古い staging、待たない）
// ============================================================
void SwarmSystem::ReadBossInfo()
{
    const int readIndex = m_BossStagingWrite;   // 次に書く = 一番古い
    if (!m_BossStagingFilled[readIndex] || !m_BossStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_BossStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;   // まだ。前回の値のまま
    memcpy(&m_BossInfo, mapped.pData, sizeof(Swarm::BossInfo));
    m_Context->Unmap(m_BossStaging[readIndex].Get(), 0);
}

// ============================================================
// プレイヤーが受けた打撃の向きを読む（一番古い staging、待たない）。
// 累計なので前回値との差分を足す（読めなかったフレームの分も次で拾える）。
// int の和は回り込んでも uint の引き算 → int で正しい差になる
// ============================================================
void SwarmSystem::ReadPlayerHits()
{
    const int readIndex = m_PlayerHitStagingWrite;   // 次に書く = 一番古い
    if (!m_PlayerHitStagingFilled[readIndex] || !m_PlayerHitStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_PlayerHitStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    Swarm::PlayerHitInfo now;
    memcpy(&now, mapped.pData, sizeof(now));
    m_Context->Unmap(m_PlayerHitStaging[readIndex].Get(), 0);

    auto diff = [](int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); };
    const float s = 1.0f / Swarm::kHitDirScale;
    PlayerHits& p = m_PendingPlayerHits;
    p.meleeDir += Vector2((float)diff(now.meleeX, m_LastPlayerHits.meleeX), (float)diff(now.meleeZ, m_LastPlayerHits.meleeZ)) * s;
    p.blastDir += Vector2((float)diff(now.blastX, m_LastPlayerHits.blastX), (float)diff(now.blastZ, m_LastPlayerHits.blastZ)) * s;
    p.melee += now.meleeCount - m_LastPlayerHits.meleeCount;
    p.blasts += now.blastCount - m_LastPlayerHits.blastCount;
    m_LastPlayerHits = now;
}

SwarmSystem::PlayerHits SwarmSystem::ConsumePlayerHits()
{
    const PlayerHits h = m_PendingPlayerHits;
    m_PendingPlayerHits = {};
    return h;
}

// 生まれた範囲の累計を読み、差分を溜める（ReadPlayerHits と同じ輪転）
void SwarmSystem::ReadAreaBirths()
{
    const int readIndex = m_AreaBirthStagingWrite;   // 次に書く = 一番古い
    if (!m_AreaBirthStagingFilled[readIndex] || !m_AreaBirthStaging[readIndex]) return;

    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = m_Context->Map(m_AreaBirthStaging[readIndex].Get(), 0,
        D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    if (FAILED(hr) || hr == DXGI_ERROR_WAS_STILL_DRAWING) return;
    std::array<uint32_t, kAreaBirthSlots> now;
    memcpy(now.data(), mapped.pData, sizeof(uint32_t) * kAreaBirthSlots);
    m_Context->Unmap(m_AreaBirthStaging[readIndex].Get(), 0);
    m_PendingShakes += now[0] - m_LastAreaBirths[0];
    for (uint32_t k = 0; k < Swarm::kAreaBirthKinds; ++k)
        m_PendingAreaBirths[k] += now[1 + k] - m_LastAreaBirths[1 + k];
    m_LastAreaBirths = now;
}

uint32_t SwarmSystem::ConsumeShakeAreas()
{
    const uint32_t n = m_PendingShakes;
    m_PendingShakes = 0;
    return n;
}

void SwarmSystem::ConsumeAreaBirths(std::array<uint32_t, Swarm::kAreaBirthKinds>& out)
{
    for (uint32_t k = 0; k < Swarm::kAreaBirthKinds; ++k) out[k] += m_PendingAreaBirths[k];
    m_PendingAreaBirths = {};
}

// ============================================================
// プレイヤーの被弾を取り出す（取ったら 0 に戻す）
// ============================================================
float SwarmSystem::ConsumePlayerDamage()
{
    const float d = m_PendingPlayerDamage;
    m_PendingPlayerDamage = 0.0f;
    return d;
}
// ============================================================
// 取得した経験値を取り出す（取ったら 0 に戻す）
// ============================================================
float SwarmSystem::ConsumeExp()
{
    const float e = m_PendingExp;
    m_PendingExp = 0.0f;
    return e;
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
    const UINT zero[4] = { 0, 0, 0, 0 };
    m_Context->ClearUnorderedAccessViewUint(m_EnemyStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_ProjStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_ProjTagUAV.Get(), zero);   // 消した弾を「消えた」と報告させない
    m_Context->ClearUnorderedAccessViewUint(m_OrbStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_AreaStateUAV.Get(), zero);
    m_Context->ClearUnorderedAccessViewUint(m_AreaDirUAV.Get(), zero);
    // 「前は生きていた」も消す：全消しで全員が一度に砕け散らないように
    m_Context->ClearUnorderedAccessViewUint(m_EnemyPrevAliveUAV.Get(), zero);
    m_PendingAreas.clear();

    // まだ GPU に上げていない依頼も捨てる（消した直後に湧き直さないように）
    m_PendingEnemies.clear();
    m_PendingRecycles.clear();
    m_PendingProjectiles.clear();
    m_PendingProjExtra.clear();
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
// ============================================================
// デバッグ: 判定球のワイヤーフレーム
// 全スロットを DrawInstanced し、死んだ物は VS 側でクリップ外へ畳む。
// 8192 × 64 頂点 = 約 52 万頂点。デバッグ用途なら気にしない
// ============================================================
void SwarmSystem::RenderDebug(CameraBase* camera)
{
    if (!camera || !m_DebugVS || !m_DebugPS) return;

    DebugCB cb;
    cb.view = camera->GetViewMatrix().Transpose();
    cb.proj = camera->GetProjectionMatrix().Transpose();
    m_DebugVS->WriteBuffer(m_Context, 0, &cb);

    m_DebugVS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_DebugVS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());

    m_DebugVS->Bind(m_Context);
    m_DebugPS->Bind(m_Context);

    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    m_Context->DrawInstanced(64, Swarm::kMaxProjectiles, 0, 0);

    m_DebugVS->UnbindSRVs(m_Context);
    if (m_DebugEnemyVS)
    {
        m_DebugEnemyVS->WriteBuffer(m_Context, 0, &cb);
        m_DebugEnemyVS->WriteBuffer(m_Context, 1, &m_CachedAICB);   // 半径と直線部の長さ
        m_DebugEnemyVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_DebugEnemyVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_DebugEnemyVS->Bind(m_Context);

        m_Context->DrawInstanced(128, Swarm::kMaxEnemies, 0, 0);

        m_DebugEnemyVS->UnbindSRVs(m_Context);
    }
    RenderStates::Get().Restore(m_Context);
}
// ============================================================
// 描画の全部（不透明な物 → 半透明・重ね描き）。画面のアウトラインを間に挟むシーンは 2 つを分けて呼ぶ
// ============================================================
void SwarmSystem::Render(CameraBase* camera, const LightBuffer& light)
{
    RenderOpaque(camera, light);
    RenderOverlay(camera, light);
}

// ============================================================
// 雑魚の本描画（不透明：雑魚・砕け散り・経験値オーブ。深度を書く）
// 頂点/インデックスは1体分、インスタンス数 = プール全体。
// 生死は VS が state を見て判断する（死んだ槽は near 面の外へ畳む）
// ============================================================
void SwarmSystem::RenderOpaque(CameraBase* camera, const LightBuffer& light)
{
    if (!camera || !m_EnemyMaterial || !m_EnemyModel) return;

    // ---- 活きスロットの一覧 → 描画 instance 数 ----
    // 描画の直前に作る（Flush 後の state が確定している）。
    // UAV を initialCount = 0 で bind すると append counter が 0 に戻る
    const bool indirect = m_EnemyCompactCS && m_AliveListUAV && !m_EnemyDrawArgs[0].empty();
    if (indirect)
    {
        // Boss の様子は毎回 0 から（居なければ alive = 0 のまま）
        const UINT zero[4] = { 0, 0, 0, 0 };
        m_Context->ClearUnorderedAccessViewUint(m_BossInfoUAV.Get(), zero);

        m_EnemyCompactCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyCompactCS->Bind(m_Context);
        m_EnemyCompactCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
        m_EnemyCompactCS->SetSRV(m_Context, "enemyMaxHp", m_EnemyMaxHpSRV.Get());
        m_EnemyCompactCS->SetUAV(m_Context, "aliveList", m_AliveListUAV.Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "mobList", m_KindListUAV[Swarm::kDrawListMob].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "bomberList", m_KindListUAV[Swarm::kDrawListBomber].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "ghostList", m_KindListUAV[Swarm::kDrawListGhost].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "splitterList", m_KindListUAV[Swarm::kDrawListSplitter].Get(), 0);
        m_EnemyCompactCS->SetUAV(m_Context, "bossInfo", m_BossInfoUAV.Get());
        m_EnemyCompactCS->BindUAVs(m_Context);
        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);
        m_EnemyCompactCS->UnbindSRVs(m_Context);
        m_EnemyCompactCS->UnbindUAVs(m_Context);

        // Boss の様子を staging へ（読むのは 2 フレーム後の Flush）
        m_Context->CopyResource(m_BossStaging[m_BossStagingWrite].Get(), m_BossInfoBuffer.Get());
        m_BossStagingFilled[m_BossStagingWrite] = true;
        m_BossStagingWrite = (m_BossStagingWrite + 1) % kBossStaging;

        // 種類毎の一覧の長さ → その種類の submesh 毎の InstanceCount
        for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
            for (auto& args : m_EnemyDrawArgs[k])
                if (args) m_Context->CopyStructureCount(args.Get(), sizeof(uint32_t) * 1, m_KindListUAV[k].Get());
        // HP バーも同じ数だけ（DrawInstancedIndirect の InstanceCount も 2 番目 = 4 バイト目）
        if (m_HpBarArgs)
            m_Context->CopyStructureCount(m_HpBarArgs.Get(), sizeof(uint32_t) * 1, m_AliveListUAV.Get());
        if (m_BomberRingArgs)
            m_Context->CopyStructureCount(m_BomberRingArgs.Get(), sizeof(uint32_t) * 1,
                m_KindListUAV[Swarm::kDrawListBomber].Get());
    }

    // sampler は雑魚とオーブで共通。Material::Bind は sampler を触らないので自分で入れる
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();

    // VS/PS/既定テクスチャをまとめて bind
    m_EnemyMaterial->Bind(m_Context);
    m_Context->PSSetSamplers(0, 1, &samp);
    PointLightManager::Get().BindPS(m_Context);   // t6/t7（雑魚とオーブ共通）

    // VS b0: VS.hlsl と同じ row_major なので Transpose しない
    EnemyRenderCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    m_EnemyVS->WriteBuffer(m_Context, 0, &cb);
    // VS b2: 被弾の閃光が g_HitStun / g_HitFlash を読む（b1 の FrameCB は VS では未使用）
    m_EnemyVS->WriteBuffer(m_Context, 2, &m_CachedAICB);
    // VS b5: 自爆兵の点滅と膨らみ
    m_EnemyVS->WriteBuffer(m_Context, 5, &m_CachedBomberCB);

    // PS b0: 光。cameraPosition は Renderer が DrawMesh の中でしか詰めないので自分で入れる
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();
    m_EnemyPS->WriteBuffer(m_Context, 0, &l);
    // PS b1: 溶解は使わない（threshold < 0 で OFF）。
    // ※この PS は Renderer と別のオブジェクトなので、書かないと b1 が未定義のまま
    //   → threshold >= 0 と解釈されて全ピクセルが clip され、雑魚が丸ごと消える
    DissolveCB dcb = {};
    dcb.threshold = -1.0f;
    m_EnemyPS->WriteBuffer(m_Context, 1, &dcb);

    m_EnemyVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());
    // 部品アニメの表（無ければ未 bind のまま。VS は enabled = 0 で読まない）
    if (m_PartAnimSRV)
        m_EnemyVS->SetSRV(m_Context, "partAnim", m_PartAnimSRV.Get());
    m_EnemyAnim.time = m_AnimClock;
    m_EnemyAnim.walkRate = enemyWalkAnimRate;

    // VS は一覧（aliveList の名前で繋ぐ）経由でしかスロットを引かないので、間接引数が無い時は描かない。
    // 種類毎に一覧とテクスチャ（t0）を差し替えて同じメッシュを描く。雑魚のテクスチャは Material::Bind が入れた物
    const auto& subs = m_EnemyModel->GetSubMeshes();
    // 陣営のアウトライン（2026-10-06）：敵はステンシルに 2 を書く（PostProcess/Outline が赤い線にする）。描き終えたら元へ
    m_Context->OMSetDepthStencilState(RenderStates::Get().DepthStencilWrite(), 2);
    for (uint32_t k = 0; indirect && k < Swarm::kEnemyKinds; ++k)
    {
        m_EnemyVS->SetSRV(m_Context, "aliveList", m_KindListSRV[k].Get());
        if (Texture* albedo = ListAlbedo(k))
            m_EnemyPS->SetTexture(m_Context, 0, albedo);
        else if (k != Swarm::kDrawListMob && k != Swarm::kDrawListGhost)
        {
            m_EnemyMaterial->Bind(m_Context);   // 自分のテクスチャが無い種類は雑魚のテクスチャに戻す
            m_Context->PSSetSamplers(0, 1, &samp);
        }
        if (k == Swarm::kDrawListGhost)
        {
            // 幽霊：雑魚のテクスチャに戻し、alpha blend（乗算済み: 色はそのまま足され、alpha 分だけ後ろが消える = 光る半透明）。
            // 深度は書く（後から描くオーブ・粒子が幽霊の奥にある時に正しく隠れる）
            m_EnemyMaterial->Bind(m_Context);
            m_Context->PSSetSamplers(0, 1, &samp);
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().AlphaBlend(), bf, 0xFFFFFFFF);
        }

        for (size_t i = 0; i < subs.size() && i < m_EnemyDrawArgs[k].size(); ++i)
        {
            if (!subs[i].mesh || !m_EnemyDrawArgs[k][i]) continue;
            // VS b4: どの部品を描いているか（部品アニメの表の列）
            m_EnemyAnim.part = (uint32_t)i;
            m_EnemyVS->WriteBuffer(m_Context, 4, &m_EnemyAnim);
            subs[i].mesh->DrawIndexedInstancedIndirect(m_Context, m_EnemyDrawArgs[k][i].Get(), 0);
        }
        if (k == Swarm::kDrawListGhost)
        {
            const float bf[4] = { 0, 0, 0, 0 };
            m_Context->OMSetBlendState(RenderStates::Get().Opaque(), bf, 0xFFFFFFFF);
        }
    }
    m_Context->OMSetDepthStencilState(RenderStates::Get().DepthDefault(), 0);   // 砕け散り・オーブはステンシル無し
    // 次のフレームの Compute が UAV として使うので必ず外す
    m_EnemyVS->UnbindSRVs(m_Context);

    // ---- 死んだ敵の砕け散り（同じ網・同じ PS。VS だけ差し替え）----
    RenderCorpses(cb.view, cb.proj);

    // ---- 経験値オーブ（自発光の宝石）----
    // 全スロットを DrawInstanced し、死んだ物は VS が潰す
    if (m_OrbMaterial && m_OrbModel)
    {
        m_OrbMaterial->Bind(m_Context);
        m_Context->PSSetSamplers(0, 1, &samp);
        m_OrbVS->WriteBuffer(m_Context, 0, &cb);   // 同じ row_major View/Proj

        const auto& ol = orbLook;
        OrbLookCB look = {
            m_AnimClock, ol.scale, ol.bobHeight, ol.bobSpeed,
            ol.spinSpeed, ol.pulseAmount, ol.pulseSpeed, ol.fullPullSpeed,
            ol.stretchPerSpeed, ol.stretchMax, ol.tiltMax, ol.pullGlow,
            ol.idleColor, ol.pullColor };
        m_OrbVS->WriteBuffer(m_Context, 4, &look);

        // PS b0: 太陽の向き・カメラ・霧（雑魚と同じ l）、b1: 光り方
        OrbShadeCB shade = {
            ol.emissive, ol.facet, ol.rimGain, ol.rimPower,
            ol.glintGain, ol.glintPower, { 0.0f, 0.0f },
            ol.rimColor };
        m_OrbPS->WriteBuffer(m_Context, 0, &l);
        m_OrbPS->WriteBuffer(m_Context, 1, &shade);

        m_OrbVS->SetSRV(m_Context, "orbs", m_OrbSRV.Get());
        m_OrbVS->SetSRV(m_Context, "orbStates", m_OrbStateSRV.Get());

        for (const auto& sub : m_OrbModel->GetSubMeshes())
        {
            if (sub.mesh)
                sub.mesh->DrawInstanced(m_Context, Swarm::kMaxOrbs);
        }

        m_OrbVS->UnbindSRVs(m_Context);
    }
}

// ============================================================
// 半透明・重ね描き（液面・足元の影・警告の輪・HP バー。深度は読むだけ）。
// トゥーンのアウトライン（2026-10-04）は RenderOpaque との間に描くので、アウトラインがこれらの上に乗らない
// ============================================================
void SwarmSystem::RenderOverlay(CameraBase* camera, const LightBuffer& light)
{
    if (!camera || !m_EnemyMaterial || !m_EnemyModel) return;
    const bool indirect = m_EnemyCompactCS && m_AliveListUAV && !m_EnemyDrawArgs[0].empty();
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();

    // ---- 液溜まり（Liquid entry。点光源を外す前に：毒の池の緑の光が液面に映る）----
    PointLightManager::Get().BindPS(m_Context);   // 間に草などが入っても液面が点光源を読めるように
    RenderLiquids(camera, l);
    PointLightManager::Get().UnbindPS(m_Context);

    // ---- 足元の丸い影 → 足元の警告の輪 → 頭上の HP バー（雑魚の後。深度は読むだけ）----
    if (indirect)
        RenderBlobShadows(camera);
    if (indirect)
        RenderBomberRings(camera);
    RenderDropRings(camera);
    RenderWarnRings(camera);
    if (indirect)
        RenderHpBars(camera);
}

// ============================================================
// CPU が置いた地面の警告の輪（Boss のスラム）。輪 1 つ = 地面に載せた 8x8 の格子 1 枚（頂点バッファ無し）
// ============================================================
void SwarmSystem::RenderWarnRings(CameraBase* camera)
{
    if (!warnRing.enabled || m_WarnCircleCount <= 0 || !m_WarnRingVS || !m_BomberRingPS || !m_HeightSRV) return;

    BomberRingCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = warnRing.fill;
    cb.edge = warnRing.edge;
    cb.back = warnRing.back;
    cb.edgeWidth = warnRing.edgeWidth;
    cb.lift = warnRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_WarnRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    m_WarnRingVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);   // b1: 格子（高さ場を引く）
    m_WarnRingVS->SetSRV(m_Context, "circles", m_WarnCircleSRV.Get());
    m_WarnRingVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());

    m_WarnRingVS->Bind(m_Context);
    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    constexpr UINT kGrid = 8;   // SwarmWarnRingVS の WARN_GRID
    m_Context->DrawInstanced(kGrid * kGrid * 6, (UINT)m_WarnCircleCount, 0, 0);

    m_WarnRingVS->UnbindSRVs(m_Context);   // 高さ場は Compute が書くので外す
    rs.Restore(m_Context);
}

// ============================================================
// 雑魚の足元の丸い影
// 活きている雑魚 1 体につき 1 枚の地面の板（6 頂点、頂点バッファ無し）。数は HP バーと同じ間接引数。
// 四隅を高さ場に載せる（b1 の格子、b2 の groundY、t3 の高さ場）。深度テストあり・書き込み無し、
// premultiplied の AlphaBlend（黒 x a = 地面を a だけ暗くする）
// ============================================================
void SwarmSystem::RenderBlobShadows(CameraBase* camera)
{
    if (!blobShadow.enabled || !m_BlobShadowVS || !m_BlobShadowPS || !m_HpBarArgs || !m_HeightSRV) return;

    BlobShadowCB cb = {};
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.radius = blobShadow.radius;
    cb.strength = blobShadow.strength;
    cb.lift = blobShadow.lift;
    cb.softness = blobShadow.softness;
    cb.clamp = blobShadow.clamp;
    m_BlobShadowVS->WriteBuffer(m_Context, 0, &cb);
    m_BlobShadowPS->WriteBuffer(m_Context, 0, &cb);
    m_BlobShadowVS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);
    m_BlobShadowVS->WriteBuffer(m_Context, 2, &m_CachedAICB);

    m_BlobShadowVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "aliveList", m_AliveListSRV.Get());
    m_BlobShadowVS->SetSRV(m_Context, "heights", m_HeightSRV.Get());

    m_BlobShadowVS->Bind(m_Context);
    m_BlobShadowPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_HpBarArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_BlobShadowVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 点火した自爆兵の足元の警告の輪
// 自爆兵 1 体につき 1 枚の地面の板（6 頂点、頂点バッファ無し）。数は自爆兵の一覧から間接引数で。
// 点火していない・死んだ分は VS が捨てる。深度テストあり・書き込み無し、premultiplied の AlphaBlend
// ============================================================
void SwarmSystem::RenderBomberRings(CameraBase* camera)
{
    if (!bomberRing.enabled || !m_BomberRingVS || !m_BomberRingPS || !m_BomberRingArgs) return;

    BomberRingCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = bomberRing.fill;
    cb.edge = bomberRing.edge;
    cb.back = bomberRing.back;
    cb.edgeWidth = bomberRing.edgeWidth;
    cb.lift = bomberRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_BomberRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    // b2: groundY（足元の高さ）、b4: 爆発半径と導火線の長さ
    m_BomberRingVS->WriteBuffer(m_Context, 2, &m_CachedAICB);
    m_BomberRingVS->WriteBuffer(m_Context, 4, &m_CachedBomberCB);

    m_BomberRingVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_BomberRingVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_BomberRingVS->SetSRV(m_Context, "bomberList", m_KindListSRV[Swarm::kDrawListBomber].Get());
    m_BomberRingVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    m_BomberRingVS->Bind(m_Context);
    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_BomberRingArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_BomberRingVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 隕石（DROP の弾）が落ちる所の警告の輪
// 弾の全スロットを 1 枚ずつ（6 頂点、頂点バッファ無し）。DROP でない・死んだ分は VS が捨てる。
// 着弾点は生成時に決めた軌道の終点（paths の p3）、半径は弾の hitArea の定義から。
// 状態は自爆兵の輪と同じ（深度テストあり・書き込み無し、premultiplied の AlphaBlend）
// ============================================================
void SwarmSystem::RenderDropRings(CameraBase* camera)
{
    if (!dropRing.enabled || !m_DropRingVS || !m_BomberRingPS) return;

    BomberRingCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.fill = dropRing.fill;
    cb.edge = dropRing.edge;
    cb.back = dropRing.back;
    cb.edgeWidth = dropRing.edgeWidth;
    cb.lift = dropRing.lift;
    cb._pad[0] = cb._pad[1] = 0.0f;
    m_DropRingVS->WriteBuffer(m_Context, 0, &cb);
    m_BomberRingPS->WriteBuffer(m_Context, 0, &cb);
    m_DropRingVS->WriteBuffer(m_Context, 2, &m_CachedAICB);   // b2: groundY（着弾点から地面へ下ろす）

    m_DropRingVS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "projStates", m_ProjStateSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "paths", m_PathSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "motions", m_MotionSRV.Get());
    m_DropRingVS->SetSRV(m_Context, "areaDefs", m_AreaDefSRV.Get());

    m_DropRingVS->Bind(m_Context);
    m_BomberRingPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstanced(6, Swarm::kMaxProjectiles, 0, 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_DropRingVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
}

// ============================================================
// 雑魚の HP バー
// 活きスロット 1 体につき 1 枚の板（6 頂点、頂点バッファ無し）。
// 数は雑魚の本描画と同じ aliveList から間接引数で決まる。
// 深度テストあり・書き込み無し: 手前の雑魚や壁に隠れるが、バー同士は上書きし合わない。
// 半透明（減った部分の背景）なので AlphaBlend
// ============================================================
void SwarmSystem::RenderHpBars(CameraBase* camera)
{
    if (!hpBar.enabled || !m_HpBarVS || !m_HpBarPS || !m_HpBarArgs || !m_EnemyMaxHpSRV) return;

    HpBarCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    cb.width = hpBar.width;
    cb.height = hpBar.height;
    cb.offset = hpBar.offset;
    cb.border = hpBar.border;
    cb.fill = hpBar.fill;
    cb.back = hpBar.back;
    cb.edge = hpBar.edge;
    m_HpBarVS->WriteBuffer(m_Context, 0, &cb);
    m_HpBarVS->WriteBuffer(m_Context, 2, &m_CachedAICB);      // 足元の高さ（半径・カプセル）
    m_HpBarVS->WriteBuffer(m_Context, 4, &m_CachedBomberCB);  // エリートの体格
    m_HpBarPS->WriteBuffer(m_Context, 0, &cb);

    m_HpBarVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "aliveList", m_AliveListSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyMaxHp", m_EnemyMaxHpSRV.Get());
    m_HpBarVS->SetSRV(m_Context, "enemyExtra", m_EnemyExtraSRV.Get());

    m_HpBarVS->Bind(m_Context);
    m_HpBarPS->Bind(m_Context);
    m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_Context->IASetInputLayout(nullptr);
    m_Context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

    auto& rs = RenderStates::Get();
    const float blendFactor[4] = { 0, 0, 0, 0 };
    m_Context->OMSetBlendState(rs.AlphaBlend(), blendFactor, 0xFFFFFFFF);
    m_Context->OMSetDepthStencilState(rs.DepthReadOnly(), 0);
    m_Context->RSSetState(rs.CullNone());

    m_Context->DrawInstancedIndirect(m_HpBarArgs.Get(), 0);

    // 次のフレームの Compute が UAV として使うので外す
    m_HpBarVS->UnbindSRVs(m_Context);
    rs.Restore(m_Context);
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

// ============================================================
// シェーダー読み込み
// ============================================================
bool SwarmSystem::LoadShaders(ID3D11Device* device)
{
    auto load = [&](std::shared_ptr<ComputeShader>& cs, const wchar_t* path,
        const char* name) -> bool
        {
            cs = std::make_shared<ComputeShader>();
            HRESULT hr = ShaderPath::Load(cs.get(), device, path);
            std::cout << "[SwarmSystem] " << name << ": "
                << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
            if (FAILED(hr)) { cs.reset(); return false; }
            return true;
        };

    bool ok = true;
    ok &= load(m_ClearCountersCS, L"Shader/Swarm/SwarmClearCountersCS.hlsl", "ClearCountersCS");
    ok &= load(m_SpawnProjCS, L"Shader/Swarm/SwarmSpawnProjCS.hlsl", "SpawnProjCS");
    ok &= load(m_ProjMoveCS, L"Shader/Swarm/SwarmProjMoveCS.hlsl", "ProjMoveCS");
    ok &= load(m_ProjEndCS, L"Shader/Swarm/SwarmProjEndCS.hlsl", "ProjEndCS");
    ok &= load(m_BeamTargetCS, L"Shader/Swarm/SwarmBeamTargetCS.hlsl", "BeamTargetCS");
    ok &= load(m_EmitCS, L"Shader/Swarm/SwarmEmitCS.hlsl", "EmitCS");
    ok &= load(m_SpawnEnemyCS, L"Shader/Swarm/SwarmSpawnEnemyCS.hlsl", "SpawnEnemyCS");
    ok &= load(m_EnemyAICS, L"Shader/Swarm/SwarmEnemyAICS.hlsl", "EnemyAICS");
    ok &= load(m_EnemyMoveCS, L"Shader/Swarm/SwarmEnemyMoveCS.hlsl", "EnemyMoveCS");
    ok &= load(m_RecycleCS, L"Shader/Swarm/SwarmRecycleCS.hlsl", "RecycleCS");
    ok &= load(m_HitCS, L"Shader/Swarm/SwarmHitCS.hlsl", "HitCS");
    ok &= load(m_AimResolveCS, L"Shader/Swarm/SwarmAimResolveCS.hlsl", "AimResolveCS");
    ok &= load(m_ContactCS, L"Shader/Swarm/SwarmContactCS.hlsl", "ContactCS");
    ok &= load(m_OrbCS, L"Shader/Swarm/SwarmOrbMoveCS.hlsl", "OrbMoveCS");
    ok &= load(m_SpawnAreaCS, L"Shader/Swarm/SwarmSpawnAreaCS.hlsl", "SpawnAreaCS");
    ok &= load(m_AreaTickCS, L"Shader/Swarm/SwarmAreaTickCS.hlsl", "AreaTickCS");
    ok &= load(m_AreaDamageCS, L"Shader/Swarm/SwarmAreaDamageCS.hlsl", "AreaDamageCS");
    ok &= load(m_AreaEmitCS, L"Shader/Swarm/SwarmAreaEmitCS.hlsl", "AreaEmitCS");
    load(m_OrbEmitCS, L"Shader/Swarm/SwarmOrbEmitCS.hlsl", "OrbEmitCS");   // 無くてもオーブの尾が出ないだけ
    load(m_SpriteCS, L"Shader/Swarm/SwarmSpriteCS.hlsl", "SpriteCS");   // 無くても連番画像が出ないだけ
    load(m_LiquidTrackCS, L"Shader/Swarm/SwarmLiquidTrackCS.hlsl", "LiquidTrackCS");   // 無くても液溜まりが出ないだけ
    load(m_CorpseTrackCS, L"Shader/Swarm/SwarmCorpseTrackCS.hlsl", "CorpseTrackCS");   // 無くても死体が砕けないだけ
    load(m_CorpseListCS, L"Shader/Swarm/SwarmCorpseListCS.hlsl", "CorpseListCS");
    ok &= load(m_LightCollectCS, L"Shader/Swarm/SwarmLightCollectCS.hlsl", "LightCollectCS");
    ok &= load(m_AreaLightCollectCS, L"Shader/Swarm/SwarmAreaLightCollectCS.hlsl", "AreaLightCollectCS");
    ok &= load(m_EnemyCompactCS, L"Shader/Swarm/SwarmEnemyCompactCS.hlsl", "EnemyCompactCS");
    ok &= load(m_EnemyBinCS, L"Shader/Swarm/SwarmEnemyBinCS.hlsl", "EnemyBinCS");
    ok &= load(m_EnemyPushCS, L"Shader/Swarm/SwarmEnemyPushCS.hlsl", "EnemyPushCS");
    // ---- デバッグ描画（失敗しても gameplay には影響しない）----
    m_DebugVS = std::make_shared<VertexShader>();
    HRESULT hr = ShaderPath::Load(m_DebugVS.get(), device, L"Shader/Swarm/SwarmDebugProjVS.hlsl");
    std::cout << "[SwarmSystem] DebugProjVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugVS.reset();

    m_DebugPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_DebugPS.get(), device, L"Shader/Swarm/SwarmDebugPS.hlsl");
    std::cout << "[SwarmSystem] DebugPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugPS.reset();

    m_DebugEnemyVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_DebugEnemyVS.get(), device, L"Shader/Swarm/SwarmDebugEnemyVS.hlsl");
    std::cout << "[SwarmSystem] DebugEnemyVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DebugEnemyVS.reset();

    // ---- 雑魚の HP バー（失敗してもバーが出ないだけ）----
    m_HpBarVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_HpBarVS.get(), device, L"Shader/Swarm/SwarmEnemyHpBarVS.hlsl");
    std::cout << "[SwarmSystem] EnemyHpBarVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_HpBarVS.reset();

    m_HpBarPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_HpBarPS.get(), device, L"Shader/Swarm/SwarmEnemyHpBarPS.hlsl");
    std::cout << "[SwarmSystem] EnemyHpBarPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_HpBarPS.reset();

    // ---- 雑魚の足元の丸い影（失敗しても影が出ないだけ）----
    m_BlobShadowVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_BlobShadowVS.get(), device, L"Shader/Swarm/SwarmBlobShadowVS.hlsl");
    std::cout << "[SwarmSystem] BlobShadowVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BlobShadowVS.reset();

    m_BlobShadowPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_BlobShadowPS.get(), device, L"Shader/Swarm/SwarmBlobShadowPS.hlsl");
    std::cout << "[SwarmSystem] BlobShadowPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BlobShadowPS.reset();

    // ---- 自爆兵の警告の輪（失敗しても輪が出ないだけ）----
    m_BomberRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_BomberRingVS.get(), device, L"Shader/Swarm/SwarmBomberRingVS.hlsl");
    std::cout << "[SwarmSystem] BomberRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BomberRingVS.reset();

    m_BomberRingPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_BomberRingPS.get(), device, L"Shader/Swarm/SwarmBomberRingPS.hlsl");
    std::cout << "[SwarmSystem] BomberRingPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_BomberRingPS.reset();

    // ---- 隕石の警告の輪（PS は自爆兵の輪と共用。失敗しても輪が出ないだけ）----
    m_DropRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_DropRingVS.get(), device, L"Shader/Swarm/SwarmDropRingVS.hlsl");
    std::cout << "[SwarmSystem] DropRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_DropRingVS.reset();

    // ---- 地面の警告の輪（Boss のスラム。PS は自爆兵の輪と共用）----
    m_WarnRingVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_WarnRingVS.get(), device, L"Shader/Swarm/SwarmWarnRingVS.hlsl");
    std::cout << "[SwarmSystem] WarnRingVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_WarnRingVS.reset();

    // ---- 範囲の連番画像（失敗しても出ないだけ）----
    m_SpriteVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_SpriteVS.get(), device, L"Shader/Swarm/SwarmSpriteVS.hlsl");
    std::cout << "[SwarmSystem] SpriteVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_SpriteVS.reset();

    m_SpritePS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_SpritePS.get(), device, L"Shader/Swarm/SwarmSpritePS.hlsl");
    std::cout << "[SwarmSystem] SpritePS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_SpritePS.reset();

    // ---- 死んだ敵の砕け散り（PS は雑魚と同じ。失敗しても砕けないだけ）----
    m_CorpseVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_CorpseVS.get(), device, L"Shader/Swarm/SwarmCorpseVS.hlsl");
    std::cout << "[SwarmSystem] CorpseVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_CorpseVS.reset();

    // ---- 液溜まり（Liquid entry。PS は CPU の経路と共用。失敗しても出ないだけ）----
    m_LiquidVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_LiquidVS.get(), device, L"Shader/Swarm/SwarmLiquidVS.hlsl");
    std::cout << "[SwarmSystem] LiquidVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_LiquidVS.reset();

    m_LiquidPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_LiquidPS.get(), device, L"Shader/VFX/VFXLiquidPS.hlsl");
    std::cout << "[SwarmSystem] LiquidPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_LiquidPS.reset();

    // テクスチャ無しの Material は Bind で既定の白を t0 に入れる。その白を用意しておく
    Material::InitDefaultTextures(device);

    // ---- 雑魚の本描画 ----
    m_EnemyVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_EnemyVS.get(), device, L"Shader/Swarm/SwarmEnemyVS.hlsl");
    std::cout << "[SwarmSystem] EnemyVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyVS.reset();

    m_EnemyPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_EnemyPS.get(), device, L"Shader/PS.hlsl");
    std::cout << "[SwarmSystem] EnemyPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyPS.reset();
    // ---- 経験値オーブの本描画 ----
    m_OrbVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_OrbVS.get(), device, L"Shader/Swarm/SwarmOrbVS.hlsl");
    std::cout << "[SwarmSystem] OrbVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_OrbVS.reset();

    m_OrbPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_OrbPS.get(), device, L"Shader/Swarm/SwarmOrbPS.hlsl");
    std::cout << "[SwarmSystem] OrbPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_OrbPS.reset();

    if (m_OrbVS && m_OrbPS)
    {
        m_OrbMaterial = std::make_shared<Material>();
        m_OrbMaterial->SetVertexShader(m_OrbVS);
        m_OrbMaterial->SetPixelShader(m_OrbPS);

        // 見た目だけの大きさ（判定は OrbCB.pickupRadius で別）。
        // 四角の双角錐：横から見ると菱形、自転で面が光ったり陰ったりする。色は VS が orbLook から入れる
        m_OrbModel = PrimitiveBuilder::CreateBipyramid(device, 0.13f, 0.17f, 0.23f, 4);
    }
    if (m_EnemyVS && m_EnemyPS)
    {
        // テクスチャ無し → Bind で既定の白が t0 に入る。色は頂点色で出す
        m_EnemyMaterial = std::make_shared<Material>();
        m_EnemyMaterial->SetVertexShader(m_EnemyVS);
        m_EnemyMaterial->SetPixelShader(m_EnemyPS);

        m_EnemyModel = BuildEnemyModel(device);
        if (!CreateEnemyDrawArgs(device))
        {
            std::cout << "[SwarmSystem] enemy draw args: FAILED (falls back to full-pool DrawInstanced)" << std::endl;
            for (auto& args : m_EnemyDrawArgs) args.clear();
            for (auto& args : m_CorpseDrawArgs) args.clear();   // 砕け散りも描かない
        }
    }

    return ok;
}

// ============================================================
// 雑魚の部品アニメの表
// 部品（submesh）をノードで動かすモデル用。クリップ毎に 30fps でサンプルを取り、
// 「焼いた姿勢の部品 → そのフレームの部品」の差分行列を [クリップ][フレーム][部品] に並べる。
//   焼いた頂点 = v × B（B = 焼いた姿勢の全体変換）
//   そのフレーム = v × G = 焼いた頂点 × (B の逆 × G)   … 行ベクトルの約束
// ループするクリップ（待機・歩き）は終端を含めず、攻撃は 1 回きりなので終端まで取る
// ============================================================
bool SwarmSystem::BuildEnemyPartAnim(ID3D11Device* device, const char* modelPath,
    const char* bakeClip, float bakeFrac, const DirectX::SimpleMath::Matrix& rootTransform,
    size_t partCount)
{
    using namespace DirectX::SimpleMath;
    m_EnemyAnim = EnemyAnimCB{};
    m_PartAnimSRV.Reset();
    m_PartAnimBuffer.Reset();
    if (partCount == 0) return false;

    // 焼いた姿勢の部品毎の全体変換
    std::vector<std::vector<Matrix>> bake;
    if (!Model::SampleSubmeshTransforms(modelPath, bakeClip, { bakeFrac }, rootTransform, bake)
        || bake.empty() || bake[0].size() != partCount)
    {
        std::cout << "[SwarmSystem] part anim: bake pose mismatch (procedural sway)" << std::endl;
        return false;
    }
    std::vector<Matrix> bakeInv(partCount);
    for (size_t p = 0; p < partCount; ++p) bakeInv[p] = bake[0][p].Invert();
    // 砕け散りの回転の中心 = 部品のノードの原点（首・肩・股の関節。網と同じ焼いた姿勢の空間）
    for (size_t p = 0; p < 8; ++p)
    {
        const Vector3 o = (p < partCount) ? bake[0][p].Translation() : Vector3::Zero;
        m_PartPivots[p] = Vector4(o.x, o.y, o.z, 1.0f);
    }

    constexpr float kSampleFps = 30.0f;
    constexpr int   kMaxFrames = 60;
    std::vector<Matrix> table;
    for (int clip = 0; clip < 3; ++clip)
    {
        const char* name = m_AnimClips[clip];
        const bool loop = (clip != 2);

        // 長さだけ先に知りたいので 1 点だけ取る
        float length = 0.0f;
        std::vector<std::vector<Matrix>> probe;
        if (!name || !*name
            || !Model::SampleSubmeshTransforms(modelPath, name, { 0.0f }, rootTransform, probe, &length))
        {
            // 無いクリップは焼いた姿勢（差分 = 単位行列）1 フレームで埋める
            m_EnemyAnim.clipStart[clip] = (uint32_t)(table.size() / partCount);
            m_EnemyAnim.clipFrames[clip] = 1;
            m_EnemyAnim.clipLength[clip] = 1.0f;
            for (size_t p = 0; p < partCount; ++p) table.push_back(Matrix::Identity);
            std::cout << "[SwarmSystem] part anim: clip not found: " << (name ? name : "") << std::endl;
            continue;
        }

        const int frames = (std::max)(2, (std::min)(kMaxFrames, (int)std::lround(length * kSampleFps)));
        std::vector<float> fracs(frames);
        for (int f = 0; f < frames; ++f)
            fracs[f] = loop ? (float)f / (float)frames : (float)f / (float)(frames - 1);

        std::vector<std::vector<Matrix>> samples;
        if (!Model::SampleSubmeshTransforms(modelPath, name, fracs, rootTransform, samples)) return false;

        m_EnemyAnim.clipStart[clip] = (uint32_t)(table.size() / partCount);
        m_EnemyAnim.clipFrames[clip] = (uint32_t)frames;
        m_EnemyAnim.clipLength[clip] = (length > 1e-3f) ? length : 1.0f;
        for (const auto& frame : samples)
        {
            if (frame.size() != partCount) return false;
            for (size_t p = 0; p < partCount; ++p)
                table.push_back(bakeInv[p] * frame[p]);
        }
    }

    // GPU へ（読み取り専用。Matrix は行優先のまま = VS は行ベクトルとして 4 行を読む）
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth = (UINT)(sizeof(Matrix) * table.size());
    bd.Usage = D3D11_USAGE_IMMUTABLE;
    bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bd.StructureByteStride = sizeof(Matrix);
    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = table.data();
    if (FAILED(device->CreateBuffer(&bd, &sd, &m_PartAnimBuffer))) return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srv = {};
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    srv.Buffer.NumElements = (UINT)table.size();
    if (FAILED(device->CreateShaderResourceView(m_PartAnimBuffer.Get(), &srv, &m_PartAnimSRV))) return false;

    m_EnemyAnim.partCount = (uint32_t)partCount;
    m_EnemyAnim.enabled = 1;
    std::cout << "[SwarmSystem] part anim: " << partCount << " parts, frames idle "
        << m_EnemyAnim.clipFrames[0] << " (" << m_EnemyAnim.clipLength[0] << "s) walk "
        << m_EnemyAnim.clipFrames[1] << " (" << m_EnemyAnim.clipLength[1] << "s) attack "
        << m_EnemyAnim.clipFrames[2] << " (" << m_EnemyAnim.clipLength[2] << "s), "
        << table.size() * sizeof(Matrix) / 1024 << " KB" << std::endl;
    return true;
}

// ============================================================
// 雑魚の見た目
// 骨付きモデルを歩きの 1 フレームで焼いて静的メッシュにする。
// 4096 体が同じポーズで、歩きの揺れは VS の procedural。
// 候補を上から試して、読めた物を使う:
//   1) Kenney Blocky の L（緑肌のゾンビ、ピクセルテクスチャ）… 今の雑魚
//   2) KayKit Skeleton_Minion                    … 予備
//   3) カプセル                                   … どちらも読めない時
// 位置合わせ: enemies[].position はカプセル中心（groundY = 半径 + 直線半分）
//   なので足元を -(radius + half) に置く。
// 大きさ: ファイルの単位（cm / m）に頼らず、一度そのまま焼いて測った高さを
//   targetHeight に合わせる（Kenney と KayKit で単位が違っても同じ寸法になる）
// ============================================================
std::shared_ptr<Model> SwarmSystem::BuildEnemyModel(ID3D11Device* device)
{
    using namespace DirectX::SimpleMath;
    const float footY = -(m_CachedAICB.enemyRadius + m_CachedAICB.enemyCapsuleHalf);

    struct EnemyLook
    {
        const char*    model;
        const wchar_t* albedo;
        const char*    clip;          // 焼く歩きのクリップ（無ければ idle）
        const char*    idleClip;
        const char*    attackClip;    // 近接攻撃（部品アニメの表にだけ使う）
        float          yawDeg;        // 正面を +Z に向ける回転
        float          targetHeight;  // m。0 = ファイルの寸法 × kEnemyModelScale のまま
        const char*    label;
        const wchar_t* bomberAlbedo;  // 自爆兵に貼る物（同じメッシュ・同じ UV）。null = 雑魚と同じ
        const wchar_t* splitterAlbedo;// スプリッター・分裂体に貼る物。null = 雑魚と同じ
    };
    const EnemyLook looks[] =
    {
        // Kenney も KayKit と同じく -Z が正面（+Z のままだと背中を向けて歩いた）→ 180 度回す。
        // 高さはカプセル（1.8m）より少し低く
        { Res::Mdl::Kenney_BlockyZombie, Res::Tex::Kenney_BlockyZombieAlbedo,
          "walk", "idle", "attack-melee-right", 180.0f, 1.6f, "Kenney Blocky L (zombie)",
          Res::Tex::Kenney_BlockyRobotAlbedo, Res::Tex::Kenney_BlockyDummyAlbedo },
        // KayKit は Blender 出力の -Z が正面 → 180 度回す
        { Res::Mdl::KayKit_SkeletonMinion, Res::Tex::KayKit_SkeletonAlbedo,
          "Walking_A", "Idle", "", 180.0f, 0.0f, "Skeleton_Minion", nullptr, nullptr },
    };

    // 焼く前の寸法 → 焼く時に掛ける変換。
    // 中心を xz の原点へ、足の裏を 0 へ → 拡縮 → 向き → カプセルの足元へ。
    // 寸法が取れない（0）時は拡縮 kEnemyModelScale、ずらし無し（Minion の従来どおり）
    auto makeXform = [&](const EnemyLook& look, const Vector3& lo, const Vector3& hi, float& outScale)
        {
            const float height = hi.y - lo.y;
            outScale = (look.targetHeight > 0.0f && height > 1e-4f)
                ? look.targetHeight / height : kEnemyModelScale;
            return Matrix::CreateTranslation(-(lo.x + hi.x) * 0.5f, -lo.y, -(lo.z + hi.z) * 0.5f)
                * Matrix::CreateScale(outScale)
                * Matrix::CreateRotationY(DirectX::XMConvertToRadians(look.yawDeg))
                * Matrix::CreateTranslation(0.0f, footY, 0.0f);
        };

    for (const EnemyLook& look : looks)
    {
        auto loaded = ResourceManager::Get().LoadModelAuto(look.model);

        // ---- 骨（skin weights）の無いモデル: 部品をノードで動かす FBX（Kenney Blocky）----
        // LoadModelAuto は Static と判定する。Model::Load に歩きの姿勢と変換を渡して焼く
        if (loaded.kind == ModelKind::Static && loaded.staticModel)
        {
            Model::LoadOptions opt;
            opt.poseClip = look.clip;
            opt.poseTimeFrac = 0.25f;   // 片足が前に出た辺り

            // 一度そのまま読んで寸法を測る（535KB の FBX なので 2 リードバックんでも軽い）
            auto raw = std::make_shared<Model>();
            if (!raw->Load(device, look.model, opt)) continue;
            Vector3 lo = raw->GetBoundsMin();
            Vector3 hi = raw->GetBoundsMax();
            // 足元と高さは待機の姿勢で測る（2026-10-03、ユーザー：Boss の足が地面に埋まっている）。
            // 歩きの 0.25 は両脚を一番開いた所で、最低点が一番高い。それを足元に合わせていたので、
            // 待機・攻撃・歩きの大半で足が 0.29m（1.6m の雑魚で。Boss は 3 倍）地面に埋まっていた。
            // 待機に合わせると立っている時は足が地面に着き、歩きは脚が振り子のように上がる（Kenney の元の動き）
            {
                Model::LoadOptions standOpt;
                standOpt.poseClip = look.idleClip;
                standOpt.poseTimeFrac = 0.0f;
                Model stand;
                if (look.idleClip && *look.idleClip && stand.Load(device, look.model, standOpt)
                    && stand.GetBoundsMax().y - stand.GetBoundsMin().y > 1e-4f)
                {
                    lo = stand.GetBoundsMin();
                    hi = stand.GetBoundsMax();
                }
            }

            float scale = 1.0f;
            opt.rootTransform = makeXform(look, lo, hi, scale);
            auto baked = std::make_shared<Model>();
            if (!baked->Load(device, look.model, opt)) continue;

            m_EnemyMaterial->SetAlbedoTexture(ResourceManager::Get().LoadTexture(look.albedo));
            m_BomberAlbedo = look.bomberAlbedo ? ResourceManager::Get().LoadTexture(look.bomberAlbedo) : nullptr;
            m_SplitterAlbedo = look.splitterAlbedo ? ResourceManager::Get().LoadTexture(look.splitterAlbedo) : nullptr;

            // 部品アニメの表（待機・歩き・近接攻撃）。作れなければ従来の揺れで動く
            m_AnimClips[0] = look.idleClip;
            m_AnimClips[1] = look.clip;
            m_AnimClips[2] = look.attackClip;
            BuildEnemyPartAnim(device, look.model, look.clip, opt.poseTimeFrac,
                opt.rootTransform, baked->GetSubMeshes().size());

            std::cout << "[SwarmSystem] enemy model: " << look.label << " (node pose '" << look.clip
                << "', raw height " << (hi.y - lo.y) << " -> scale " << scale
                << ", baked size " << (baked->GetBoundsMax() - baked->GetBoundsMin()).x << " x "
                << (baked->GetBoundsMax() - baked->GetBoundsMin()).y << " x "
                << (baked->GetBoundsMax() - baked->GetBoundsMin()).z << ")" << std::endl;
            return baked;
        }

        // ---- 骨付きモデル: 1 フレームを CPU スキニングして焼く（KayKit）----
        if (loaded.kind != ModelKind::Skinned || !loaded.skinnedModel) continue;

        const auto& sk = *loaded.skinnedModel;
        int clip = sk.FindClip(look.clip);
        if (clip < 0) clip = sk.FindClip(look.idleClip);
        if (clip < 0 && sk.GetClipCount() > 0) clip = 0;
        const float t = (clip >= 0) ? sk.GetClipDurationSec(clip) * 0.25f : 0.0f;   // 片足が前に出た辺り

        // 一度そのまま焼いて寸法を測る。※BakeStatic は包囲ボックスを持たないので今は 0 が返り、
        //   拡縮は kEnemyModelScale のまま（Minion は targetHeight = 0 なので元々それで良い）
        auto raw = sk.BakeStatic(device, clip, t, Matrix::Identity, {});
        if (!raw) continue;
        float scale = 1.0f;
        const Matrix xform = makeXform(look, raw->GetBoundsMin(), raw->GetBoundsMax(), scale);

        auto baked = sk.BakeStatic(device, clip, t, xform, {});
        if (!baked) continue;

        // テクスチャは雑魚材質の t0 へ（VS/PS は雑魚専用のまま。頂点色は白で焼いてある）
        m_EnemyMaterial->SetAlbedoTexture(ResourceManager::Get().LoadTexture(look.albedo));
        m_BomberAlbedo = look.bomberAlbedo ? ResourceManager::Get().LoadTexture(look.bomberAlbedo) : nullptr;
        m_SplitterAlbedo = look.splitterAlbedo ? ResourceManager::Get().LoadTexture(look.splitterAlbedo) : nullptr;
        std::cout << "[SwarmSystem] enemy model: " << look.label << " (baked, clip "
            << (clip >= 0 ? sk.GetClipName(clip) : std::string("-"))
            << ", scale " << scale << ")" << std::endl;
        return baked;
    }

    std::cout << "[SwarmSystem] enemy model: capsule (fallback)" << std::endl;
    // 衝突体と同じ寸法（AICB.enemyRadius = 0.4 と揃える）
    return PrimitiveBuilder::CreateCapsule(device, m_CachedAICB.enemyRadius,
        m_CachedAICB.enemyCapsuleHalf * 2.0f, { 0.85f, 0.25f, 0.25f, 1.0f });
}

// ============================================================
// 雑魚描画の間接引数
//   aliveList: 活きスロット番号（CompactCS が毎フレーム append）。HP バー用
//   kindList : 同じく種類毎（雑魚 / 自爆兵）。本描画は種類毎に 1 回
//   args[k][i]: DrawIndexedInstancedIndirect の 5 uint。IndexCount は submesh 固有、
//              InstanceCount は種類 k の一覧から CopyStructureCount で毎フレーム上書き
// ============================================================
bool SwarmSystem::CreateEnemyDrawArgs(ID3D11Device* device)
{
    if (!m_EnemyModel) return false;

    // 活きスロットの一覧（append）。全種類 1 本 + 種類毎
    auto makeList = [&](ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11UnorderedAccessView>& uav,
        ComPtr<ID3D11ShaderResourceView>& srv) -> bool
        {
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = sizeof(uint32_t) * Swarm::kMaxEnemies;
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = sizeof(uint32_t);
            if (FAILED(device->CreateBuffer(&bd, nullptr, &buf))) return false;

            D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
            ud.Format = DXGI_FORMAT_UNKNOWN;
            ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            ud.Buffer.NumElements = Swarm::kMaxEnemies;
            ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_APPEND;
            if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &ud, &uav))) return false;

            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = Swarm::kMaxEnemies;
            return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sd, &srv));
        };
    if (!makeList(m_AliveListBuffer, m_AliveListUAV, m_AliveListSRV)) return false;
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        if (!makeList(m_KindListBuffer[k], m_KindListUAV[k], m_KindListSRV[k])) return false;
    // 砕け散り中の尸の一覧（見た目毎。大きさは敵と同じで足りる）
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
        if (!makeList(m_CorpseListBuffer[k], m_CorpseListUAV[k], m_CorpseListSRV[k])) return false;

    // submesh 毎の間接引数（IndexCount が違う）。生きている敵と尸で別々に持つ
    auto makeDrawArgs = [&](std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>>& out) -> bool
        {
            out.clear();
            for (const auto& sub : m_EnemyModel->GetSubMeshes())
            {
                if (!sub.mesh) { out.emplace_back(); continue; }

                D3D11_BUFFER_DESC desc = {};
                desc.ByteWidth = sizeof(uint32_t) * 5;
                desc.Usage = D3D11_USAGE_DEFAULT;
                desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
                desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
                // IndexCountPerInstance, InstanceCount, StartIndex, BaseVertex, StartInstance
                const uint32_t init[5] = { sub.mesh->GetIndexCount(), 0, 0, 0, 0 };
                D3D11_SUBRESOURCE_DATA sd = {};
                sd.pSysMem = init;
                Microsoft::WRL::ComPtr<ID3D11Buffer> args;
                if (FAILED(device->CreateBuffer(&desc, &sd, &args))) return false;
                out.push_back(args);
            }
            return true;
        };
    for (uint32_t k = 0; k < Swarm::kEnemyKinds; ++k)
    {
        if (!makeDrawArgs(m_EnemyDrawArgs[k])) return false;
        if (!makeDrawArgs(m_CorpseDrawArgs[k])) return false;
    }

    // HP バー: VertexCountPerInstance, InstanceCount, StartVertex, StartInstance
    {
        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth = sizeof(uint32_t) * 4;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        desc.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS;
        const uint32_t init[4] = { 6, 0, 0, 0 };
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = init;
        if (FAILED(device->CreateBuffer(&desc, &sd, &m_HpBarArgs))) return false;
        // 自爆兵の警告の輪も同じ形（InstanceCount は自爆兵の一覧から）
        if (FAILED(device->CreateBuffer(&desc, &sd, &m_BomberRingArgs))) return false;
    }
    return true;
}

// ============================================================
// TEMP-TEST: 敵の池と状態を丸ごと読み戻す（staging を作って CopyResource → Map。GPU を待つので自動テストだけ）
// ============================================================
bool SwarmSystem::DebugReadEnemies(std::vector<Swarm::Enemy>& outEnemies, std::vector<uint32_t>& outStates,
    std::vector<Swarm::EnemyExtra>* outExtras)
{
    if (!m_Device || !m_Context || !m_EnemyBuffer || !m_EnemyStateBuffer) return false;

    auto readback = [&](ID3D11Buffer* src, UINT bytes, void* dst) -> bool
        {
            D3D11_BUFFER_DESC sd = {};
            sd.ByteWidth = bytes;
            sd.Usage = D3D11_USAGE_STAGING;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Buffer> staging;
            if (FAILED(m_Device->CreateBuffer(&sd, nullptr, &staging))) return false;
            m_Context->CopyResource(staging.Get(), src);
            D3D11_MAPPED_SUBRESOURCE m = {};
            if (FAILED(m_Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) return false;
            memcpy(dst, m.pData, bytes);
            m_Context->Unmap(staging.Get(), 0);
            return true;
        };

    outEnemies.resize(Swarm::kMaxEnemies);
    outStates.resize(Swarm::kMaxEnemies);
    if (!readback(m_EnemyBuffer.Get(), (UINT)(sizeof(Swarm::Enemy) * Swarm::kMaxEnemies), outEnemies.data())) return false;
    if (!readback(m_EnemyStateBuffer.Get(), (UINT)(sizeof(uint32_t) * Swarm::kMaxEnemies), outStates.data())) return false;
    // 種類（並行バッファ）。無ければ空のまま（呼ぶ側は全部雑魚として扱う）
    if (outExtras && m_EnemyExtraBuffer)
    {
        outExtras->resize(Swarm::kMaxEnemies);
        if (!readback(m_EnemyExtraBuffer.Get(), (UINT)(sizeof(Swarm::EnemyExtra) * Swarm::kMaxEnemies), outExtras->data()))
            outExtras->clear();
    }
    return true;
}
