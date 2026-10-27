// ============================================================
// CollisionTestSceneField.cpp
// CollisionTestScene のフィールドの物（2026-10-25 に CollisionTestScene.cpp から分けた）:
//   世界の風を配る・突風の風源・報酬の箱 / Boss の門 / 磁石の置き直し・置物の下と袋小路のマスを雑魚用に塞ぐ
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/InteractableComponent.h"
#include "VFX_Editor/VFXId.h"

// ============================================================
// 世界の風（2026-10-12）：WeatherSystem が作った 1 つの風を、草・マント・粒子へ配る。
// 以前は 3 か所がそれぞれ別の向き・強さを持っていた
// ============================================================
void CollisionTestScene::PushWind()
{
    // 天候の Update は粒子の Flush の後なので、ここで配るのは 1 フレーム前の風（風はゆっくりしか変わらない）
    const WindState& wind = m_Weather.Wind();
    m_Grass.SetWind(wind);
    m_ClothChainSystem.wind = wind;
    m_ClothChainSystem.windTime = m_Grass.Time();
    m_ClothChainSystem.localWind = &m_Swarm.GetLocalWind();   // 竜巻・渦・砂旋・突風にマントも巻かれる（2026-10-22）
    m_ParticleSystem.SetWind(wind, m_Grass.Time());
    m_ParticleSystem.SetWindSources(m_Swarm.GetWindSourceSRV());   // 砂旋の風源（煙が周りを回る。2026-10-19）
    m_Debris.SetWind(wind, m_Grass.Time());   // 落ち葉（強い風で舞う）
    m_Dandelions.SetWind(wind, m_Grass.Time());   // タンポポ（強い風で綿毛がほどける。2026-10-22）
    // 水面（2026-10-27）：草と同じ 2 つの突風の波で、風の強い所だけさざ波が立つ。雨 = 雨粒の波紋、砂嵐 = 濁り
    {
        const auto& gs = m_Grass.GetSettings();
        const float a = DirectX::XMConvertToRadians(gs.wave2Angle);
        const Vector2 d(wind.dir.x * std::cos(a) - wind.dir.y * std::sin(a), wind.dir.x * std::sin(a) + wind.dir.y * std::cos(a));
        m_Water.SetWind(wind, m_Grass.Time(),
            DirectX::SimpleMath::Vector4(d.x, d.y, wind.waveScale * gs.wave2ScaleMul, wind.waveSpeed * gs.wave2SpeedMul),
            std::clamp(gs.wave2Weight, 0.0f, 1.0f));
        const bool sand = m_Weather.CurrentKind() == WeatherSystem::Kind::Sandstorm;
        m_Water.SetWeather(m_Weather.RainIntensity(), sand ? m_Weather.Intensity() : 0.0f);
        m_Water.SetSky(m_Lighting.SkyZenith(), m_Lighting.SkyHorizon());
    }

    // 草の火：風下へ広がる、雨で消える。燃えているマスからの粒子は SwarmSystem の Flush が出す
    m_GrassFire.SetWind(wind);
    m_GrassFire.SetRain(m_Weather.RainIntensity());
    GrassFireEmitInput fire;
    CameraBase* cam = GetCamera();
    if (cam && m_GrassFire.FillEmitInput(fire, cam->GetPosition(), m_Grass.GetGroundSRV(), m_Grass.GetHeightSRV()))
        m_Swarm.SetGrassFire(&fire);
    else
        m_Swarm.SetGrassFire(nullptr);
}

