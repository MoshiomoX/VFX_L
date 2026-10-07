// ============================================================
// DifficultyCurve.cpp
// ============================================================
#include "Enemy/DifficultyCurve.h"
#include "ResourcePaths.h"
#include "imgui.h"

#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ============================================================
// 既定値（2026-10-04）
//   1 分で毎秒片付けるべき HP は以前の約 1.4 倍、6 分で約 0.7 倍、8 分で約 0.55 倍（第 1 面、スプリッター込み）。
//   ダメージは 10 分で 1.25 倍（以前は 2.2 倍。6 分で 1 発 26 → 17）
//   2026-10-07 ユーザー：最初の 2 分の湧く数を減らす（2 分以降は変えない）。
//   0 分 2.0 → 1.0 体/秒、1 分に点を足して 1.8。最初の 2 分の合計は約 300 体 → 約 215 体（−28%）。
//   湧く環も 1.5 倍に広げた（SpawnDirector）ので、序盤は寄ってくるまでの間も長い
// ============================================================
void DifficultyCurve::Reset()
{
    points = {
        {  0.0f, 1.0f, 1.00f, 1.00f },
        {  1.0f, 1.8f, 1.08f, 1.03f },
        {  2.0f, 3.0f, 1.15f, 1.05f },
        {  4.0f, 3.7f, 1.30f, 1.10f },
        {  6.0f, 4.2f, 1.45f, 1.15f },
        {  8.0f, 4.7f, 1.60f, 1.20f },
        { 10.0f, 5.2f, 1.75f, 1.25f },
    };
}

DifficultyCurve::Sample DifficultyCurve::Evaluate(float minutes) const
{
    Sample s;
    if (points.empty()) return s;
    auto at = [](const Point& p) { return Sample{ p.spawnRate, p.hpMul, p.damageMul }; };
    if (points.size() == 1 || minutes <= points.front().minute) return at(points.front());

    // 区間を探す。最後の点より後は最後の区間を延長する
    size_t i = 1;
    while (i + 1 < points.size() && minutes > points[i].minute) ++i;
    const Point& a = points[i - 1];
    const Point& b = points[i];
    const float span = (std::max)(1.0e-4f, b.minute - a.minute);
    const float t = (minutes - a.minute) / span;   // 最後の区間では 1 を超えてよい（延長）
    s.spawnRate = a.spawnRate + (b.spawnRate - a.spawnRate) * t;
    s.hpMul = a.hpMul + (b.hpMul - a.hpMul) * t;
    s.damageMul = a.damageMul + (b.damageMul - a.damageMul) * t;
    s.spawnRate = (std::max)(0.0f, s.spawnRate);
    s.hpMul = (std::max)(0.05f, s.hpMul);
    s.damageMul = (std::max)(0.0f, s.damageMul);
    return s;
}

// ============================================================
// 保存 / 読み込み（[[分, 湧き, HP, ダメージ], ...]）
// ============================================================
bool DifficultyCurve::Save(const char* path) const
{
    const char* file = path ? path : Res::Cfg::Difficulty;
    json root;
    json arr = json::array();
    for (const Point& p : points)
        arr.push_back(json::array({ p.minute, p.spawnRate, p.hpMul, p.damageMul }));
    root["points"] = arr;

    std::ofstream ofs(file);
    if (!ofs.is_open())
    {
        std::cout << "[Error] Difficulty: failed to save " << file << std::endl;
        return false;
    }
    ofs << root.dump(4);
    std::cout << "[OK] Difficulty saved: " << file << std::endl;
    return true;
}

bool DifficultyCurve::Load(const char* path)
{
    const char* file = path ? path : Res::Cfg::Difficulty;
    std::ifstream ifs(file);
    if (!ifs.is_open()) return false;   // 未保存はエラーではない（既定値のまま）

    json root;
    try { ifs >> root; }
    catch (const json::exception& e)
    {
        std::cout << "[Error] Difficulty: json parse error: " << e.what() << std::endl;
        return false;
    }
    if (!root.contains("points") || !root["points"].is_array()) return false;

    std::vector<Point> loaded;
    for (const auto& j : root["points"])
    {
        if (!j.is_array() || j.size() < 4) continue;
        loaded.push_back({ j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() });
    }
    if (loaded.empty()) return false;
    std::sort(loaded.begin(), loaded.end(), [](const Point& a, const Point& b) { return a.minute < b.minute; });
    points = std::move(loaded);
    std::cout << "[OK] Difficulty loaded: " << file << " (" << points.size() << " points)" << std::endl;
    return true;
}

// ============================================================
// ImGui：点の表 + 「毎秒湧く HP（湧き × HP）」とダメージの曲線
// ============================================================
void DifficultyCurve::DrawImGui(float nowMinutes)
{
    const Sample now = Evaluate(nowMinutes);
    ImGui::Text("now %.1f min: spawn %.2f /s  HP x%.2f  damage x%.2f", nowMinutes, now.spawnRate, now.hpMul, now.damageMul);

    ImGui::TextDisabled("   min     spawn/s    HP x     dmg x");
    bool sortNeeded = false;
    int removeAt = -1;
    for (size_t i = 0; i < points.size(); ++i)
    {
        Point& p = points[i];
        ImGui::PushID((int)i);
        ImGui::SetNextItemWidth(60.0f);
        if (ImGui::DragFloat("##min", &p.minute, 0.05f, 0.0f, 60.0f, "%.1f")) sortNeeded = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("##spawn", &p.spawnRate, 0.05f, 0.0f, 100.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##hp", &p.hpMul, 0.01f, 0.05f, 50.0f, "%.2f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60.0f);
        ImGui::DragFloat("##dmg", &p.damageMul, 0.01f, 0.0f, 50.0f, "%.2f");
        ImGui::SameLine();
        if (ImGui::SmallButton("x") && points.size() > 1) removeAt = (int)i;
        ImGui::PopID();
    }
    if (removeAt >= 0) points.erase(points.begin() + removeAt);
    if (sortNeeded)
        std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.minute < b.minute; });

    if (ImGui::Button("+ Point"))
    {
        Point p = points.empty() ? Point{} : points.back();
        p.minute += 2.0f;
        points.push_back(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save##diff")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Load##diff")) Load();
    ImGui::SameLine();
    if (ImGui::Button("Reset##diff")) Reset();

    // 0〜12 分を 0.5 分刻みで。「湧き × HP」= 毎秒片付けるべき HP の目安（雑魚 1 体 HP 15 を 1 として）
    constexpr int kN = 25;
    float load[kN], dmg[kN];
    float maxLoad = 0.0f;
    for (int k = 0; k < kN; ++k)
    {
        const Sample s = Evaluate(0.5f * (float)k);
        load[k] = s.spawnRate * s.hpMul;
        dmg[k] = s.damageMul;
        maxLoad = (std::max)(maxLoad, load[k]);
    }
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "spawn x HP (0-12 min)  now %.1f", now.spawnRate * now.hpMul);
    ImGui::PlotLines("##load", load, kN, 0, overlay, 0.0f, maxLoad * 1.1f, ImVec2(0.0f, 70.0f));
    snprintf(overlay, sizeof(overlay), "damage x (0-12 min)  now %.2f", now.damageMul);
    ImGui::PlotLines("##dmg", dmg, kN, 0, overlay, 0.0f, 3.0f, ImVec2(0.0f, 50.0f));
}
