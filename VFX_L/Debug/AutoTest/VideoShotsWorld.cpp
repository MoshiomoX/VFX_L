// ============================================================
// VideoShotsWorld.cpp
// TEMP-TEST: 作品紹介の動画のカット（風と世界・大群の動き）。VFXL_BATTLE_AUTOTEST=video_<名前> + VFXL_CAPTURE
//   video_gust      ③ 突風：前から来る群れを吹き飛ばし、草が扇形に倒れる（草原）
//   video_twister   ③ 竜巻：渦 + 突風 → 竜巻。砂漠（VFXL_STAGE=2）では転がり草が巻き上がる、草原では草が渦を巻く
//   video_cliff     ⑤ 台地の上の群れが崖から飛び降りてくる（草原の段々）
//   video_flow      ⑤ 段々の台地を群れが回り込んで寄ってくる（高い所から）
//   video_fire      ③ メテオの爆発で草に火が付き、風下へ燃え広がる（延焼は既定で切。このカットだけ入れる）
//   video_wind      ③ 強い風：草が波打ち、落ち葉が舞い、マントがはためく（全部同じ 1 つの風）
//   video_flatten   ③ メテオの爆発と光線が草をなぎ倒す（低いカメラ）
// ============================================================
#include "Debug/AutoTest/VideoShot.h"
#include "Enemy/Spawn/MobSpawner.h"
#include "World/Tumbleweeds.h"
#include "World/GrassFireField.h"
#include "World/WeatherSystem.h"

