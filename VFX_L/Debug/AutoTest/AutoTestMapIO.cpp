// ============================================================
// AutoTestMapIO.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=mapio
// 地図のデータ（World/MapData、2026-10-05）：生成した地形を保存 → 読んで建て直し、同じ物になるかを確かめる
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"
#include "World/MapData.h"
#include "World/MapEdit.h"

namespace
{
    class AutoTestMapIO final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        void Look();

        // 地形の実体の要約：数と、順番に依らない合計（位置・拡縮・衝突の形 / 大きさ / 層・モデルの有無）
        struct Digest { size_t count = 0; uint64_t sum = 0; };
        Digest TerrainDigest() const;

        std::vector<uint8_t> m_BytesA;
        Digest m_EntA;
        uint64_t m_GridA = 0;
        Vector3 m_Start;
    };

    constexpr const char* kFile = "_autotest";

    uint64_t Fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull)
    {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }
}

AutoTestMapIO::Digest AutoTestMapIO::TerrainDigest() const
{
    Digest d;
    for (Entity e : m_Terrain)
    {
        if (!m_Registry.IsValid(e)) continue;
        ++d.count;
        uint64_t h = 1469598103934665603ull;
        if (m_Registry.Has<TransformComponent>(e))
        {
            const auto& tf = m_Registry.Get<TransformComponent>(e);
            const float v[9] = { tf.position.x, tf.position.y, tf.position.z, tf.rotation.x, tf.rotation.y, tf.rotation.z,
                tf.scale.x, tf.scale.y, tf.scale.z };
            h = Fnv(v, sizeof(v), h);
        }
        if (m_Registry.Has<ColliderComponent>(e))
        {
            const auto& c = m_Registry.Get<ColliderComponent>(e);
            const float v[3] = { c.halfExtents.x, c.halfExtents.y, c.halfExtents.z };
            const uint32_t k[2] = { (uint32_t)c.shape, (uint32_t)c.layer };
            h = Fnv(v, sizeof(v), h);
            h = Fnv(k, sizeof(k), h);
        }
        if (m_Registry.Has<ModelComponent>(e))
        {
            const auto& mc = m_Registry.Get<ModelComponent>(e);
            const uint8_t k[2] = { (uint8_t)(mc.model ? 1 : 0), (uint8_t)(mc.batched ? 1 : 0) };
            h = Fnv(k, sizeof(k), h);
        }
        d.sum += h;
    }
    return d;
}

// 同じ場所・同じ向きの高い視点（前後のスクリーンショットを見比べる）
void AutoTestMapIO::Look()
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    tf.position = Vector3(m_Start.x, m_Grid.SampleHeight(m_Start.x, m_Start.z) + 1.0f, m_Start.z);
    m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
    auto& cam = m_Camera.Camera();
    cam.SetYaw(30.0f);
    cam.distance = 45.0f;
    cam.SetPitch(42.0f);
    cam.SnapToTarget();
}

