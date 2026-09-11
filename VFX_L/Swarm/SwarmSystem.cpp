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
#include "Camera/CameraBase.h"
#include "Graphics/Model/Model.h"
#include "Graphics/Material/Material.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Shader/PixelShader.h"
#include "World/GridWorld.h"
#include <chrono>
#include <iostream>

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

    if (!makeStructured(sizeof(Swarm::Projectile), Swarm::kMaxProjectiles,
        m_ProjBuffer, m_ProjUAV, m_ProjSRV, "projectile")) return false;

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
        m_Context->ClearUnorderedAccessViewUint(m_OrbStateUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_CounterUAV.Get(), zero);
        m_Context->ClearUnorderedAccessViewUint(m_EmitBudgetUAV.Get(), zero);
    }

    return true;
}

// ============================================================
// 地形の格子表を上げる（起動時と Regenerate の時だけ）
// ============================================================
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

    // CS が格子を引くのに要る値も控えておく
    m_CachedFrameCB.gridOrigin = { grid.OriginX(), 0.0f, grid.OriginZ() };
    m_CachedFrameCB.cellSize = GridWorld::kCellSize;
    m_CachedFrameCB.gridW = (uint32_t)w;
    m_CachedFrameCB.gridD = (uint32_t)d;

    std::cout << "[Swarm] terrain uploaded: " << w << "x" << d << std::endl;
}

// ============================================================
// VFX 配方表の構築（起動時に1回）
// ============================================================
bool SwarmSystem::BuildVFXTable()
{
    return m_VFX.Build(m_Device, m_Particles);
}

// ============================================================
// 生成依頼を溜める（実際に GPU へ入るのは Flush）
// ※state は本体に無い。スロットの生死は SpawnCS が
//   state buffer 側で InterlockedCompareExchange して立てる
// ============================================================
void SwarmSystem::SpawnEnemy(const Vector3& pos, float hp, float moveSpeed)
{
    if (m_PendingEnemies.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;

    Swarm::Enemy e;
    e.position = pos;
    e.hp = Swarm::HpToFixed(hp);
    e.velocity = { 0, 0, 0 };
    e.moveSpeed = moveSpeed;
    e.yaw = 0.0f;
    m_PendingEnemies.push_back(e);
}
void SwarmSystem::RecycleEnemy(const Vector3& pos, float hp, float moveSpeed)
{
    if (m_PendingRecycles.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;
    Swarm::Enemy e = {};
    e.position = pos;
    e.hp = Swarm::HpToFixed(hp);
    e.moveSpeed = moveSpeed;
    m_PendingRecycles.push_back(e);
}
void SwarmSystem::SpawnProjectile(VFXId vfx, const Vector3& pos, const Vector3& vel,
    float damage, float radius, float lifetime)
{
    if (m_PendingProjectiles.size() >= Swarm::kMaxSpawnProjPerFrame) return;

    Swarm::Projectile p;
    p.position = pos;
    p.damage = damage;
    p.velocity = vel;
    p.lifetime = lifetime;
    p.radius = radius;
    p.vfxType = m_VFX.IndexOf(vfx);
    m_PendingProjectiles.push_back(p);
    ++m_TotalRequested;
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

    // ============================================================
    // 1) 前フレームの counter を読む（阻塞しない）
    // killCount / playerDamage は GPU 上で永久に累加される。
    // 前回値との差分を取る（回読が失敗したフレームがあっても取りこぼさない）
    // ============================================================
    SwarmCounters c;
    if (m_Readback.TryRead(m_Context, c))
    {
        const uint32_t dmgDelta = c.playerDamage - m_LastDamageTotal;
        m_LastDamageTotal = c.playerDamage;
        m_PendingPlayerDamage += (float)dmgDelta * 0.01f;   // 固定小数 × 100 を戻す

        m_LastKillCount = c.killCount;
    }

    // ---- 2) 定数と生成依頼を上げる ----
    UploadFrameCB(playerPos, playerRadius, playerAlive);
    UploadSpawns();

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

    // ---- 5) counter の写しを発行（CopyResource だけ。待たない）----
    RequestReadback();

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

        // 2) 生成用の定数
        Swarm::SpawnCB scb;
        scb.requestCount = (uint32_t)m_PendingProjectiles.size();
        scb.scanStart = m_FrameSeed * 613u;   // 毎フレーム探索開始位置をずらす

        m_SpawnProjCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_SpawnProjCS->WriteBuffer(m_Context, 1, &scb);

        // 3) dispatch
        m_SpawnProjCS->Bind(m_Context);
        m_SpawnProjCS->SetSRV(m_Context, "spawnRequests", m_SpawnProjSRV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projectiles", m_ProjUAV.Get());
        m_SpawnProjCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_SpawnProjCS->BindUAVs(m_Context);

        m_Context->Dispatch((scb.requestCount + 63) / 64, 1, 1);

        m_SpawnProjCS->UnbindSRVs(m_Context);
        m_SpawnProjCS->UnbindUAVs(m_Context);

        ++m_TotalDispatched;
    }
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
            m_RecycleCS->BindUAVs(m_Context);

            m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

            m_RecycleCS->UnbindSRVs(m_Context);
            m_RecycleCS->UnbindUAVs(m_Context);
        }
    }

    m_PendingEnemies.clear();
    m_PendingRecycles.clear();
    m_PendingProjectiles.clear();
}