namespace
{
    // ---- ③ 突風 ----
    class VideoGust final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            const int gust = PlaceAnywhere(ItemID::Gust);
            PlaceRuneFor(ItemID::Magnifier, gust);   // 大きい扇
            PlaceRuneFor(ItemID::HasteRune, gust);   // 間隔 0.9 秒
            SetCasting(true);
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            // 前（+Z）から歩いて来る群れ。突風で 3.5m 押し戻されてはまた寄って来る
            SpawnRing(PlayerPos(), 22, 4.5f, 9.0f, Swarm::kEnemyKindMob, 260.0f, 0.8f, 0.0f, 30.0f);
            recStart = 1.6f;
            recEnd = 9.5f;
            // 真横（+X 側）から：+Z は画面の左。押し出しが横向きに見える
            camKeys = {
                { 0.0f, 100.0f, 36.0f, 11.0f, 1.0f },
                { 9.5f, 80.0f, 40.0f, 11.5f, 1.0f },
            };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 3.5f); return true; }
        void Tick(float t, float) override
        {
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + 0.6f;
                if (m_Swarm.GetCounters().aliveEnemies < 30)
                    SpawnRing(PlayerPos(), 4, 9.0f, 12.0f, Swarm::kEnemyKindMob, 260.0f, 0.8f, 0.0f, 30.0f);
            }
        }
        float m_NextSpawn = 2.0f;
    };

    // ---- ③ 竜巻（windtoys の写真用と同じ並び）----
    class VideoTwister final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        bool Desert() const { return m_TerrainConfig.biome == TerrainGenerator::Biome::Desert; }
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            BackpackLogic::Place(bp, ItemID::Twister, 3, 4, 0);
            BackpackLogic::Place(bp, ItemID::VoidVortex, 4, 2, 0);
            BackpackLogic::Place(bp, ItemID::Gust, 4, 5, 0);
            bp.dirty = true;
            SetCasting(true);
            const Vector3 pp = PlayerPos();
            const float groundY = m_Swarm.GetAIParams().groundY;
            for (int k = 0; k < 24; ++k)
            {
                const float x = pp.x - 7.0f + 14.0f * (float)(k % 6) / 5.0f;
                const float z = pp.z + 11.0f + 2.0f * (float)(k / 6);
                m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + groundY, z), 300.0f, 1.8f, Swarm::kEnemyKindMob);
            }
            // 突風が届く距離（6m 前後）にも数体：突風の連鎖がすぐ起きて、竜巻が早く出る（渦は群れを向こうへ引き留める）
            SpawnRing(pp, 8, 4.5f, 6.5f, Swarm::kEnemyKindMob, 300.0f, 0.5f, 0.0f, 35.0f);
            if (Desert())
            {
                auto& ts = m_Tumbleweeds.GetSettings();
                ts.countDesert = 26;
                ts.spawnMin = 5.0f;
                ts.spawnMax = 16.0f;
                m_Tumbleweeds.Reset(m_Registry, m_Grid, m_TerrainConfig.biome, m_TerrainConfig.seed, pp + Vector3(0.0f, 0.0f, 11.0f));
            }
            recStart = 1.8f;
            recEnd = 12.0f;
            if (Desert())
                camKeys = { { 0.0f, 18.0f, 22.0f, 14.0f, 1.5f }, { 12.0f, 2.0f, 24.0f, 15.0f, 1.5f } };
            else
                camKeys = { { 0.0f, 24.0f, 30.0f, 12.0f, 1.2f }, { 12.0f, 6.0f, 34.0f, 13.0f, 1.2f } };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 2.5f); return true; }
        void Tick(float, float) override
        {
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            if (!m_Placed && Desert() && m_WeaponSystem.GetTwisterCount() > 0)
            {
                // 竜巻の出た所の周り 2〜7m に転がり草を置き直す（吸い込み・巻き上げを直接見せる）
                Vector3 tp;
                if (m_WeaponSystem.GetTwisterPos(0, tp))
                {
                    auto& ts = m_Tumbleweeds.GetSettings();
                    ts.spawnMin = 2.0f;
                    ts.spawnMax = 7.0f;
                    m_Tumbleweeds.Reset(m_Registry, m_Grid, m_TerrainConfig.biome, m_TerrainConfig.seed, tp);
                    m_Placed = true;
                }
            }
        }
        bool m_Placed = false;
    };

    // 段々の崖：0 段のマスの隣が 1 段（坂でない）で、その向こうも 1 段が続く所。中央から近い順
    bool FindCliff(const MapData::Map& map, const GridWorld& grid, int& outX, int& outZ, int& outDx, int& outDz)
    {
        const int gw = map.gw, gd = map.gd;
        if (map.stepLevel.empty()) return false;
        float bestD = 1.0e9f;
        bool found = false;
        auto lv = [&](int x, int z) -> int { return (x < 0 || z < 0 || x >= gw || z >= gd) ? -1 : map.stepLevel[(size_t)z * gw + x]; };
        auto ramp = [&](int x, int z) { return map.stepRampAt[(size_t)z * gw + x] >= 0; };
        for (int z = 8; z < gd - 8; ++z)
            for (int x = 8; x < gw - 8; ++x)
            {
                if (lv(x, z) != 0 || ramp(x, z) || !grid.IsWalkable(x, z)) continue;
                const int nb[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (const auto& n : nb)
                {
                    // 向こう 6 マスは 1 段（広い台地）、手前 5 マスは 0 段（立つ所）、横 ±2 マスも同じ形（崖の面が続く）
                    bool ok = true;
                    for (int s = -2; s <= 2 && ok; ++s)
                    {
                        const int sx = x + n[1] * s, sz = z + n[0] * s;
                        for (int k = 1; k <= 6 && ok; ++k)
                            ok = lv(sx + n[0] * k, sz + n[1] * k) == 1 && !ramp(sx + n[0] * k, sz + n[1] * k) && grid.IsWalkable(sx + n[0] * k, sz + n[1] * k);
                        for (int k = 0; k < 5 && ok; ++k)
                            ok = lv(sx - n[0] * k, sz - n[1] * k) == 0 && grid.IsWalkable(sx - n[0] * k, sz - n[1] * k);
                    }
                    if (!ok) continue;
                    const float d = (float)((x - gw / 2) * (x - gw / 2) + (z - gd / 2) * (z - gd / 2));
                    if (d < bestD) { bestD = d; outX = x; outZ = z; outDx = n[0]; outDz = n[1]; found = true; }
                }
            }
        return found;
    }

    // ---- ⑤ 崖から飛び降りる ----
    class VideoCliff final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        Vector3 m_Dir, m_Edge;
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            PlaceAnywhere(ItemID::Fireball);
            PlaceAnywhere(ItemID::ArcBolt);
            SetCasting(false);
            int x = 0, z = 0, dx = 0, dz = 1;
            if (!FindCliff(m_TerrainMap, m_Grid, x, z, dx, dz)) AutoTestLog("video cliff not found");
            m_Dir = Vector3((float)dx, 0.0f, (float)dz);
            const Vector3 cell = m_Grid.CellToWorld(x, z);
            m_Edge = cell + m_Dir * 1.0f;                 // 崖の面（マスの境）
            const Vector3 stand = m_Edge - m_Dir * 7.0f;   // 崖の 7m 手前に立つ
            auto& tf = m_Registry.Get<TransformComponent>(m_Player);
            tf.position = Vector3(stand.x, m_Grid.SampleHeight(stand.x, stand.z) + 1.0f, stand.z);
            tf.rotation.y = DirectX::XMConvertToDegrees(std::atan2(m_Dir.x, m_Dir.z));
            if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
            // 台地の上（崖の向こう 3〜16m、横 ±9m）に群れ
            const float gy = m_Swarm.GetAIParams().groundY;
            const Vector3 side(-m_Dir.z, 0.0f, m_Dir.x);
            for (int i = 0; i < 160; ++i)
            {
                const Vector3 p = m_Edge + m_Dir * (3.0f + 13.0f * Rand01()) + side * (Rand01() * 18.0f - 9.0f);
                if (!m_Grid.IsWalkableAt(p)) continue;
                m_Swarm.SpawnEnemy(Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + gy, p.z), 200.0f, 3.2f, Swarm::kEnemyKindMob);
            }
            // カメラ：崖の面を斜め横から（進む向きから 55° 回した向きで見る）
            const float base = DirectX::XMConvertToDegrees(std::atan2(-m_Dir.x, m_Dir.z));
            recStart = 0.8f;
            recEnd = 8.0f;
            camKeys = { { 0.0f, base + 58.0f, 20.0f, 15.0f, 1.0f }, { 8.0f, base + 40.0f, 24.0f, 15.0f, 1.0f } };
            char line[128];
            snprintf(line, sizeof(line), "video cliff at %d,%d dir %d,%d", x, z, dx, dz);
            AutoTestLog(line);
        }
        bool CameraTarget(float, Vector3& out) override
        {
            out = PlayerPos() + m_Dir * 4.5f;
            return true;
        }
        void Tick(float t, float) override
        {
            if (t > 4.5f) SetCasting(true);   // 降りてきた所を最後に吹き飛ばす
        }
    };

    // ---- ⑤ 段々を回り込む群れ（高い所から）----
    class VideoFlow final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            BuildShowcase();
            SetCasting(false);
            m_Swarm.hpBar.enabled = false;
            SpawnRing(PlayerPos(), 2200, 20.0f, 46.0f, Swarm::kEnemyKindMob, 200.0f, 1.0f);
            recStart = 0.8f;
            recEnd = 10.0f;
            camKeys = { { 0.0f, -30.0f, 56.0f, 34.0f, 1.0f }, { 10.0f, 20.0f, 50.0f, 28.0f, 1.0f } };
        }
        void Tick(float t, float dt) override
        {
            FaceNearestEnemy(dt, 200.0f);
            if (t > 7.5f) SetCasting(true);
        }
    };
}

