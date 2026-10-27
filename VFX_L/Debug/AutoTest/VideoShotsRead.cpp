// ============================================================
// VideoShotsRead.cpp
// TEMP-TEST: 作品紹介の動画のカット（見失わない・組み合わせの背嚢）。VFXL_BATTLE_AUTOTEST=video_<名前> + VFXL_CAPTURE
//   video_outline  ④ 敵（盾兵・突撃兵・重装兵・雑魚・突撃爆弾兵）は赤、プレイヤーは青の縁取り。爆弾兵は足元に予兆の円
//   video_boss     ④ ボスの技（重撃の赤い円 → 投石雨）を、円の外へ走って避ける
//   video_damage   ④ 属性の色のダメージの数字（火・氷・雷・毒・土・光）
//   video_backpack ① バックパックに魔法を並べる：上級魔法（メテオ）は 2 つの基本魔法が隣に揃うと光る
// ============================================================
#include "Debug/AutoTest/VideoShot.h"
#include "Enemy/Spawn/MobSpawner.h"
#include "Enemy/Spawn/StageDirector.h"
#include "Enemy/Boss/BossAttacks.h"
#include "Core/GameSettings.h"

namespace
{
    // ---- ④ 陣営の縁取り ----
    class VideoOutline final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            PlaceAnywhere(ItemID::HomingBolt);
            SetCasting(true);
            m_Swarm.hpBar.enabled = false;
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            const Vector3 pp = PlayerPos();
            SpawnRing(pp, 6, 10.0f, 14.0f, Swarm::kEnemyKindShield, 400.0f, 0.8f, 0.0f, 40.0f);
            SpawnRing(pp, 5, 11.0f, 15.0f, Swarm::kEnemyKindCharger, 400.0f, 0.8f, 0.0f, 40.0f);
            SpawnRing(pp, 3, 12.0f, 16.0f, Swarm::kEnemyKindBrute, 400.0f, 0.8f, 0.0f, 40.0f);
            SpawnRing(pp, 14, 9.0f, 16.0f, Swarm::kEnemyKindMob, 400.0f, 0.8f, 0.0f, 45.0f);
            SpawnRing(pp, 2, 12.0f, 13.0f, Swarm::kEnemyKindDashBomber, 400.0f, 1.0f, 0.0f, 20.0f);
            recStart = 1.2f;
            recEnd = 8.5f;
            camKeys = { { 0.0f, -24.0f, 30.0f, 10.0f, 1.2f }, { 8.5f, -6.0f, 28.0f, 9.5f, 1.2f } };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 1.5f); return true; }
        void Tick(float t, float dt) override
        {
            FaceNearestEnemy(dt, 240.0f);
            Move(0.0f, t > 1.0f ? -0.32f : 0.0f);   // ゆっくり下がりながら撃つ（群れが前に並ぶ）
        }
    };

    // ---- ④ ボスの予兆 ----
    class VideoBoss final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        Vector3 m_Center;
        float m_Angle = 0.0f;
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            PlaceAnywhere(ItemID::Fireball);
            PlaceAnywhere(ItemID::ArcBolt);
            SetCasting(true);
            m_Center = PlayerPos();
            m_Stage.bossRingMin = 13.0f;
            m_Stage.bossRingMax = 13.5f;
            m_Stage.bossSpeed = 1.2f;
            m_Stage.RequestBoss(true);
            m_BossAttacks.firstDelay = 1.0f;
            m_BossAttacks.gap = 1.2f;
            for (int k = 0; k < (int)BossMove::Count; ++k)
                m_BossAttacks.brain.rules[k].enabled = (k == (int)BossMove::Slam || k == (int)BossMove::RockRain);
            recStart = 2.4f;
            recEnd = 12.0f;
            camKeys = { { 0.0f, 0.0f, 42.0f, 19.0f, 1.0f } };
        }
        // 注視点 = プレイヤーと Boss の間。カメラは Boss を奥に置く向き
        bool CameraTarget(float, Vector3& out) override
        {
            const Vector3 pp = PlayerPos();
            out = m_Stage.IsBossAlive() ? pp * 0.6f + m_Stage.BossPos() * 0.4f : pp;
            return true;
        }
        void Tick(float t, float dt) override
        {
            if (m_Stage.IsBossAlive())
            {
                const Vector3 d = m_Stage.BossPos() - PlayerPos();
                const float want = DirectX::XMConvertToDegrees(std::atan2(-d.x, d.z));
                auto& cam = m_Camera.Camera();
                const float cur = cam.GetYaw();
                cam.SetYaw(cur + std::remainder(want - cur, 360.0f) * (std::min)(1.0f, dt * 1.5f));
            }
            // 円が出たら走って抜ける：始点の周り半径 5m の円を回る（入力はカメラから見た向きなので、接線を画面の向きへ直す）
            if (t > 2.6f)
            {
                m_Angle += dt * 0.95f;
                const Vector3 target = m_Center + Vector3(std::cos(m_Angle), 0.0f, std::sin(m_Angle)) * 5.0f;
                Vector3 to = target - PlayerPos();
                to.y = 0.0f;
                if (to.LengthSquared() > 0.04f) to.Normalize();
                Vector3 fwd = m_Camera.Camera().GetForward(), right = m_Camera.Camera().GetRight();
                fwd.y = right.y = 0.0f;
                fwd.Normalize();
                right.Normalize();
                Move(to.Dot(right), to.Dot(fwd));
            }
        }
    };

    // ---- ④ ダメージの数字 ----
    class VideoDamage final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            PlacePoisonSet(bp);
            PlaceAnywhere(ItemID::Fireball);
            PlaceAnywhere(ItemID::IceLance);
            PlaceAnywhere(ItemID::ArcBolt);
            PlaceAnywhere(ItemID::GoldenArrow);
            PlaceAnywhere(ItemID::StoneShot);
            SetCasting(true);
            GameSettings::Get().damageNumbers = true;   // ユーザーの設定では切ってある（このカットだけ出す。保存はしない）
            m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f;
            SpawnRing(PlayerPos(), 14, 6.0f, 9.0f, Swarm::kEnemyKindMob, 4000.0f, 0.06f, 0.0f, 35.0f);
            recStart = 2.0f;
            recEnd = 8.5f;
            camKeys = { { 0.0f, 22.0f, 30.0f, 8.5f, 1.0f }, { 8.5f, 8.0f, 32.0f, 8.5f, 1.0f } };
        }
        bool CameraTarget(float, Vector3& out) override { out = PlayerPos() + Vector3(0.0f, 0.0f, 3.0f); return true; }
        void Tick(float, float) override { m_Registry.Get<TransformComponent>(m_Player).rotation.y = 0.0f; }
    };

    // ---- ① バックパック ----
    class VideoBackpack final : public VideoShot
    {
    public:
        using VideoShot::VideoShot;
    private:
        struct Step { float t; ItemID id; int r, c, rot; };
        std::vector<Step> m_Steps;
        size_t m_Next = 0;
        int m_Meteor = -1;
        void Setup() override
        {
            StopSpawning();
            ClearSpells();
            SetCasting(false);
            // 魔法書の木箱に入れておく（置くと木箱から消えて背嚢へ移る）
            auto& book = m_Registry.Get<SpellbookComponent>(m_Player);
            const ItemID owned[] = { ItemID::Meteor, ItemID::Fireball, ItemID::StoneShot, ItemID::SplitRune,
                                     ItemID::Magnifier, ItemID::HomingBolt, ItemID::IceLance, ItemID::DoubleCastRune };
            for (ItemID id : owned) book.Learn(id);
            m_Steps = {
                { 1.2f, ItemID::Meteor, 2, 2, 0 },
                { 2.4f, ItemID::Fireball, 0, 3, 0 },
                { 3.6f, ItemID::StoneShot, 3, 1, 0 },
            };
            m_GameUI.TestShow(1);
            recStart = 0.6f;
            recEnd = 9.5f;
        }
        void Tick(float t, float) override
        {
            auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
            while (m_Next < m_Steps.size() && t >= m_Steps[m_Next].t)
            {
                const Step& s = m_Steps[m_Next++];
                const int i = BackpackLogic::Place(bp, s.id, s.r, s.c, s.rot);
                if (s.id == ItemID::Meteor) m_Meteor = i;
                bp.dirty = true;
                AudioSystem::Get().Play("item_place");
            }
            // メテオが目覚めた後：ルーンを 2 つ足す → 説明を出す
            if (!m_Runes && t >= 4.9f && m_Meteor >= 0)
            {
                PlaceRuneFor(ItemID::SplitRune, m_Meteor);
                AudioSystem::Get().Play("item_place");
                m_Runes = true;
            }
            if (!m_Runes2 && t >= 5.8f && m_Meteor >= 0)
            {
                PlaceRuneFor(ItemID::Magnifier, m_Meteor);
                AudioSystem::Get().Play("item_place");
                m_Runes2 = true;
            }
            if (t >= 6.6f && m_Meteor >= 0) m_GameUI.TestTooltip(m_Meteor, { m_ScreenW * 0.60f, m_ScreenH * 0.24f });
        }
        bool m_Runes = false, m_Runes2 = false;
    };
}

REGISTER_BATTLE_AUTOTEST("video_outline", VideoOutline)
REGISTER_BATTLE_AUTOTEST("video_boss", VideoBoss)
REGISTER_BATTLE_AUTOTEST("video_damage", VideoDamage)
REGISTER_BATTLE_AUTOTEST("video_backpack", VideoBackpack)