// ============================================================
// 突風を風源にする（2026-10-20、エルデンリングの嵐系の質感）：渦の中心は身前 0.9m、
// 届く距離 = 範囲の半径（突風 3.4m）、縁の接線 11 m/s + 芯の上昇 3.5 m/s + 吸い込み 1.2 m/s + 風下へ 11 m/s の押し、
// 0.8 秒（sin で出入り）。草・落ち葉・煙・風を受ける粒子が同じ風に巻かれる。向き無し（円盤）なら押さない
// ============================================================
void CollisionTestScene::AddGustWindBurst(const Vector3& pos, const Vector2& dir, float length, float radius)
{
    SwarmSystem::WindBurst b;
    const bool hasDir = dir.LengthSquared() > 1e-4f;
    b.pos = hasDir ? pos + Vector3(dir.x, 0.0f, dir.y) * 0.9f : pos;
    // 10-20：筒 / 大きな板の粒子を無くし、風は「屈折 + 薄い白」と環境の反応だけに → 風源を強く・長く（2.6 / 9 / 2.5 / 0.8 / 7 / 0.45 秒 → 下）
    // 前へ吹く物の届く距離 = 範囲の半径（突風 3.4m。拡大鏡で広がれば一緒に。2026-10-22）
    b.radius = hasDir ? radius : (std::max)(length, 2.0f);
    b.swirl = 11.0f;
    b.updraft = 3.5f;
    b.inflow = 1.2f;
    b.dir = hasDir ? dir : Vector2::Zero;
    b.push = hasDir ? 11.0f : 0.0f;
    b.life = 0.8f;
    m_Swarm.AddWindBurst(b);
}

void CollisionTestScene::RespawnCrates()
{
    const Vector3* p = PlayerPos();
    // 三層のフィールドでは山頂・洞窟にも箱を置く（登る・潜るご褒美）
    // 地図に置いてある機能付きの置き物（MapData::Placement、地図エディタ）。ある種類はその通りに、無い種類は seed から
    std::vector<DirectX::SimpleMath::Vector4> fixedCrates;
    DirectX::SimpleMath::Vector4 fixedGate;
    bool hasFixedGate = false;
    for (const auto& pl : m_TerrainMap.placements)
    {
        const DirectX::SimpleMath::Vector4 v(pl.pos.x, pl.pos.y, pl.pos.z, pl.yawDeg);
        if (pl.type == MapData::kPlaceBossGate) { fixedGate = v; hasFixedGate = true; }
        else fixedCrates.push_back(v);
    }
    m_Crates.Spawn(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction,
        &m_TerrainLayout.summitCells, &m_TerrainLayout.mineCells, &fixedCrates);
    // Boss を呼ぶ門・磁石も同じ時に置き直す（地形が変わると前の場所は歩けないかもしれない）。
    // 門は洞窟の一番奥（無ければ従来通りプレイヤーの周り）。面は洞窟の真ん中の方（洞の中から近づく）
    Vector3 mineMid;
    for (int i : m_TerrainLayout.mineCells) mineMid += m_Grid.CellToWorld(i % m_Grid.Width(), i / m_Grid.Width());
    const bool hasMine = !m_TerrainLayout.mineCells.empty();
    if (hasMine) mineMid /= (float)m_TerrainLayout.mineCells.size();
    m_Stage.SpawnPortal(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed, m_Interaction,
        m_TerrainLayout.hasMineDeep ? &m_TerrainLayout.mineDeep : nullptr, hasMine ? &mineMid : nullptr,
        hasFixedGate ? &fixedGate : nullptr);
    // 石の拱の間の渦（粒子。門の向きに回す）。Boss を呼ぶと UpdateGameplay が止める
    if (m_PortalVfx) m_AreaVFX.StopInstance(m_PortalVfx);
    m_PortalVfx = 0;
    if (m_Registry.IsValid(m_Stage.GetPortal()))
    {
        m_PortalVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossPortal), m_Stage.GetPortalCenter(), 1.0e9f, false, m_VFXContext);
        if (m_PortalVfx) m_AreaVFX.RotateInstance(m_PortalVfx, m_Stage.GetPortalYaw());
    }
    m_Pickups.Reset(m_Registry, m_Grid, p ? *p : Vector3::Zero, m_TerrainConfig.seed);
    BlockPropCells();
}

