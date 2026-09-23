// ============================================================
// ProjectileProfile.cpp
// ============================================================
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace DirectX::SimpleMath;
using json = nlohmann::json;

namespace
{
    json V3(const Vector3& v) { return { v.x, v.y, v.z }; }
    Vector3 J3(const json& j, const Vector3& d)
    {
        return (j.is_array() && j.size() >= 3) ? Vector3(j[0], j[1], j[2]) : d;
    }

    std::vector<ProjectileProfile> g_Profiles;   // [0] = 組み込みの直進
    std::vector<uint32_t>          g_ShotCount;  // Alternate 用。プロファイルごとの発射数
    std::mt19937                   g_Rng{ 0x51A7u };

    void EnsureBuiltin()
    {
        if (!g_Profiles.empty()) return;
        ProjectileProfile s;
        s.name = "Straight";
        s.mode = Swarm::MotionMode::Straight;
        s.mirror = ProjectileProfile::Mirror::Fixed;
        g_Profiles.push_back(s);
        g_ShotCount.push_back(0);
    }
}

// ============================================================
// ProjectileProfile
// ============================================================
Swarm::Motion ProjectileProfile::ToMotion() const
{
    Swarm::Motion m;
    m.mode = static_cast<uint32_t>(mode);
    m.retargetRadius = retargetRadius;
    m.hitArea = (uint32_t)AreaProfileDB::IndexOf(hitArea);   // 無ければ 0 = 出さない
    m.hitAreaFlags = hitAreaOnExpire ? Swarm::kHitAreaOnExpire : 0u;
    m.c1 = c1;
    m.c2 = c2;
    return m;
}

json ProjectileProfile::ToJson() const
{
    json j;
    j["name"] = name;
    j["mode"] = static_cast<int>(mode);
    j["c1"] = V3(c1);
    j["c2"] = V3(c2);
    j["mirror"] = static_cast<int>(mirror);
    j["retargetRadius"] = retargetRadius;
    j["hitArea"] = hitArea;
    j["hitAreaOnExpire"] = hitAreaOnExpire;

    json pv;
    pv["speed"] = previewSpeed;
    pv["lifetime"] = previewLifetime;
    pv["radius"] = previewRadius;
    pv["damage"] = previewDamage;
    j["preview"] = pv;
    return j;
}

void ProjectileProfile::FromJson(const json& j)
{
    name = j.value("name", name);
    const int m = j.value("mode", 0);
    mode = (m >= 0 && m <= 2) ? static_cast<Swarm::MotionMode>(m) : Swarm::MotionMode::Straight;
    c1 = J3(j.value("c1", json()), c1);
    c2 = J3(j.value("c2", json()), c2);
    const int mi = j.value("mirror", 0);
    mirror = (mi >= 0 && mi <= 2) ? static_cast<Mirror>(mi) : Mirror::Fixed;
    retargetRadius = j.value("retargetRadius", retargetRadius);
    hitArea = j.value("hitArea", hitArea);
    hitAreaOnExpire = j.value("hitAreaOnExpire", hitAreaOnExpire);

    if (j.contains("preview"))
    {
        const json& pv = j["preview"];
        previewSpeed = pv.value("speed", previewSpeed);
        previewLifetime = pv.value("lifetime", previewLifetime);
        previewRadius = pv.value("radius", previewRadius);
        previewDamage = pv.value("damage", previewDamage);
    }
}

// ============================================================
// SwarmCommon.hlsli の SwarmBuildPath と同じ式（keepHeading = false の側）。
// 片方だけ直すと編集器の線と実際の弾道がずれるので、必ず揃えること
// ============================================================
void ProjectileProfile::BuildPreview(const Vector3& from, const Vector3& to,
    float sideSign, Vector3 out[4]) const
{
    const Vector3 chord = to - from;
    const float dist = chord.Length();
    const Vector3 fwd = (dist > 1e-4f) ? chord / dist : Vector3(0, 0, 1);

    const Vector3 up(0, 1, 0);
    Vector3 side = up.Cross(fwd);
    if (side.LengthSquared() > 1e-6f) side.Normalize();
    else side = Vector3(1, 0, 0);

    out[0] = from;
    out[1] = from + fwd * (c1.x * dist) + side * (c1.y * dist * sideSign) + up * (c1.z * dist);
    out[2] = from + fwd * (c2.x * dist) + side * (c2.y * dist * sideSign) + up * (c2.z * dist);
    out[3] = to;
}