// ============================================================
// 固定ステップ1回ぶんの CS 群
// 順序はここが全て:
//   counter 清零 → 敵AI → 投射物積分 → 命中 → 接触 → オーブ
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
    // ---- 1) 雑魚 AI: 速度を決める ----
    // position は読むだけ。全スレッドが同じ快照を見るために
    // 積分は次の dispatch に分けてある
    if (m_EnemyAICS)
    {
        m_EnemyAICS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_EnemyAICS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_EnemyAICS->Bind(m_Context);
        m_EnemyAICS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyAICS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
        m_EnemyAICS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
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
        m_EnemyMoveCS->Bind(m_Context);
        m_EnemyMoveCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_EnemyMoveCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_EnemyMoveCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_EnemyMoveCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_EnemyMoveCS->UnbindSRVs(m_Context);
        m_EnemyMoveCS->UnbindUAVs(m_Context);
    }
    // ---- 3) 投射物の積分 ----
    if (m_ProjMoveCS)
    {
        m_ProjMoveCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_ProjMoveCS->Bind(m_Context);
        m_ProjMoveCS->SetSRV(m_Context, "terrain", m_TerrainSRV.Get());
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
        m_HitCS->Bind(m_Context);
        m_HitCS->SetSRV(m_Context, "projectiles", m_ProjSRV.Get());
        m_HitCS->SetUAV(m_Context, "projStates", m_ProjStateUAV.Get());
        m_HitCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_HitCS->SetUAV(m_Context, "enemyStates", m_EnemyStateUAV.Get());
        m_HitCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_HitCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxProjectiles + 255) / 256, 1, 1);

        m_HitCS->UnbindSRVs(m_Context);
        m_HitCS->UnbindUAVs(m_Context);
    }
    // ---- 5) Hit            弹 × 怪
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
    // ---- 6) 接触: 雑魚 × 玩家 ----
   // ダメージは counter に固定小数で累加。無敵時間の判定は CPU の状態機
    if (m_ContactCS)
    {
        m_ContactCS->WriteBuffer(m_Context, 0, &m_CachedFrameCB);
        m_ContactCS->WriteBuffer(m_Context, 1, &m_CachedAICB);
        m_ContactCS->Bind(m_Context);
        m_ContactCS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());
        m_ContactCS->SetUAV(m_Context, "enemies", m_EnemyUAV.Get());
        m_ContactCS->SetUAV(m_Context, "counters", m_CounterUAV.Get());
        m_ContactCS->BindUAVs(m_Context);

        m_Context->Dispatch((Swarm::kMaxEnemies + 255) / 256, 1, 1);

        m_ContactCS->UnbindSRVs(m_Context);
        m_ContactCS->UnbindUAVs(m_Context);
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

    // ※WriteBuffer の index は反射に出た cbuffer の順。
    //   DeadListCB（b1）はこの CS で未参照なので反射に出ず、
    //   SwarmFrameCB（b2）が index 1 になる想定。
    //   起動時の reflection 出力で確認し、ずれていれば index を直す
    m_EmitCS->WriteBuffer(m_Context, 0, &gcb);
   // m_EmitCS->WriteBuffer(m_Context, 1, &m_CachedFrameCB);
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
}

// ============================================================
// counter の写しを発行（待たない）
// ============================================================
void SwarmSystem::RequestReadback()
{
    m_Readback.RequestCopy(m_Context, m_CounterBuffer.Get());
}