namespace
{
    // ---- ③ 草の延焼：メテオの爆発で草に火が付き、風下へ燃え広がる（延焼は既定で切。このカットだけ入れる）----
    class VideoFire final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int meteor = BackpackLogic::Place(bp, ItemID::Meteor, 2, 2, 0);
            BackpackLogic::Place(bp, ItemID::Fireball, 0, 3, 0);
            BackpackLogic::Place(bp, ItemID::StoneShot, 3, 1, 0);
            PlaceRuneFor(ItemID::SplitRune, meteor);
            bp.dirty = true;
            SetCasting(true);
            m_GrassFire.GetSettings().enabled = true;
            m_Weather.windSpeed = 2.4f;   // 炎が風下へ傾き、煙が流れる程度
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            const Vector3 pp = PlayerPos();
            SpawnRing(pp, 40, 8.0f, 16.0f, Swarm::kEnemyKindMob, 40.0f, 0.35f, 0.0f, 40.0f);
            // 書き出しの頭で既に燃えているよう、前の 2 か所に先に火を付けておく
            m_GrassFire.Ignite(pp + Vector3(-3.0f, 0.0f, 10.0f), 2.0f);
            m_GrassFire.Ignite(pp + Vector3(4.0f, 0.0f, 12.5f), 2.0f);
            recStart = 3.0f;
            recEnd = 12.0f;
            camKeys = { { 0.0f, -22.0f, 34.0f, 13.0f, 1.0f }, { 12.0f, -6.0f, 40.0f, 14.5f, 1.0f } };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 6.0f); return true; }
        void Tick(float t, float) override
        {
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + 0.6f;
                if (m_Swarm.GetCounters().aliveEnemies < 40)
                    SpawnRing(PlayerPos(), 8, 12.0f, 17.0f, Swarm::kEnemyKindMob, 40.0f, 0.35f, 0.0f, 40.0f);
            }
        }
        float m_NextSpawn = 1.0f;
    };

    // ---- ③ 強い風：同じ 1 つの風で草が波打ち、落ち葉が舞い、マントがはためく ----
    class VideoWind final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            SetCasting(false);
            m_Weather.windSpeed = 4.2f;
            m_Weather.windGust = 2.2f;
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 90.0f;
            recStart = 2.0f;
            recEnd = 9.5f;
            camKeys = { { 0.0f, 20.0f, 12.0f, 7.5f, 1.1f }, { 9.5f, 48.0f, 15.0f, 8.0f, 1.1f } };
        }
        void Tick(float t, float) override { Move(t > 0.5f ? 0.38f : 0.0f, 0.0f); }   // 横へゆっくり歩く（マント）
    };

    // ---- ③ 爆発と光線が草をなぎ倒す（低いカメラ）----
    class VideoFlatten final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            const int beam = BackpackLogic::Place(bp, ItemID::Beam, 6, 5, 0);
            BackpackLogic::Place(bp, ItemID::HomingBolt, 5, 4, 0);
            BackpackLogic::Place(bp, ItemID::ArcBolt, 7, 6, 0);
            PlaceRuneFor(ItemID::Magnifier, beam);
            bp.dirty = true;
            SetCasting(false);
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            SpawnRing(PlayerPos(), 10, 14.0f, 17.0f, Swarm::kEnemyKindMob, 100000.0f, 0.0f, 0.0f, 8.0f);
            recStart = 1.0f;
            recEnd = 8.5f;
            camKeys = { { 0.0f, 38.0f, 15.0f, 8.5f, 0.8f }, { 8.5f, 22.0f, 17.0f, 9.0f, 0.8f } };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 5.0f); return true; }
        void Tick(float t, float) override
        {
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            const Vector3 pp = PlayerPos();
            if (m_Drops == 0 && t >= 1.3f) { DropMeteor(pp + Vector3(-2.5f, 0.0f, 6.0f)); ++m_Drops; }
            if (m_Drops == 1 && t >= 2.4f) { DropMeteor(pp + Vector3(2.8f, 0.0f, 7.5f)); ++m_Drops; }
            if (t >= 3.8f) SetCasting(true);   // 光線が草の上を通る
        }
        int m_Drops = 0;
    };
}

REGISTER_BATTLE_AUTOTEST("video_fire", VideoFire)
REGISTER_BATTLE_AUTOTEST("video_wind", VideoWind)
REGISTER_BATTLE_AUTOTEST("video_flatten", VideoFlatten)
REGISTER_BATTLE_AUTOTEST("video_gust", VideoGust)
REGISTER_BATTLE_AUTOTEST("video_twister", VideoTwister)
REGISTER_BATTLE_AUTOTEST("video_cliff", VideoCliff)
REGISTER_BATTLE_AUTOTEST("video_flow", VideoFlow)
