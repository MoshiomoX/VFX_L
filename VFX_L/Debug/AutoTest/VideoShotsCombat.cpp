// ============================================================
// VideoShotsCombat.cpp
// TEMP-TEST: 作品紹介の動画のカット（戦い）。VFXL_BATTLE_AUTOTEST=video_<名前> + VFXL_CAPTURE
//   video_open   冒頭：見本の背嚢（メテオ・光線・毒・矢 + ルーン）で群れを吹き飛ばす。途中でスロー。UI なし
//   video_combo  ① 組み合わせ：ファイアボール + ストーンショットの弾が消えた所にメテオが落ちる（背後から）
//   video_beam   ① 組み合わせ：ホーミングボルト + アークボルト → 魔導光線が群れを貫く
//   video_swarm  ② 大群：4,000 体の群れが四方から押し寄せ、見本の背嚢で溶ける（高い所から）
//   video_shatter② 大群：当たった敵が部品ごとに砕ける（近くからスロー）
//   video_ending 締め：見本の背嚢 + 群れを大きく回り込む。UI なし
// ============================================================
#include "Debug/AutoTest/VideoShot.h"
#include "Enemy/Spawn/MobSpawner.h"

namespace
{
    // 見本の背嚢で群れと戦う（open / ending / shatter が共有）
    class ShowcaseFight : public VideoShot
    {
    public:
        using VideoShot::VideoShot;

    protected:
        int   maxAlive = 650;
        float spawnEvery = 0.25f, spawnMin = 13.0f, spawnMax = 26.0f;
        int   spawnBatch = 30;
        float meteorEvery = 0.45f, meteorMin = 4.0f, meteorMax = 13.0f;
        float enemyHp = 5.0f, enemySpeedMul = 0.7f;

        void SetupFight(int prewarm)
        {
            StopSpawning();
            BuildShowcase();
            SetCasting(true);
            m_Swarm.hpBar.enabled = false;
            SpawnRing(PlayerPos(), prewarm, 6.0f, spawnMax, Swarm::kEnemyKindMob, enemyHp, enemySpeedMul);
        }
        void TickFight(float t, float dt)
        {
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + spawnEvery;
                if ((int)m_Swarm.GetCounters().aliveEnemies < maxAlive)
                    SpawnRing(PlayerPos(), spawnBatch, spawnMin, spawnMax, Swarm::kEnemyKindMob, enemyHp, enemySpeedMul);
            }
            if (meteorEvery > 0.0f && t >= m_NextMeteor)
            {
                m_NextMeteor = t + meteorEvery;
                const float a = Rand01() * DirectX::XM_2PI;
                const float r = meteorMin + (meteorMax - meteorMin) * Rand01();
                DropMeteor(PlayerPos() + Vector3(std::sin(a) * r, 0.0f, std::cos(a) * r));
            }
            FaceNearestEnemy(dt, 300.0f);
        }