// ============================================================
// 玩家の被弾を取り出す（取ったら 0 に戻す）
// ============================================================
float SwarmSystem::ConsumePlayerDamage()
{
    const float d = m_PendingPlayerDamage;
    m_PendingPlayerDamage = 0.0f;
    return d;
}
// ============================================================
// デバッグ: 判定球の線框
// 全スロットを DrawInstanced し、死んだ物は VS 側で裁剪外へ畳む。
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
// 雑魚の本描画
// 頂点/インデックスは1体分、インスタンス数 = プール全体。
// 生死は VS が state を見て判断する（死んだ槽は near 面の外へ畳む）
// ============================================================
void SwarmSystem::Render(CameraBase* camera, const LightBuffer& light)
{
    if (!camera || !m_EnemyMaterial || !m_EnemyModel) return;

    // VS/PS/既定テクスチャをまとめて bind
    m_EnemyMaterial->Bind(m_Context);
    ID3D11SamplerState* samp = RenderStates::Get().LinearWrap();
    m_Context->PSSetSamplers(0, 1, &samp);

    // VS b0: VS.hlsl と同じ row_major なので Transpose しない
    EnemyRenderCB cb;
    cb.view = camera->GetViewMatrix();
    cb.proj = camera->GetProjectionMatrix();
    m_EnemyVS->WriteBuffer(m_Context, 0, &cb);

    // PS b0: 光。cameraPosition は Renderer が DrawMesh の中でしか詰めないので自分で入れる
    LightBuffer l = light;
    l.cameraPosition = camera->GetPosition();
    m_EnemyPS->WriteBuffer(m_Context, 0, &l);

    m_EnemyVS->SetSRV(m_Context, "enemies", m_EnemySRV.Get());
    m_EnemyVS->SetSRV(m_Context, "enemyStates", m_EnemyStateSRV.Get());

    for (const auto& sub : m_EnemyModel->GetSubMeshes())
    {
        if (sub.mesh)
            sub.mesh->DrawInstanced(m_Context, Swarm::kMaxEnemies);
    }

    // 次のフレームの Compute が UAV として使うので必ず外す
    m_EnemyVS->UnbindSRVs(m_Context);
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
    ok &= load(m_EmitCS, L"Shader/Swarm/SwarmEmitCS.hlsl", "EmitCS");
    ok &= load(m_SpawnEnemyCS, L"Shader/Swarm/SwarmSpawnEnemyCS.hlsl", "SpawnEnemyCS");
    ok &= load(m_EnemyAICS, L"Shader/Swarm/SwarmEnemyAICS.hlsl", "EnemyAICS");
    ok &= load(m_EnemyMoveCS, L"Shader/Swarm/SwarmEnemyMoveCS.hlsl", "EnemyMoveCS");
    ok &= load(m_RecycleCS, L"Shader/Swarm/SwarmRecycleCS.hlsl", "RecycleCS");
    ok &= load(m_HitCS, L"Shader/Swarm/SwarmHitCS.hlsl", "HitCS");
    ok &= load(m_AimResolveCS, L"Shader/Swarm/SwarmAimResolveCS.hlsl", "AimResolveCS");
    ok &= load(m_ContactCS, L"Shader/Swarm/SwarmContactCS.hlsl", "ContactCS");
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
    // ---- 雑魚の本描画 ----
    m_EnemyVS = std::make_shared<VertexShader>();
    hr = ShaderPath::Load(m_EnemyVS.get(), device, L"Shader/Swarm/SwarmEnemyVS.hlsl");
    std::cout << "[SwarmSystem] EnemyVS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyVS.reset();

    m_EnemyPS = std::make_shared<PixelShader>();
    hr = ShaderPath::Load(m_EnemyPS.get(), device, L"Shader/PS.hlsl");
    std::cout << "[SwarmSystem] EnemyPS: " << (SUCCEEDED(hr) ? "OK" : "FAILED") << std::endl;
    if (FAILED(hr)) m_EnemyPS.reset();

    if (m_EnemyVS && m_EnemyPS)
    {
        // テクスチャ無し → Bind で既定の白が t0 に入る。色は頂点色で出す
        Material::InitDefaultTextures(device);
        m_EnemyMaterial = std::make_shared<Material>();
        m_EnemyMaterial->SetVertexShader(m_EnemyVS);
        m_EnemyMaterial->SetPixelShader(m_EnemyPS);

        // 衝突体と同じ寸法（AICB.enemyRadius = 0.4 と揃える）
        m_EnemyModel = PrimitiveBuilder::CreateCapsule(device, 0.4f, 1.0f,
            { 0.85f, 0.25f, 0.25f, 1.0f });
    }

    return ok;
}