// ============================================================
// 置物の下のマスを雑魚用に塞ぐ（2026-10-03、通し検査 soak で見つけた）。
// 報酬の箱・Boss の門の柱の衝突はプレイヤーにしか効かず、GPU の雑魚は格子しか見ないので素通りしていた。
// 木・岩と同じく、衝突の箱が掛かるマスを塞ぐ（元から塞がっていたマスは覚えない = 戻さない）
// ============================================================
void CollisionTestScene::BlockPropCells()
{
    const int W = m_Grid.Width();
    for (const auto& b : m_PropBlocks)
        for (int c : b.cells) m_Grid.SetWalkable(c % W, c / W, true);
    m_PropBlocks.clear();

    auto block = [&](Entity e, bool crate)
        {
            if (!m_Registry.IsValid(e) || !m_Registry.Has<ColliderComponent>(e) || !m_Registry.Has<TransformComponent>(e))
                return;
            const auto& col = m_Registry.Get<ColliderComponent>(e);
            const Vector3 c = m_Registry.Get<TransformComponent>(e).position + col.offset;
            const Vector3 h = col.halfExtents;
            int x0, z0, x1, z1;
            m_Grid.WorldToCell(c - h, x0, z0);
            m_Grid.WorldToCell(c + h, x1, z1);
            PropBlock b{ e, crate, {} };
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    if (m_Grid.IsWalkable(x, z))
                    {
                        m_Grid.SetWalkable(x, z, false);
                        b.cells.push_back(z * W + x);
                    }
            m_PropBlocks.push_back(std::move(b));
        };
    for (Entity e : m_Crates.GetCrates()) block(e, true);
    for (Entity e : m_Stage.GetPortalPillars()) block(e, false);
    m_Swarm.RefreshWalkable(m_Grid);
}

// ============================================================
// 出られない袋小路を塞ぐ（2026-10-03、通し検査 soak で見つけた）。
// 山頂の凹み・崖と木 / 台地に囲まれた所など、フィールドの真ん中（開始時の場所）へ歩いて行けない歩けるマスがあると、
// そこに湧いた雑魚はフローフィールドの答えが無く、何分でもその場に留まっていた（3 面 × 10 分で 54 件）。
// 雑魚と同じ規則（FlowField：崖は上れない、下りは飛び降り）で真ん中を目標に解き、届かないマスを塞ぐ
// （湧かない・入らない）。地形を作った直後、GPU へ上げる前に呼ぶ
// ============================================================
void CollisionTestScene::BlockUnreachablePockets()
{
    const int W = m_Grid.Width(), D = m_Grid.Depth();
    if (W <= 0 || D <= 0) return;
    std::vector<uint8_t> walk((size_t)W * D);
    std::vector<float> hgt((size_t)W * D);
    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x)
        {
            walk[(size_t)z * W + x] = m_Grid.IsWalkable(x, z) ? 1 : 0;
            const Vector3 c = m_Grid.CellToWorld(x, z);
            hgt[(size_t)z * W + x] = m_Grid.SampleHeight(c.x, c.z);
        }
    FlowField ff;
    ff.SetGrid(W, D, walk, hgt, m_Grid.Heights(), GridWorld::kHeightSub);
    if (!ff.Build(W / 2, D / 2)) return;
    const auto& cost = ff.Costs();
    int blocked = 0;
    for (size_t i = 0; i < cost.size() && i < walk.size(); ++i)
        if (walk[i] && cost[i] > 1e30f)
        {
            m_Grid.BlockArea((int)(i % W), (int)(i / W), 1, 1);
            ++blocked;
        }
    std::cout << "[Terrain] unreachable pockets blocked: " << blocked << " cells" << std::endl;
}

void CollisionTestScene::UpdatePropBlocks()
{
    const int W = m_Grid.Width();
    bool changed = false;
    for (auto it = m_PropBlocks.begin(); it != m_PropBlocks.end();)
    {
        const bool gone = !m_Registry.IsValid(it->entity)
            || (it->crate && !m_Registry.Has<InteractableComponent>(it->entity));
        if (!gone) { ++it; continue; }
        for (int c : it->cells) m_Grid.SetWalkable(c % W, c / W, true);
        it = m_PropBlocks.erase(it);
        changed = true;
    }
    if (changed) m_Swarm.RefreshWalkable(m_Grid);
}

void CollisionTestScene::RespawnElites()
{
    m_Elites.Respawn(m_Registry, PlayerPos());
}
