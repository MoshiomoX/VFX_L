// ============================================================
// FrameProfiler.h
// 1 フレームの CPU 時間を区間ごとに測る（Debug 構成が重い理由の内訳を見る用）。
//   PROFILE_SCOPE("名前") を置いた区間の時間を積み、1 秒ごとに「1 フレームあたりの平均 ms」にまとめる。
//   名前は文字列リテラル（ポインタで見分ける）。入れ子にしてよい（表示は字下げ）。
//   Application::Run の最後で EndFrame。場面の面板から DrawImGui、自己テストは Summary を日志へ。
// GPU の時間は測らない（CPU が GPU を待った分は、待った区間 = 大抵 Present に出る）
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class FrameProfiler
{
public:
    static FrameProfiler& Get();

    void Begin(const char* name);
    void End(const char* name);
    void EndFrame();

    struct Row { const char* name; int depth; float ms; };
    const std::vector<Row>& Report() const { return m_Report; }   // 直近 1 秒の 1 フレーム平均
    float ReportFrameMs() const { return m_ReportFrameMs; }
    std::string Summary() const;   // 日志用の 1 行
    void DrawImGui();

private:
    struct Section { const char* name; int depth; double accum; int64_t start; };
    Section* Find(const char* name);

    std::vector<Section> m_Sections;   // 初めて通った順
    int m_Depth = 0;
    int m_Frames = 0;
    int64_t m_WindowStart = 0;
    int64_t m_Freq = 0;
    std::vector<Row> m_Report;
    float m_ReportFrameMs = 0.0f;
};

class ProfileScope
{
public:
    explicit ProfileScope(const char* name) : m_Name(name) { FrameProfiler::Get().Begin(name); }
    ~ProfileScope() { FrameProfiler::Get().End(m_Name); }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
private:
    const char* m_Name;
};

#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
#define PROFILE_SCOPE(name) ProfileScope PROFILE_CONCAT(profileScope_, __LINE__)(name)
