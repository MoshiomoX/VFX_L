// ============================================================
// VideoShot.cpp
// TEMP-TEST: 作品紹介の動画の 1 カットの土台（VideoShot.h の説明）
// ============================================================
#include "Debug/AutoTest/VideoShot.h"
#include "Swarm/ProjectileProfile.h"
#include "World/WeatherSystem.h"
#include "Enemy/Spawn/MobSpawner.h"
#include "Enemy/Spawn/StageDirector.h"
#include "Core/GameSettings.h"

namespace
{
    constexpr float kSetupAt = 1.0f;   // 起動（戦闘シーンに入って）からこの秒で Setup
}

float VideoShot::Rand01()
{
    m_Rng = m_Rng * 1664525u + 1013904223u;
    return (float)((m_Rng >> 8) & 0xFFFFFF) / 16777216.0f;
}

Vector3 VideoShot::PlayerPos()
{
    return m_Registry.Has<TransformComponent>(m_Player) ? m_Registry.Get<TransformComponent>(m_Player).position : Vector3::Zero;
}

void VideoShot::UpdateFrame(float dt)
{
    // 時計は書き出しの時計（スローでも等速）。書き出していない時は渡された dt
    const float vdt = VideoCapture::Get().Enabled() ? VideoCapture::Get().FrameDt() : dt;
    m_Boot += vdt;
    m_AutoTime = m_Boot;
    if (m_Done) return;

    if (!m_Setup)
    {
        if (m_Boot < kSetupAt || !m_Registry.IsValid(m_Player)) return;
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_PlayerControlSystem.testInput = true;
        m_PlayerControlSystem.testMove = Vector2::Zero;
        auto& cam = m_Camera.Camera();
        cam.avoidOcclusion = false;
        GameSettings::Get().showFps = false;   // 右上の FPS（書き出し中の実 fps）は映さない。保存はしない
        m_Swarm.hpBar.enabled = false;         // 雑魚の体力バー（群れが赤い棒だらけに見える）。要るカットは Setup で戻す
        Setup();
        m_GameUI.hideAll = !hud;
        m_Setup = true;
        m_T = 0.0f;
        ApplyCamera();
        cam.SnapToTarget();
        char line[160];
        snprintf(line, sizeof(line), "video setup stage %d seed %u rec %.2f..%.2f", m_StageIndex, m_TerrainConfig.seed, recStart, recEnd);
        AutoTestLog(line);
        return;
    }

    m_T += vdt;
    KeepHud();
    Tick(m_T, dt);
    ApplyCamera();

    VideoCapture& vc = VideoCapture::Get();
    if (!vc.Recording() && m_T >= recStart && m_T < recEnd)
    {
        vc.SetRecording(true);
        AutoTestLog("video rec start");
    }
    if (m_T >= recEnd)
    {
        char line[96];
        snprintf(line, sizeof(line), "video rec end frames %u", vc.FramesWritten());
        AutoTestLog(line);
        vc.Finish();
        AutoTestLog("video done");
        m_Done = true;
        PostQuitMessage(0);
    }
}

void VideoShot::ApplyCamera()
{
    Vector3 target;
    if (CameraTarget(m_T, target)) m_Camera.SetTargetOverride(&target);
    else m_Camera.SetTargetOverride(nullptr);
    if (camKeys.empty()) return;

    // キー同士の間を smoothstep で補う（キーの所で一瞬止まらないよう、端以外は線形と混ぜる）
    const CamKey* a = &camKeys.front();
    const CamKey* b = a;
    for (size_t i = 0; i + 1 < camKeys.size(); ++i)
        if (m_T >= camKeys[i].t) { a = &camKeys[i]; b = &camKeys[i + 1]; }
    if (m_T >= camKeys.back().t) a = b = &camKeys.back();
    float u = (b->t > a->t) ? std::clamp((m_T - a->t) / (b->t - a->t), 0.0f, 1.0f) : 0.0f;
    u = u * u * (3.0f - 2.0f * u);
    auto lerp = [u](float x, float y) { return x + (y - x) * u; };
    auto& cam = m_Camera.Camera();
    cam.SetYaw(lerp(a->yaw, b->yaw));
    cam.SetPitch(lerp(a->pitch, b->pitch));
    cam.distance = lerp(a->dist, b->dist);
    cam.height = lerp(a->height, b->height);
}

