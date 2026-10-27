// ============================================================
// AutoTestTerrainLook.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=terrainlook（VFXL_STAGE=2 で砂漠、3 で遺跡）
// マップ毎の地形の生成規則（World/TerrainBiomeProfile.cpp、2026-10-26）を撮る：
//   1 秒: 湧き停止・全消し・無敵・詠唱停止。地図の記録（m_TerrainMap）から岩柱 / 峡谷の壁（MapData::kPillar）の数と
//         group（= 1 本の柱 / 1 枚の壁）を数えて `terrainlook pillars <置物> groups <group> blocks <塞いだ矩形>`
//   2 / 4 / 6 秒: 最初の 3 つの group の真ん中の南 14m にプレイヤーを置き、北を向いた 10m / 22° のカメラで `look pillar<n>`
//   8 秒: 同じ場所から 70m / 72° の俯瞰 `look overview`。9 秒: done
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestTerrainLook final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        std::vector<Vector3> m_Targets;
        void Teleport(const Vector3& p);
    };
}

void AutoTestTerrainLook::Teleport(const Vector3& p)
{
    auto& tf = m_Registry.Get<TransformComponent>(m_Player);
    tf.position = Vector3(p.x, m_Grid.SampleHeight(p.x, p.z) + 1.0f, p.z);
    if (m_Registry.Has<RigidbodyComponent>(m_Player)) m_Registry.Get<RigidbodyComponent>(m_Player).velocity = Vector3::Zero;
}

void AutoTestTerrainLook::Run()
{
    char line[200];
    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        m_Registry.Get<HealthComponent>(m_Player).invincible = true;
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;

        // 岩柱 / 峡谷の壁の記録：group 毎に置物の位置の平均
        std::vector<std::pair<uint32_t, Vector3>> groups;
        std::vector<int> counts;
        int props = 0, blocks = 0;
        for (const auto& p : m_TerrainMap.props)
        {
            if (p.tag.kind != MapData::kPillar && p.tag.kind != MapData::kRuinWall) continue;
            // 遺跡：外周の壁（縁の 8m 以内）は飛ばして、場内の壁 = 部屋だけ数える（spawnVisual は 1 片毎に group を切る）
            if (p.tag.kind == MapData::kRuinWall && (std::fabs(p.pos.x) > 90.0f || std::fabs(p.pos.z) > 90.0f)) continue;
            ++props;
            size_t g = 0;
            for (; g < groups.size(); ++g) if (groups[g].first == p.tag.group) break;
            if (g == groups.size()) { groups.push_back({ p.tag.group, Vector3::Zero }); counts.push_back(0); }
            groups[g].second += p.pos;
            ++counts[g];
        }
        for (const auto& b : m_TerrainMap.blocks) if (b.tag.kind == MapData::kPillar || b.tag.kind == MapData::kRuinWall) ++blocks;
        // 的：離れた物を 3 つ（同じ部屋 / 壁の節を 3 回撮らない）
        for (size_t g = 0; g < groups.size() && m_Targets.size() < 3; ++g)
        {
            const Vector3 c = groups[g].second / (float)(std::max)(1, counts[g]);
            bool isFar = true;
            for (const Vector3& t : m_Targets) if ((t - c).LengthSquared() < 20.0f * 20.0f) isFar = false;
            if (isFar) m_Targets.push_back(c);
        }
        snprintf(line, sizeof(line), "terrainlook stage %d pillars %d groups %d blocks %d", m_StageIndex, props, (int)groups.size(), blocks);
        AutoTestLog(line);
        auto& cam = m_Camera.Camera();
        cam.avoidOcclusion = false;
        m_AutoStep = 1;
    }
    if (m_AutoStep == 0) return;

    auto shot = [&](int idx)
        {
            auto& cam = m_Camera.Camera();
            if (idx < (int)m_Targets.size())
            {
                const Vector3 t = m_Targets[(size_t)idx];
                Teleport(Vector3(t.x - 9.0f, 0.0f, t.z - 11.0f));   // 斜め手前から（壁が視線に沿うと 1 本に見える）
                cam.SetYaw(-39.0f);   // カメラの前 = (-sin yaw, cos yaw) → (+9, +11) の方（10-26 まで +35 で的の反対を向いていた）
                cam.SetPitch(22.0f);
                cam.distance = 10.0f;
                cam.SnapToTarget();
                snprintf(line, sizeof(line), "terrainlook target %d at %.1f %.1f", idx, t.x, t.z);
                AutoTestLog(line);
            }
        };
    if (m_AutoStep == 1 && m_AutoTime >= 2.0f) { shot(0); m_AutoStep = 2; }
    if (m_AutoStep == 2 && m_AutoTime >= 3.0f) { if (!m_Targets.empty()) AutoTestLog("terrainlook look pillar0"); m_AutoStep = 3; }
    if (m_AutoStep == 3 && m_AutoTime >= 4.0f) { shot(1); m_AutoStep = 4; }
    if (m_AutoStep == 4 && m_AutoTime >= 5.0f) { if (m_Targets.size() > 1) AutoTestLog("terrainlook look pillar1"); m_AutoStep = 5; }
    if (m_AutoStep == 5 && m_AutoTime >= 6.0f) { shot(2); m_AutoStep = 6; }
    if (m_AutoStep == 6 && m_AutoTime >= 7.0f) { if (m_Targets.size() > 2) AutoTestLog("terrainlook look pillar2"); m_AutoStep = 7; }
    if (m_AutoStep == 7 && m_AutoTime >= 8.0f)
    {
        auto& cam = m_Camera.Camera();
        cam.SetPitch(60.0f);
        cam.distance = 38.0f;   // 砂漠の霧（10〜85m）で遠いと白くなる
        cam.SnapToTarget();
        m_AutoStep = 8;
    }
    if (m_AutoStep == 8 && m_AutoTime >= 9.0f) { AutoTestLog("terrainlook look overview"); m_AutoStep = 9; }
    // 真上から（遺跡：部屋の中身 = 主と壁際の小物。2026-10-26）：的 0 の真ん中に立って 65° で見下ろす
    if (m_AutoStep == 9 && m_AutoTime >= 9.6f && !m_Targets.empty())
    {
        const Vector3 t = m_Targets[0];
        Teleport(Vector3(t.x, 0.0f, t.z + 1.0f));
        auto& cam = m_Camera.Camera();
        cam.SetYaw(0.0f);
        cam.SetPitch(65.0f);
        cam.distance = 13.0f;
        cam.SnapToTarget();
        m_AutoStep = 10;
    }
    else if (m_AutoStep == 9 && m_AutoTime >= 9.6f) m_AutoStep = 10;
    if (m_AutoStep == 10 && m_AutoTime >= 10.6f) { if (!m_Targets.empty()) AutoTestLog("terrainlook look top"); m_AutoStep = 11; }
    if (m_AutoStep == 11 && m_AutoTime >= 11.2f) { AutoTestLog("terrainlook done"); m_AutoStep = 12; }
}

REGISTER_BATTLE_AUTOTEST("terrainlook", AutoTestTerrainLook)
