// ============================================================
// VideoShotsOutlet.cpp
// TEMP-TEST: 出口魔法の見た目の確認（2026-10-31）。VFXL_BATTLE_AUTOTEST=video_outlet + VFXL_CAPTURE
//   火花 → 分かれ玉 → メテオ + アイスランス、時限の種 → 竜巻。正面から寄って来る群れへ撃つ。UI なし
// ============================================================
#include "Debug/AutoTest/VideoShot.h"

namespace
{
    class VideoOutlet final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            hud = false;
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            BackpackLogic::Place(bp, ItemID::OutletSpark, 4, 0, 0);           // 出口 (4,2)
            const int split = BackpackLogic::Place(bp, ItemID::OutletSplit, 3, 1, 0);   // 軸 (4,2)、出口 (3,0) (3,4)
            PlaceAtOutlet(bp, split, ItemID::Meteor);
            PlaceAtOutlet(bp, split, ItemID::IceLance);
            const int seed = BackpackLogic::Place(bp, ItemID::OutletSeed, 6, 1, 0);
            PlaceAtOutlet(bp, seed, ItemID::Twister);
            bp.dirty = true;
            SetCasting(true);
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            m_Swarm.hpBar.enabled = false;
            SpawnRing(PlayerPos(), 90, 7.0f, 20.0f, Swarm::kEnemyKindMob, 400.0f, 0.45f, 0.0f, 60.0f);
            recStart = 1.5f;
            recEnd = 13.5f;
            camKeys = {
                { 0.0f, -18.0f, 40.0f, 15.0f, 1.0f },
                { 13.5f, 14.0f, 36.0f, 16.0f, 1.0f },
            };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 7.0f); return true; }
        void Tick(float t, float) override
        {
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            if (t >= m_NextSpawn)
            {
                m_NextSpawn = t + 0.5f;
                if (m_Swarm.GetCounters().aliveEnemies < 110)
                    SpawnRing(PlayerPos(), 12, 16.0f, 22.0f, Swarm::kEnemyKindMob, 400.0f, 0.45f, 0.0f, 60.0f);
            }
        }
        float m_NextSpawn = 1.0f;
    };
}

REGISTER_BATTLE_AUTOTEST("video_outlet", VideoOutlet)