void VideoShot::KeepHud()
{
    if (m_Registry.Has<HealthComponent>(m_Player))
    {
        auto& hp = m_Registry.Get<HealthComponent>(m_Player);
        hp.invincible = true;
        hp.current = hp.max;
    }
    if (m_Registry.Has<ManaComponent>(m_Player))
    {
        auto& mp = m_Registry.Get<ManaComponent>(m_Player);
        mp.current = mp.max;
    }
    if (m_Registry.Has<LevelComponent>(m_Player))
    {
        auto& lv = m_Registry.Get<LevelComponent>(m_Player);
        lv.level = level;
        const float cap = lv.ExpToNext() * expFill;
        if (lv.experience > cap || lv.experience < cap * 0.5f) lv.experience = cap;
    }
    if (m_Registry.Has<WalletComponent>(m_Player))
    {
        auto& w = m_Registry.Get<WalletComponent>(m_Player);
        if (w.gold < (float)gold) w.gold = (float)gold;
    }
}

void VideoShot::StopSpawning()
{
    m_Mobs.Director().enabled = false;
    m_Stage.waves.enabled = false;
    m_Weather.eventsEnabled = false;
    m_Swarm.KillAll();
}

void VideoShot::ClearSpells()
{
    if (!m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    ClearBackpackItems(bp);
    FillAllFrames(bp);
}

int VideoShot::PlaceAnywhere(ItemID id)
{
    if (!m_Registry.Has<BackpackComponent>(m_Player)) return -1;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    for (int r = 0; r < BackpackComponent::GRID; ++r)
        for (int c = 0; c < BackpackComponent::GRID; ++c)
            for (int rot = 0; rot < 4; ++rot)
                if (BackpackLogic::CanPlace(bp, id, r, c, rot))
                {
                    const int i = BackpackLogic::Place(bp, id, r, c, rot);
                    bp.dirty = true;
                    return i;
                }
    return -1;
}

void VideoShot::BuildShowcase()
{
    if (!m_Registry.Has<BackpackComponent>(m_Player)) return;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    ClearSpells();
    // タイトルの戦いの見本（CollisionTestScene::InitTitleDemo）と同じ並び
    struct P { ItemID id; int r, c; };
    const P layout[] = {
        { ItemID::Meteor, 2, 2 }, { ItemID::Fireball, 0, 3 }, { ItemID::StoneShot, 3, 1 },
        { ItemID::Beam, 6, 5 }, { ItemID::HomingBolt, 5, 4 }, { ItemID::ArcBolt, 7, 6 },
        { ItemID::Poison, 1, 7 }, { ItemID::GoldenArrow, 3, 8 }, { ItemID::Swamp, 2, 7 }, { ItemID::Spore, 2, 6 },
    };
    int index[(int)std::size(layout)];
    for (int i = 0; i < (int)std::size(layout); ++i)
        index[i] = BackpackLogic::Place(bp, layout[i].id, layout[i].r, layout[i].c, 0);
    struct R { ItemID rune; int target; };
    const R runes[] = {
        { ItemID::SplitRune, 0 }, { ItemID::DoubleCastRune, 0 }, { ItemID::Magnifier, 0 },
        { ItemID::SplitRune, 4 }, { ItemID::SplitRune, 7 }, { ItemID::HasteRune, 3 },
        { ItemID::Magnifier, 6 }, { ItemID::DoubleCastRune, 5 }, { ItemID::HasteRune, 1 },
        { ItemID::SplitRune, 2 },
    };
    for (const R& rr : runes)
        PlaceRuneFor(rr.rune, index[rr.target]);
    bp.dirty = true;
}

int VideoShot::PlaceRuneFor(ItemID rune, int target)
{
    if (target < 0 || !m_Registry.Has<BackpackComponent>(m_Player)) return -1;
    auto& bp = m_Registry.Get<BackpackComponent>(m_Player);
    auto influences = [&](int runeIdx, int item)
        {
            const std::vector<int> inf = BackpackLogic::GetInfluencers(bp, item);
            return std::find(inf.begin(), inf.end(), runeIdx) != inf.end();
        };
    // 狙いの魔法に届く置き場所のうち、影響を受ける物の数が一番多い所（総当たり。置いて調べて外す）
    int bestR = -1, bestC = -1, bestRot = 0, bestCount = 0;
    for (int r = 0; r < BackpackComponent::GRID; ++r)
        for (int c = 0; c < BackpackComponent::GRID; ++c)
            for (int rot = 0; rot < 4; ++rot)
            {
                if (!BackpackLogic::CanPlace(bp, rune, r, c, rot)) continue;
                const int ri = BackpackLogic::Place(bp, rune, r, c, rot);
                if (ri < 0) continue;
                int count = 0;
                if (influences(ri, target))
                    for (int k = 0; k < ri; ++k) count += influences(ri, k) ? 1 : 0;
                BackpackLogic::Remove(bp, ri);   // 一番後ろなので他の番号は変わらない
                if (count > bestCount) { bestCount = count; bestR = r; bestC = c; bestRot = rot; }
            }
    if (bestCount <= 0) return -1;
    bp.dirty = true;
    return BackpackLogic::Place(bp, rune, bestR, bestC, bestRot);
}

void VideoShot::SetCasting(bool on)
{
    if (m_Registry.Has<WandComponent>(m_Player))
        m_Registry.Get<WandComponent>(m_Player).castingPaused = !on;
}

void VideoShot::SpawnRing(const Vector3& center, int count, float rMin, float rMax, uint32_t kind, float hp, float speedMul,
    float arcCenterDeg, float arcHalfDeg)
{
    const float gy = m_Swarm.GetAIParams().groundY;
    float baseHp = 15.0f, speed = 3.5f;
    m_Mobs.KindStats(kind, baseHp, speed);
    for (int i = 0; i < count; ++i)
        for (int tries = 0; tries < 8; ++tries)
        {
            const float yaw = DirectX::XMConvertToRadians(arcCenterDeg + (Rand01() * 2.0f - 1.0f) * arcHalfDeg);
            const float dist = rMin + (rMax - rMin) * std::sqrt(Rand01());
            const float x = center.x + std::sin(yaw) * dist;
            const float z = center.z + std::cos(yaw) * dist;
            if (!m_Grid.IsWalkableAt(Vector3(x, 0.0f, z))) continue;
            m_Swarm.SpawnEnemy(Vector3(x, m_Grid.SampleHeight(x, z) + gy, z), hp, speed * speedMul, kind);
            break;
        }
}

void VideoShot::DropMeteor(const Vector3& impactXZ)
{
    static const int s_Meteor = ProjectileProfileDB::IndexOf("Meteor");
    if (s_Meteor <= 0) return;
    const ProjectileProfile& prof = ProjectileProfileDB::At(s_Meteor);
    const Vector3 pp = PlayerPos();
    Vector3 dir = impactXZ - pp;
    dir.y = 0.0f;
    if (dir.LengthSquared() < 0.01f) dir = Vector3(0, 0, 1);
    dir.Normalize();
    Vector3 impact = impactXZ;
    impact.y = m_Grid.SampleHeight(impact.x, impact.z) + m_Swarm.GetAIParams().groundY;
    m_Swarm.SpawnProjectile(prof.ResolveVFX(), impact, dir * prof.speed, prof.damage, prof.radius, prof.lifetime,
        (uint32_t)s_Meteor, false, 0, true);
    AudioSystem::Get().Play(prof.castSound);
}

void VideoShot::FaceNearestEnemy(float dt, float turnDegPerSec)
{
    if (!m_Registry.Has<TransformComponent>(m_Player)) return;
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    Vector3 np, nv;
    float nd = 0.0f;
    if (!m_Swarm.GetNearestEnemy(np, nv, nd) || nd > 30.0f) return;
    const Vector3 d = np - tf.position;
    if (d.x * d.x + d.z * d.z < 0.01f) return;
    const float target = DirectX::XMConvertToDegrees(std::atan2(d.x, d.z));
    const float diff = std::remainder(target - tf.rotation.y, 360.0f);
    const float step = turnDegPerSec * dt;
    tf.rotation.y = std::remainder(tf.rotation.y + std::clamp(diff, -step, step), 360.0f);
}