    private:
        float m_NextSpawn = 0.0f, m_NextMeteor = 0.5f;
    };

    // ---- 冒頭 ----
    class VideoOpen final : public ShowcaseFight
    {
    public:
        using ShowcaseFight::ShowcaseFight;
    private:
        void Setup() override
        {
            hud = false;
            SetupFight(520);
            recStart = 2.5f;
            recEnd = 14.5f;
            camKeys = {
                { 0.0f, -25.0f, 10.0f, 6.5f, 1.2f },
                { 2.5f, -25.0f, 10.0f, 6.5f, 1.2f },
                { 7.0f, 10.0f, 16.0f, 8.5f, 1.3f },
                { 10.5f, 40.0f, 28.0f, 13.0f, 1.4f },
                { 14.5f, 70.0f, 40.0f, 19.0f, 1.5f },
            };
        }
        void Tick(float t, float dt) override
        {
            TickFight(t, dt);
            // 6〜9 秒（書き出しの 3.5〜6.5 秒）をスローに（前後 0.5 秒で滑らかに）
            float s = 1.0f;
            if (t > 6.0f && t < 9.5f)
            {
                const float in = std::clamp((t - 6.0f) / 0.5f, 0.0f, 1.0f), out = std::clamp((9.5f - t) / 0.5f, 0.0f, 1.0f);
                s = 1.0f - 0.72f * (std::min)(in, out);
            }
            SetSlow(s);
        }
    };

    // ---- ① メテオ ----
    class VideoCombo final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            // タイトルの見本と同じ並び（メテオの上の腕の右にファイアボール、左の腕の下にストーンショット）
            const int meteor = BackpackLogic::Place(bp, ItemID::Meteor, 2, 2, 0);
            BackpackLogic::Place(bp, ItemID::Fireball, 0, 3, 0);
            BackpackLogic::Place(bp, ItemID::StoneShot, 3, 1, 0);
            PlaceRuneFor(ItemID::SplitRune, meteor);   // 輪になって落ちる
            bp.dirty = true;
            SetCasting(true);
            m_Swarm.dropRing.enabled = true;
            SpawnRing(PlayerPos(), 60, 8.0f, 17.0f, Swarm::kEnemyKindMob, 30.0f, 0.55f, 0.0f, 38.0f);
            recStart = 1.8f;
            recEnd = 10.5f;
            camKeys = {
                { 0.0f, -34.0f, 26.0f, 9.5f, 1.3f },
                { 10.5f, -14.0f, 30.0f, 10.5f, 1.3f },
            };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 2.5f); return true; }
        void Tick(float t, float dt) override
        {
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + 0.5f;
                if (m_Swarm.GetCounters().aliveEnemies < 70)
                    SpawnRing(PlayerPos(), 10, 13.0f, 18.0f, Swarm::kEnemyKindMob, 30.0f, 0.55f, 0.0f, 38.0f);
            }
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;   // 群れ（+Z）の方を向いて立つ
            (void)dt;
        }
        float m_NextSpawn = 0.0f;
    };

    // ---- ① 魔導光線 ----
    class VideoBeam final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            // タイトルの見本と同じ並び（光線の上にホーミングボルト、下にアークボルト）+ 加速・拡大鏡
            const int beam = BackpackLogic::Place(bp, ItemID::Beam, 6, 5, 0);
            BackpackLogic::Place(bp, ItemID::HomingBolt, 5, 4, 0);
            BackpackLogic::Place(bp, ItemID::ArcBolt, 7, 6, 0);
            PlaceRuneFor(ItemID::HasteRune, beam);
            PlaceRuneFor(ItemID::Magnifier, beam);
            bp.dirty = true;
            SetCasting(true);
            // 正面に長い群れ（光線が貫く）
            SpawnRing(PlayerPos(), 120, 8.0f, 24.0f, Swarm::kEnemyKindMob, 70.0f, 0.4f, 0.0f, 22.0f);
            recStart = 2.0f;
            recEnd = 10.0f;
            camKeys = {
                { 0.0f, 32.0f, 22.0f, 11.0f, 1.5f },
                { 10.0f, 12.0f, 26.0f, 12.5f, 1.5f },
            };
        }
        void Tick(float t, float) override
        {
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + 0.4f;
                if (m_Swarm.GetCounters().aliveEnemies < 140)
                    SpawnRing(PlayerPos(), 12, 18.0f, 26.0f, Swarm::kEnemyKindMob, 70.0f, 0.4f, 0.0f, 22.0f);
            }
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
        }
        float m_NextSpawn = 0.0f;
    };

    // ---- ② 大群 4,000 ----
    class VideoSwarm final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            BuildShowcase();
            SetCasting(true);
            m_Swarm.hpBar.enabled = false;
            auto& d = m_Mobs.Director();
            d.enabled = true;
            d.spawnCap = 4000;
            m_Mobs.scaling = false;
            d.spawnPerSecond = 3000.0f;
            d.maxPerFrame = 120;
            d.rMin = 16.0f;
            d.rMax = 48.0f;
            recStart = 1.0f;
            recEnd = 12.0f;
            camKeys = {
                { 0.0f, 20.0f, 56.0f, 24.0f, 1.5f },
                { 1.0f, 20.0f, 56.0f, 24.0f, 1.5f },
                { 12.0f, 80.0f, 58.0f, 40.0f, 1.5f },
            };
        }
        void Tick(float t, float dt) override
        {
            (void)t;
            FaceNearestEnemy(dt, 300.0f);
        }
    };

    // ---- ② 砕け散る（スロー）----
    class VideoShatter final : public ShowcaseFight
    {
    public:
        using ShowcaseFight::ShowcaseFight;
    private:
        void Setup() override
        {
            meteorEvery = 0.6f;
            meteorMin = 5.0f;
            meteorMax = 9.0f;
            maxAlive = 300;
            spawnMin = 7.0f;
            spawnMax = 14.0f;
            spawnBatch = 14;
            SetupFight(220);
            hud = false;
            recStart = 2.0f;
            recEnd = 8.5f;
            camKeys = {
                { 0.0f, 150.0f, 14.0f, 7.0f, 1.0f },
                { 8.5f, 185.0f, 18.0f, 7.5f, 1.0f },
            };
        }
        void Tick(float t, float dt) override
        {
            TickFight(t, dt);
            SetSlow(t >= 2.0f ? 0.25f : 1.0f);
        }
    };

    // ---- 締め ----
    class VideoEnding final : public ShowcaseFight
    {
    public:
        using ShowcaseFight::ShowcaseFight;
    private:
        void Setup() override
        {
            hud = false;
            maxAlive = 900;
            spawnBatch = 40;
            spawnMax = 30.0f;
            SetupFight(700);
            recStart = 2.5f;
            recEnd = 12.5f;
            camKeys = {
                { 0.0f, 200.0f, 20.0f, 10.0f, 1.4f },
                { 12.5f, 300.0f, 34.0f, 22.0f, 1.6f },
            };
        }
        void Tick(float t, float dt) override { TickFight(t, dt); }
    };
}

REGISTER_BATTLE_AUTOTEST("video_open", VideoOpen)
REGISTER_BATTLE_AUTOTEST("video_combo", VideoCombo)
REGISTER_BATTLE_AUTOTEST("video_beam", VideoBeam)
REGISTER_BATTLE_AUTOTEST("video_swarm", VideoSwarm)
REGISTER_BATTLE_AUTOTEST("video_shatter", VideoShatter)
REGISTER_BATTLE_AUTOTEST("video_ending", VideoEnding)
