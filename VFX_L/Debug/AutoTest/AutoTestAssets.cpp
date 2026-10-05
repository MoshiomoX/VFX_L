// ============================================================
// AutoTestAssets.cpp
// TEMP-TEST: VFXL_BATTLE_AUTOTEST=assets
// VFXL_BATTLE_AUTOTEST=assets：新しい素材（砂漠・遺跡）を 1 包ずつプレイヤーの前に並べ、大きさ（m）を記録して撮る
// ============================================================
#include "Debug/AutoTest/AutoTestCommon.h"

namespace
{
    class AutoTestAssets final : public BattleAutoTest
    {
    public:
        using BattleAutoTest::BattleAutoTest;
        void UpdateGameplay(float) override { Run(); }

    private:
        void Run();
        std::vector<Entity> m_AutoAssetEntities;
    };
}

// ============================================================
// TEMP-TEST: 新しい素材の並べ見（VFXL_BATTLE_AUTOTEST=assets）
// 1 秒: 湧き停止・全消し・無敵。2 / 7 / 12 秒: 1 包ずつ（前の包は消す）プレイヤーの前へ格子に並べ、
// モデル毎の大きさ（ファイルの単位を掛けた m）を記録。各 3 秒後に "assets look <包>"（外から撮る）。
// VFXL_ASSET_SET=rocks なら岩の 3 組（Rock-Set / KayKit Forest の Rock_ / dglopez の *rock*）を並べた後、
// 各組の岩を 8〜20m に拡大して 2 列に積んだ「山の壁」を 3 本並べて撮る（"assets look wall" / "wall top"）
// ============================================================
void AutoTestAssets::Run()
{
    struct Pack { const char* dir; const char* contains; };   // contains: 名前にこれを含む物だけ（小文字比較、空 = 全部）
    static const Pack kDefault[] = {
        { "Assets/Model/dglopez_WesternDesert/FBX", "" },
        { "Assets/Model/Quaternius_UltimateNature/FBX", "" },
        { "Assets/Model/Quaternius_ModularRuins/FBX", "" },
    };
    static const Pack kRocks[] = {
        { "Assets/Model/Rock-Set", "" },
        { "Assets/Model/KayKit_Forest/fbx", "rock_" },
        { "Assets/Model/dglopez_WesternDesert/FBX", "rock" },
    };
    static const Pack kArrows[] = {
        { "Assets/VFX/Mesh", "gongjian" },
        { "Assets/VFX/Mesh", "gonjian" },
        { "Assets/VFX/Mesh", "jian0" },
    };
    static const Pack kArches[] = {   // Boss の門の候補（2026-10-03）
        { "Assets/Model/Quaternius_ModularRuins/FBX", "arch" },
        { "Assets/Model/Kenney_RetroFantasy/fbx", "gate" },
        { "Assets/Model/Rock-Set", "" },
    };
    static const std::string s_Set = [] {
        char v[16] = {};
        return GetEnvironmentVariableA("VFXL_ASSET_SET", v, sizeof(v)) > 0 ? std::string(v) : std::string();
    }();
    static const bool s_Rocks = (s_Set == "rocks");
    const Pack* kPacks = s_Rocks ? kRocks : (s_Set == "arrows" ? kArrows : (s_Set == "arches" ? kArches : kDefault));
    const int kPackCount = 3;
    static std::vector<std::shared_ptr<Model>> s_PackModels[3];   // 山の壁に使う（組毎）

    auto listFiles = [](const Pack& p)
        {
            std::vector<std::string> files;
            for (const auto& de : std::filesystem::recursive_directory_iterator(p.dir))
            {
                std::string ext = de.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (ext != ".fbx") continue;
                std::string name = de.path().filename().string();
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (*p.contains && name.find(p.contains) == std::string::npos) continue;
                files.push_back(de.path().generic_string());
            }
            std::sort(files.begin(), files.end());
            return files;
        };
    if (m_Registry.Has<LevelComponent>(m_Player))
        m_Registry.Get<LevelComponent>(m_Player).experience = 0.0f;
    m_Registry.Get<HealthComponent>(m_Player).invincible = true;

    if (m_AutoStep == 0 && m_AutoTime >= 1.0f)
    {
        m_ShowWireframe = m_ShowWandDebug = m_ShowGridDebug = false;
        m_Mobs.Director().enabled = false;
        m_Swarm.KillAll();
        if (m_Registry.Has<WandComponent>(m_Player)) m_Registry.Get<WandComponent>(m_Player).castingPaused = true;
        auto& cam = m_Camera.Camera();
        cam.distance = 14.0f;
        cam.SetPitch(25.0f);
        m_AutoStep = 1;
        return;
    }
    const int pack = m_AutoStep - 1;   // 0.. 並べる包
    if (m_AutoStep >= 1 && m_AutoStep <= kPackCount && m_AutoTime >= 2.0f + 5.0f * pack)
    {
        for (Entity e : m_AutoAssetEntities) if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
        m_AutoAssetEntities.clear();

        const std::vector<std::string> files = listFiles(kPacks[pack]);
        s_PackModels[pack].clear();

        // プレイヤーの前（カメラの奥）に 列 cols で並べる。間隔は大きい物に合わせて広め
        const auto& cam = m_Camera.Camera();
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        const int cols = (int)std::ceil(std::sqrt((float)files.size() * 1.6f));
        const float gap = (pack == 2 || s_Set == "arches") ? 5.0f : 3.0f;
        char line[256];
        for (int i = 0; i < (int)files.size(); ++i)
        {
            auto m = ResourceManager::Get().LoadModel(files[i]);
            if (!m) { snprintf(line, sizeof(line), "assets FAIL %s", files[i].c_str()); AutoTestLog(line); continue; }
            const float u = m->GetFileUnitScale();
            const Vector3 size = (m->GetBoundsMax() - m->GetBoundsMin()) * u;
            if (size.y > 0.4f) s_PackModels[pack].push_back(m);   // 小石は山に使わない
            snprintf(line, sizeof(line), "assets size %s unit %.3f  %.2f x %.2f x %.2f m",
                std::filesystem::path(files[i]).filename().string().c_str(), u, size.x, size.z, size.y);
            AutoTestLog(line);

            const int cx = i % cols, cz = i / cols;
            Vector3 pos = pp + f * (6.0f + cz * gap) + r * ((cx - (cols - 1) * 0.5f) * gap);
            pos.y = m_Grid.SampleHeight(pos.x, pos.z) - m->GetBoundsMin().y * u;
            Entity e = m_Registry.Create();
            TransformComponent tf;
            tf.position = pos;
            tf.scale = { u, u, u };
            m_Registry.Add<TransformComponent>(e, tf);
            ModelComponent mc;
            mc.model = m;
            m_Registry.Add<ModelComponent>(e, mc);
            m_AutoAssetEntities.push_back(e);
        }
        snprintf(line, sizeof(line), "assets pack %s: %d models", kPacks[pack].dir, (int)files.size());
        AutoTestLog(line);
        m_AutoStep += 100;   // 撮るのを待つ（下で戻す）
    }
    else if (m_AutoStep > 100 && m_AutoStep < 200)
    {
        const int p = m_AutoStep - 101;
        if (m_AutoTime >= 5.0f + 5.0f * p)
        {
            char line[128];
            snprintf(line, sizeof(line), "assets look %d", p);
            AutoTestLog(line);
            m_AutoStep = p + 2;
            if (m_AutoStep > kPackCount)
            {
                if (s_Rocks) m_AutoStep = 500;   // 山の壁へ
                else { AutoTestLog("assets done"); m_AutoStep = 999; }
            }
        }
    }
    else if (m_AutoStep == 500)
    {
        // ---- 山の壁: 組毎に 36m、手前の列 8〜12m・奥の列 14〜20m に拡大して隙間なく積む ----
        for (Entity e : m_AutoAssetEntities) if (m_Registry.IsValid(e)) m_Registry.Destroy(e);
        m_AutoAssetEntities.clear();
        auto& cam = m_Camera.Camera();
        Vector3 f = cam.GetForward(); f.y = 0.0f; f.Normalize();
        const Vector3 r(f.z, 0.0f, -f.x);
        const Vector3 pp = m_Registry.Get<TransformComponent>(m_Player).position;
        std::mt19937 rng(1234u);
        std::uniform_real_distribution<float> u01(0.0f, 1.0f);
        char line[160];
        for (int pk = 0; pk < kPackCount; ++pk)
        {
            const auto& models = s_PackModels[pk];
            if (models.empty()) continue;
            const float segCenter = (pk - 1) * 42.0f;
            for (int row = 0; row < 2; ++row)
            {
                const float step = row == 0 ? 5.0f : 7.0f;
                const float hMin = row == 0 ? 8.0f : 14.0f, hMax = row == 0 ? 12.0f : 20.0f;
                for (float x = -18.0f; x <= 18.0f; x += step)
                {
                    const auto& m = models[(size_t)(u01(rng) * models.size()) % models.size()];
                    const float unit = m->GetFileUnitScale();
                    const Vector3 lo = m->GetBoundsMin() * unit, hi = m->GetBoundsMax() * unit;
                    const float targetH = hMin + (hMax - hMin) * u01(rng);
                    const float s = targetH / (std::max)(hi.y - lo.y, 0.1f);
                    Vector3 pos = pp + f * (16.0f + row * 7.0f + u01(rng) * 2.0f) + r * (segCenter + x + u01(rng) * 2.0f);
                    pos.y = m_Grid.SampleHeight(pos.x, pos.z) - lo.y * s - targetH * 0.08f;   // 少し埋める（浮いて見えない）
                    Entity e = m_Registry.Create();
                    TransformComponent tf;
                    tf.position = pos;
                    tf.rotation = { 0.0f, u01(rng) * 360.0f, 0.0f };
                    tf.scale = { unit * s, unit * s, unit * s };
                    m_Registry.Add<TransformComponent>(e, tf);
                    ModelComponent mc;
                    mc.model = m;
                    m_Registry.Add<ModelComponent>(e, mc);
                    m_AutoAssetEntities.push_back(e);
                }
            }
            snprintf(line, sizeof(line), "assets wall %d (%s): %d rock models, left -> right", pk, kPacks[pk].dir, (int)models.size());
            AutoTestLog(line);
        }
        cam.distance = 12.0f;
        cam.SetPitch(4.0f);
        m_AutoStep = 501;
    }
    else if (m_AutoStep == 501 && m_AutoTime >= 20.0f)
    {
        AutoTestLog("assets look wall");
        m_Camera.Camera().distance = 45.0f;
        m_Camera.Camera().SetPitch(35.0f);
        m_AutoStep = 502;
    }
    else if (m_AutoStep == 502 && m_AutoTime >= 22.5f)
    {
        AutoTestLog("assets look walltop");
        m_AutoStep = 503;
    }
    else if (m_AutoStep == 503 && m_AutoTime >= 23.0f)
    {
        AutoTestLog("assets done");
        m_AutoStep = 999;
    }
}

REGISTER_BATTLE_AUTOTEST("assets", AutoTestAssets)
