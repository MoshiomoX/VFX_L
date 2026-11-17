// ============================================================
// SwarmSystemTerrain.cpp
// 通行表・高さ場のアップロード、フローフィールド（裏のスレッドで作る）。
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
// 地形の格子表を上げる（起動時と Regenerate の時だけ）
// ============================================================
// ============================================================
// 通行マップだけを上げ直す。箱・門の柱は GPU の雑魚には格子でしか見えないので、
// シーンがその下のマスを塞いだ / 箱が開いて戻した時に呼ぶ
// ============================================================
void SwarmSystem::RefreshWalkable(const GridWorld& grid)
{
    const int w = grid.Width();
    const int d = grid.Depth();
    if (!m_Device || w <= 0 || d <= 0) return;
    // 格子の寸法が変わった（まだ上げていない時も）：空間ハッシュ・高さ場・向き表の buffer と CS の格子の定数が
    // 合わなくなる（向き表の Map は古い大きさを越えて書く）ので、全部作り直す
    if ((uint32_t)w != m_CachedFrameCB.gridW || (uint32_t)d != m_CachedFrameCB.gridD)
    {
        UploadTerrain(grid);
        return;
    }

    std::vector<uint32_t> data((size_t)w * d);
    for (int z = 0; z < d; ++z)
        for (int x = 0; x < w; ++x)
            data[(size_t)z * w + x] = grid.IsWalkable(x, z) ? 1u : 0u;

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

    // 雑魚用の通行表・高さ場（登れる障害物入り）と流れ場（SwarmSystemClimb.cpp）
    UploadMobTerrain(grid);
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
    // 解像度は GridWorld::kHeightSub = SwarmTerrain.hlsli の SWARM_HEIGHT_SUB
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

    // ---- 巡路: 雑魚用の通行表・高さ場（登れる障害物入り。SwarmSystemClimb.cpp）を GPU と流れ場へ、向き表の buffer を作る ----
    {
        UploadMobTerrain(grid);

        m_FlowSRV.Reset();
        m_FlowBuffer.Reset();
        D3D11_BUFFER_DESC fb = {};
        fb.ByteWidth = (UINT)(sizeof(float) * 2 * w * d);
        fb.Usage = D3D11_USAGE_DYNAMIC;
        fb.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        fb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        fb.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        fb.StructureByteStride = sizeof(float) * 2;
        // 最初の作り直しが終わるまで AI が読むので 0（向き無し）で始める
        const std::vector<Vector2> flowZero((size_t)w * d, Vector2::Zero);
        D3D11_SUBRESOURCE_DATA fi = {};
        fi.pSysMem = flowZero.data();
        if (SUCCEEDED(m_Device->CreateBuffer(&fb, &fi, &m_FlowBuffer)))
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
                // buffer の大きさまでしか書かない（格子の寸法が変わった時に溢れない）
                D3D11_BUFFER_DESC fd = {};
                m_FlowBuffer->GetDesc(&fd);
                const auto& dirs = m_Flow.Directions();
                const size_t bytes = (std::min)(sizeof(Vector2) * dirs.size(), (size_t)fd.ByteWidth);
                memcpy(mapped.pData, dirs.data(), bytes);
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
