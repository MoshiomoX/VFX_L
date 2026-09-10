// ============================================================
// SwarmSystem.cpp
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Graphics/Shader/ShaderPath.h"
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

    // ---- counter（RAW UAV。CS が InterlockedAdd し、CopyResource で staging へ）----
    {
        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = sizeof(SwarmCounters);
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
        bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

        if (FAILED(device->CreateBuffer(&bd, nullptr, &m_CounterBuffer))) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
        ud.Format = DXGI_FORMAT_R32_TYPELESS;
        ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
        ud.Buffer.NumElements = sizeof(SwarmCounters) / 4;

        if (FAILED(device->CreateUnorderedAccessView(
            m_CounterBuffer.Get(), &ud, &m_CounterUAV))) return false;
    }

    // ---- 定数バッファ ----
    // ※ComputeShader::WriteBuffer が反射から自前の CB を持つ場合は未使用。
    //   将来 VS 側（描画）で直接使うので残しておく
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
// 生成依頼を溜める（実際に GPU へ入るのは Flush）
// ※state は本体に無い。スロットの生死は SpawnCS が
//   state buffer 側で InterlockedCompareExchange して立てる
// ============================================================
void SwarmSystem::SpawnEnemy(const Vector3& pos, float hp, float moveSpeed)
{
    if (m_PendingEnemies.size() >= Swarm::kMaxSpawnEnemyPerFrame) return;

    Swarm::Enemy e;
    e.position = pos;
    e.hp = hp;
    e.velocity = { 0, 0, 0 };
    e.moveSpeed = moveSpeed;
    e.yaw = 0.0f;
    m_PendingEnemies.push_back(e);
}

void SwarmSystem::SpawnProjectile(const Vector3& pos, const Vector3& vel,
    float damage, float radius, float lifetime)
{
    if (m_PendingProjectiles.size() >= Swarm::kMaxSpawnProjPerFrame) return;

    Swarm::Projectile p;
    p.position = pos;
    p.damage = damage;
    p.velocity = vel;
    p.lifetime = lifetime;
    p.radius = radius;
    m_PendingProjectiles.push_back(p);
    ++m_TotalRequested;
}

// ============================================================
// Flush
// 粒子の Flush と同じ位置（UpdateGameplay の末尾）で呼ぶ。
// 違いは中で固定ステップを回すこと
// ============================================================
void SwarmSystem::Flush(const Vector3& playerPos, float playerRadius,
    bool playerAlive, float dt)
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

    // ---- 4) counter の写しを発行（CopyResource だけ。待たない）----
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

    // ---- 敵（Phase 3 で SpawnEnemyCS を書いたらここに同じ形で）----

    m_PendingEnemies.clear();
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

    // ---- 1) 投射物の積分 ----
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

    // Phase 3: m_EnemyAICS
    // Phase 4: m_HitCS / m_ContactCS / m_OrbCS
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
    return ok;
}

// ============================================================
// 描画
// ※Phase 2 の残り。UAV と SRV は同時に繋げないので、
//   Flush の側で外し、ここで SRV として繋ぎ直す
// ============================================================
void SwarmSystem::Render(CameraBase* camera)
{
    (void)camera;
}