// ============================================================
// 世界座標 → 制御点。
// rel = along * fwd + side * sideDir + up * upDir を (along, side, up) について解く。
// fwd が水平でない時は 3 本が直交しないので、内積ではなく逆行列で解く
// ============================================================
void ProjectileProfile::SetControlFromWorld(int which, const Vector3& from, const Vector3& to,
    float sideSign, const Vector3& worldPos)
{
    const Vector3 chord = to - from;
    const float dist = chord.Length();
    if (dist < 1e-4f) return;
    const Vector3 fwd = chord / dist;

    const Vector3 up(0, 1, 0);
    Vector3 side = up.Cross(fwd);
    if (side.LengthSquared() > 1e-6f) side.Normalize();
    else side = Vector3(1, 0, 0);
    side *= sideSign;

    // 行 = 基底。行ベクトル × 行列 の規約なので rel = coeff * M、coeff = rel * M^-1
    Matrix m = Matrix::Identity;
    m._11 = fwd.x;  m._12 = fwd.y;  m._13 = fwd.z;
    m._21 = side.x; m._22 = side.y; m._23 = side.z;
    m._31 = up.x;   m._32 = up.y;   m._33 = up.z;
    if (std::abs(m.Determinant()) < 1e-6f) return;   // 真上・真下へ撃っている

    const Vector3 coeff = Vector3::Transform(worldPos - from, m.Invert()) / dist;
    (which == 2 ? c2 : c1) = coeff;
}

// ============================================================
// ProjectileProfileDB
// ============================================================
void ProjectileProfileDB::LoadAll()
{
    g_Profiles.clear();
    g_ShotCount.clear();
    EnsureBuiltin();

    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(kDir, ec)) return;

    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(kDir, ec))
        if (e.is_regular_file() && e.path().extension() == ".json")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());   // 番号を起動ごとに変えない

    for (const auto& path : files)
    {
        if ((int)g_Profiles.size() >= kScratchRow) break;   // 最後の行は編集器用

        std::ifstream in(path);
        if (!in.is_open()) continue;

        json j;
        try { in >> j; }
        catch (const json::exception& e)
        {
            std::cout << "[ProjectileProfile] parse error: " << path.string()
                << " : " << e.what() << std::endl;
            continue;
        }

        ProjectileProfile p;
        p.name = path.stem().string();   // json に name が無ければファイル名
        p.FromJson(j);
        g_Profiles.push_back(p);
        g_ShotCount.push_back(0);
    }

    std::cout << "[ProjectileProfile] loaded " << (g_Profiles.size() - 1)
        << " profile(s) from " << kDir << std::endl;
}

int ProjectileProfileDB::Count()
{
    EnsureBuiltin();
    return (int)g_Profiles.size();
}

ProjectileProfile& ProjectileProfileDB::At(int index)
{
    EnsureBuiltin();
    if (index < 0 || index >= (int)g_Profiles.size()) index = 0;
    return g_Profiles[index];
}

int ProjectileProfileDB::IndexOf(const std::string& name)
{
    EnsureBuiltin();
    if (name.empty()) return 0;
    for (int i = 0; i < (int)g_Profiles.size(); ++i)
        if (g_Profiles[i].name == name) return i;
    return 0;
}

int ProjectileProfileDB::Add(const ProjectileProfile& p)
{
    EnsureBuiltin();
    if ((int)g_Profiles.size() >= kScratchRow) return -1;   // 最後の行は編集器用
    g_Profiles.push_back(p);
    g_ShotCount.push_back(0);
    return (int)g_Profiles.size() - 1;
}

bool ProjectileProfileDB::Save(int index)
{
    EnsureBuiltin();
    if (index <= 0 || index >= (int)g_Profiles.size()) return false;   // 0 番は組み込み

    const ProjectileProfile& p = g_Profiles[index];
    if (p.name.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);

    const std::string path = std::string(kDir) + p.name + ".json";
    std::ofstream out(path);
    if (!out.is_open())
    {
        std::cout << "[ProjectileProfile] save failed: " << path << std::endl;
        return false;
    }
    out << p.ToJson().dump(4);
    std::cout << "[ProjectileProfile] saved: " << path << std::endl;
    return true;
}

std::vector<Swarm::Motion> ProjectileProfileDB::BuildMotions()
{
    EnsureBuiltin();
    std::vector<Swarm::Motion> out;
    out.reserve(g_Profiles.size());
    for (const auto& p : g_Profiles) out.push_back(p.ToMotion());
    return out;
}

bool ProjectileProfileDB::NextMirror(int index)
{
    EnsureBuiltin();
    if (index < 0 || index >= (int)g_Profiles.size()) return false;

    switch (g_Profiles[index].mirror)
    {
    case ProjectileProfile::Mirror::Alternate:
        return (g_ShotCount[index]++ & 1u) != 0u;
    case ProjectileProfile::Mirror::Random:
        return (g_Rng() & 1u) != 0u;
    default:
        return false;
    }
}