// ============================================================
// 1 秒：湧き停止・片付け・無敵。今の地形（seed から生成した物）の記録をバイト列にして控え（A）、
//       Assets/Data/MapData/_autotest.vmap へ保存。実体の要約と格子（シーンの後処理後）のハッシュも控える。`mapio look gen`
// 2.5 秒：その地図を読んで建て直す（RebuildTerrain）。建て直しの記録（B）が A と 1 バイトも違わないか、
//       実体の数・要約、格子のハッシュが同じかを記録。`mapio look load`（3.5 秒）
// 4.5 秒：ファイルを消して `mapio done`
// ============================================================
void AutoTestMapIO::Run()
{
    if (!m_Registry.IsValid(m_Player)) return;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    char line[320];

    auto gridHash = [&]()
        {
            uint64_t h = Fnv(m_Grid.Walkable().data(), m_Grid.Walkable().size());
            return Fnv(m_Grid.Heights().data(), m_Grid.Heights().size() * sizeof(float), h);
        };

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        m_Start = m_Registry.Get<TransformComponent>(m_Player).position;

        MapData::Serialize(m_TerrainMap, m_BytesA);
        const bool saved = MapData::Save(kFile, m_TerrainMap);
        m_EntA = TerrainDigest();
        m_GridA = gridHash();
        snprintf(line, sizeof(line),
            "mapio gen seed %u biome %d bytes %zu saved %d boxes %zu hulls %zu visuals %zu props %zu blocks %zu models %zu torches %zu entities %zu",
            m_TerrainMap.seed, m_TerrainMap.biome, m_BytesA.size(), (int)saved, m_TerrainMap.boxes.size(),
            m_TerrainMap.hulls.size(), m_TerrainMap.visuals.size(), m_TerrainMap.props.size(), m_TerrainMap.blocks.size(),
            m_TerrainMap.models.size(), m_TerrainMap.torches.size(), m_EntA.count);
        AutoTestLog(line);

        // 機能付きの置き物（地図エディタで置いた箱・門。VFXL_MAP=<名前> で読んだ時）：地図の数と、実際に出来た箱の数・門の位置
        int mapCrates = 0, mapGates = 0, crates = 0;
        Vector3 gateMap, gateWorld;
        for (const auto& p : m_TerrainMap.placements)
        {
            if (p.type == MapData::kPlaceBossGate) { ++mapGates; gateMap = p.pos; }
            else ++mapCrates;
        }
        m_Registry.CreateView<InteractableComponent>().Each([&](Entity, InteractableComponent& it)
            {
                if (it.kind == InteractKind::RewardChoice) ++crates;
                else if (it.kind == InteractKind::BossPortal) gateWorld = it.basePos;
            });
        snprintf(line, sizeof(line),
            "mapio placements map crates %d gates %d -> world crates %d gate %.2f,%.2f,%.2f (map %.2f,%.2f,%.2f) blocksMatch %d",
            mapCrates, mapGates, crates, gateWorld.x, gateWorld.y, gateWorld.z, gateMap.x, gateMap.y, gateMap.z,
            (int)MapEdit::WalkableMatchesBlocks(m_TerrainMap));
        AutoTestLog(line);
        Look();
        m_AutoStep = 1;
    }
    else if (m_AutoStep == 1 && m_AutoTime >= 2.0f) { AutoTestLog("mapio look gen"); m_AutoStep = 2; }
    else if (m_AutoStep == 2 && m_AutoTime >= 2.5f)
    {
        m_MapFile = kFile;
        RebuildTerrain();

        std::vector<uint8_t> bytesB;
        MapData::Serialize(m_TerrainMap, bytesB);
        size_t firstDiff = 0;
        const size_t n = (std::min)(m_BytesA.size(), bytesB.size());
        while (firstDiff < n && m_BytesA[firstDiff] == bytesB[firstDiff]) ++firstDiff;
        const bool sameBytes = m_BytesA.size() == bytesB.size() && firstDiff == n;
        const Digest entB = TerrainDigest();
        const uint64_t gridB = gridHash();
        snprintf(line, sizeof(line),
            "mapio load sameBytes %d (a %zu b %zu firstDiff %zu) entities %zu -> %zu sameEntities %d sameGrid %d",
            (int)sameBytes, m_BytesA.size(), bytesB.size(), sameBytes ? (size_t)0 : firstDiff,
            m_EntA.count, entB.count, (int)(m_EntA.count == entB.count && m_EntA.sum == entB.sum), (int)(m_GridA == gridB));
        AutoTestLog(line);
        Look();
        m_AutoStep = 3;
    }
    else if (m_AutoStep == 3 && m_AutoTime >= 3.5f) { AutoTestLog("mapio look load"); m_AutoStep = 4; }
    else if (m_AutoStep == 4 && m_AutoTime >= 4.5f)
    {
        std::error_code ec;
        std::filesystem::remove(MapData::PathFor(kFile), ec);
        AutoTestLog("mapio done");
        m_AutoStep = 5;
    }
}

REGISTER_BATTLE_AUTOTEST("mapio", AutoTestMapIO)
