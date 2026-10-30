// ============================================================
// SwarmSystemClimb.cpp
// 雑魚が壁を登る（2026-10-29、ユーザー「全部の雑魚が壁を登れるように。でないと無敵の場所ができる」）。
//   雑魚用の通行表・高さ場：GridWorld の登れる障害物（World/ClimbMap が地形の記録から作る：場内の岩・木の幹・遺跡の部屋の壁・
//   岩柱）のマスは通れて、その細かいマスの高さ = 障害物の上面。雑魚の AI / 積分 / 押し合いだけがこれを見る
//   （弾・経験球・湧く所・地面の描画は普通の物 = 弾は今まで通り岩で止まる）。
//   流れ場（FlowField）もこの格子で作る：上りの崖は「登る」として通れ、登る高さの分だけ遠回りとみなす（allowClimb / climbCostPerMeter）。
//   登る動き自体は SwarmEnemyMoveCS（壁に貼り付いて g_ClimbSpeed で上がり、上に着いたら面の上へ出る）
// ============================================================
#include "Swarm/SwarmSystem.h"
#include "World/GridWorld.h"

using Microsoft::WRL::ComPtr;

void SwarmSystem::UploadMobTerrain(const GridWorld& grid)
{
    const int w = grid.Width(), d = grid.Depth();
    if (!m_Device || w <= 0 || d <= 0) return;

    // ---- マス：通れるか（普通に歩けるマス + 登れる障害物）・中心の高さ（流れ場の段差と坂の代価）----
    std::vector<uint32_t> walkGpu((size_t)w * d);
    std::vector<uint8_t> walk((size_t)w * d);
    std::vector<float> hgt((size_t)w * d);
    for (int z = 0; z < d; ++z)
        for (int x = 0; x < w; ++x)
        {
            const size_t i = (size_t)z * w + x;
            walk[i] = grid.IsMobWalkable(x, z) ? 1 : 0;
            walkGpu[i] = walk[i];
            const auto c = grid.CellToWorld(x, z);
            hgt[i] = grid.SampleMobHeight(c.x, c.z);
        }
    // ---- 細かい高さ場：地面と障害物の上面の高い方 ----
    const auto& base = grid.Heights();
    std::vector<float> fine(base.size());
    for (size_t i = 0; i < fine.size(); ++i) fine[i] = grid.MobHeight(i);

    auto makeImmutable = [&](const void* data, UINT stride, UINT count, ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv)
        {
            buf.Reset();
            srv.Reset();
            if (count == 0) return;
            D3D11_BUFFER_DESC bd = {};
            bd.ByteWidth = stride * count;
            bd.Usage = D3D11_USAGE_IMMUTABLE;
            bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bd.StructureByteStride = stride;
            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem = data;
            D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
            sd.Format = DXGI_FORMAT_UNKNOWN;
            sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            sd.Buffer.NumElements = count;
            if (FAILED(m_Device->CreateBuffer(&bd, &init, &buf)) || FAILED(m_Device->CreateShaderResourceView(buf.Get(), &sd, &srv)))
            {
                std::cout << "[Error] SwarmSystem: mob terrain buffer failed" << std::endl;
                buf.Reset();
                srv.Reset();
            }
        };
    makeImmutable(walkGpu.data(), sizeof(uint32_t), (UINT)walkGpu.size(), m_MobTerrainBuffer, m_MobTerrainSRV);
    makeImmutable(fine.data(), sizeof(float), (UINT)fine.size(), m_MobHeightBuffer, m_MobHeightSRV);

    WaitFlowJob();   // 古い格子で作っている途中の物は捨てる
    m_Flow.SetGrid(w, d, walk, hgt, fine, GridWorld::kHeightSub);
    m_FlowWorker.SetGrid(w, d, walk, hgt, fine, GridWorld::kHeightSub);
    m_FlowRequestX = m_FlowRequestZ = -1;
}
