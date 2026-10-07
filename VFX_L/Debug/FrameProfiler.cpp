// ============================================================
// FrameProfiler.cpp
// ============================================================
#include "Debug/FrameProfiler.h"
#include "imgui.h"

#include <windows.h>

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

void FrameProfiler::InitGpu(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
    for (auto& slot : m_Gpu)
    {
        D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        if (FAILED(device->CreateQuery(&qd, &slot.disjoint)))
        {
            m_Device = nullptr;
            m_Context = nullptr;
            return;
        }
    }
}

void FrameProfiler::ShutdownGpu()
{
    for (auto& slot : m_Gpu) slot = GpuSlot{};
    m_Device = nullptr;
    m_Context = nullptr;
    m_GpuInFrame = false;
}

int FrameProfiler::FindIndex(const char* name)
{
    for (int i = 0; i < (int)m_Sections.size(); ++i)
        if (m_Sections[i].name == name) return i;
    m_Sections.push_back({ name, m_Depth, 0.0, 0, false, 0.0, 0 });
    return (int)m_Sections.size() - 1;
}

// kGpuSlots フレーム前の結果を読む（まだなら捨てる。待たない）
void FrameProfiler::ResolveGpuSlot(GpuSlot& slot)
{
    if (!slot.used) return;
    slot.used = false;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj = {};
    if (m_Context->GetData(slot.disjoint.Get(), &dj, sizeof(dj), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK
        || dj.Disjoint || dj.Frequency == 0)
        return;
    for (size_t i = 0; i < slot.issued.size() && i < m_Sections.size(); ++i)
    {
        if (!slot.issued[i]) continue;
        UINT64 t0 = 0, t1 = 0;
        if (m_Context->GetData(slot.begin[i].Get(), &t0, sizeof(t0), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (m_Context->GetData(slot.end[i].Get(), &t1, sizeof(t1), D3D11_ASYNC_GETDATA_DONOTFLUSH) != S_OK) continue;
        if (t1 < t0) continue;
        m_Sections[i].gpuAccum += (double)(t1 - t0) * 1000.0 / (double)dj.Frequency;
        ++m_Sections[i].gpuFrames;
    }
}

void FrameProfiler::BeginFrame()
{
    if (!m_Context) return;
    GpuSlot& slot = m_Gpu[m_GpuCur];
    ResolveGpuSlot(slot);
    std::fill(slot.issued.begin(), slot.issued.end(), (uint8_t)0);
    m_Context->Begin(slot.disjoint.Get());
    slot.used = true;
    m_GpuInFrame = true;
}

void FrameProfiler::Begin(const char* name, bool gpu)
{
    const int i = FindIndex(name);
    Section& s = m_Sections[i];
    s.start = Now();
    s.gpu = gpu && m_GpuInFrame;
    if (s.gpu)
    {
        GpuSlot& slot = m_Gpu[m_GpuCur];
        if (slot.begin.size() <= (size_t)i)
        {
            slot.begin.resize(i + 1);
            slot.end.resize(i + 1);
            slot.issued.resize(i + 1, 0);
        }
        if (!slot.begin[i])
        {
            D3D11_QUERY_DESC qd = { D3D11_QUERY_TIMESTAMP, 0 };
            if (FAILED(m_Device->CreateQuery(&qd, &slot.begin[i])) || FAILED(m_Device->CreateQuery(&qd, &slot.end[i])))
            {
                slot.begin[i].Reset();
                slot.end[i].Reset();
                s.gpu = false;
            }
        }
        if (s.gpu)
        {
            m_Context->End(slot.begin[i].Get());   // timestamp は End で打つ
            slot.issued[i] = 1;
        }
    }
    ++m_Depth;
}

void FrameProfiler::End(const char* name)
{
    --m_Depth;
    const int i = FindIndex(name);
    Section& s = m_Sections[i];
    s.accum += (double)(Now() - s.start);
    if (s.gpu)
    {
        m_Context->End(m_Gpu[m_GpuCur].end[i].Get());
        s.gpu = false;
    }
}

void FrameProfiler::EndFrame()
{
    if (m_GpuInFrame)
    {
        m_Context->End(m_Gpu[m_GpuCur].disjoint.Get());
        m_GpuCur = (m_GpuCur + 1) % kGpuSlots;
        m_GpuInFrame = false;
    }

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
        const float gpuMs = (s.gpuFrames > 0) ? (float)(s.gpuAccum / s.gpuFrames) : -1.0f;
        m_Report.push_back({ s.name, s.depth, (float)(s.accum * toMs), gpuMs });
        s.accum = 0.0;
        s.gpuAccum = 0.0;
        s.gpuFrames = 0;
    }
    m_ReportFrameMs = (float)(elapsed * 1000.0 / m_Frames);
    m_Frames = 0;
    m_WindowStart = now;
}

std::string FrameProfiler::Summary() const
{
    std::string out;
    char buf[160];
    snprintf(buf, sizeof(buf), "frame %.2f", m_ReportFrameMs);
    out += buf;
    for (const Row& r : m_Report)
    {
        if (r.gpuMs >= 0.0f)
            snprintf(buf, sizeof(buf), " | %s%s %.2f/%.2f", std::string((size_t)r.depth, '>').c_str(), r.name, r.ms, r.gpuMs);
        else
            snprintf(buf, sizeof(buf), " | %s%s %.2f", std::string((size_t)r.depth, '>').c_str(), r.name, r.ms);
        out += buf;
    }
    return out;
}

void FrameProfiler::DrawImGui()
{
    if (!ImGui::CollapsingHeader("Frame Profiler")) return;
    ImGui::Text("frame %.2f ms (1 s average).  CPU ms / GPU ms", m_ReportFrameMs);
    ImGui::TextDisabled("GPU = timestamp gap; includes GPU idle time when the CPU is the bottleneck");
    for (const Row& r : m_Report)
    {
        ImGui::Indent(12.0f * r.depth + 1.0f);
        if (r.gpuMs >= 0.0f)
            ImGui::Text("%-28s %6.2f  %6.2f", r.name, r.ms, r.gpuMs);
        else
            ImGui::Text("%-28s %6.2f", r.name, r.ms);
        ImGui::Unindent(12.0f * r.depth + 1.0f);
    }
}
