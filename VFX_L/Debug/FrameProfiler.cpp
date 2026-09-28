// ============================================================
// FrameProfiler.cpp
// ============================================================
#include "Debug/FrameProfiler.h"
#include "imgui.h"
#include <windows.h>
#include <cstdio>

namespace
{
    int64_t Now()
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        return t.QuadPart;
    }
}

FrameProfiler& FrameProfiler::Get()
{
    static FrameProfiler s;
    return s;
}

FrameProfiler::Section* FrameProfiler::Find(const char* name)
{
    for (auto& s : m_Sections)
        if (s.name == name) return &s;
    m_Sections.push_back({ name, m_Depth, 0.0, 0 });
    return &m_Sections.back();
}

void FrameProfiler::Begin(const char* name)
{
    Section* s = Find(name);
    s->start = Now();
    ++m_Depth;
}

void FrameProfiler::End(const char* name)
{
    --m_Depth;
    Section* s = Find(name);
    s->accum += (double)(Now() - s->start);
}

void FrameProfiler::EndFrame()
{
    if (m_Freq == 0)
    {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        m_Freq = f.QuadPart;
        m_WindowStart = Now();
    }
    ++m_Frames;
    const int64_t now = Now();
    const double elapsed = (double)(now - m_WindowStart) / (double)m_Freq;
    if (elapsed < 1.0) return;

    m_Report.clear();
    const double toMs = 1000.0 / (double)m_Freq / (double)m_Frames;
    for (auto& s : m_Sections)
    {
        m_Report.push_back({ s.name, s.depth, (float)(s.accum * toMs) });
        s.accum = 0.0;
    }
    m_ReportFrameMs = (float)(elapsed * 1000.0 / m_Frames);
    m_Frames = 0;
    m_WindowStart = now;
}

std::string FrameProfiler::Summary() const
{
    std::string out;
    char buf[128];
    snprintf(buf, sizeof(buf), "frame %.2f", m_ReportFrameMs);
    out += buf;
    for (const Row& r : m_Report)
    {
        snprintf(buf, sizeof(buf), " | %s%s %.2f", std::string((size_t)r.depth, '>').c_str(), r.name, r.ms);
        out += buf;
    }
    return out;
}

void FrameProfiler::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Frame Profiler (CPU)")) return;
    ImGui::Text("frame %.2f ms (1 s average)", m_ReportFrameMs);
    for (const Row& r : m_Report)
    {
        ImGui::Indent(12.0f * r.depth + 1.0f);
        ImGui::Text("%-28s %6.2f ms", r.name, r.ms);
        ImGui::Unindent(12.0f * r.depth + 1.0f);
    }
}
