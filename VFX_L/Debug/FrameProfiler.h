// ============================================================
// FrameProfiler.h
// 1 フレームの時間を区間ごとに測る（どこが重いかの内訳を見る用）。
//   PROFILE_SCOPE("名前")     … CPU 時間だけ
//   PROFILE_SCOPE_GPU("名前") … CPU 時間 + GPU 時間（D3D11 の timestamp query。数フレーム遅れで読む）
//   区間の時間を積み、1 秒ごとに「1 フレームあたりの平均 ms」にまとめる。
//   名前は文字列リテラル（ポインタで見分ける）。入れ子にしてよい（表示は字下げ）。
//   Application::Run の頭で BeginFrame、最後で EndFrame。場面の面板から DrawImGui、自己テストは Summary を日志へ。
// GPU の値は区間の始めと終わりの GPU 時刻の差。CPU が遅くて GPU が命令待ちで遊んでいる時（Debug）は
// その待ちも入るので大きく出る。GPU が詰まっている時（Release）に意味がある
// ============================================================
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <d3d11.h>
#include <wrl/client.h>

class FrameProfiler
{
public:
    static FrameProfiler& Get();

    // GPU の計測を使う時だけ（Application の初期化で 1 回）
    void InitGpu(ID3D11Device* device, ID3D11DeviceContext* context);
    void ShutdownGpu();

    void BeginFrame();
    void Begin(const char* name, bool gpu = false);
    void End(const char* name);
    void EndFrame();

    struct Row { const char* name; int depth; float ms; float gpuMs; };   // gpuMs < 0 = 測っていない
    const std::vector<Row>& Report() const { return m_Report; }   // 直近 1 秒の 1 フレーム平均
    float ReportFrameMs() const { return m_ReportFrameMs; }
    std::string Summary() const;   // 日志用の 1 行（GPU を測った区間は "cpu/gpu"）
    void DrawImGui();

private:
    static constexpr int kGpuSlots = 4;   // GPU の結果を読むまでのフレーム数（リング）

    struct Section
    {
        const char* name; int depth;
        double accum; int64_t start;
        bool gpu;                 // 今フレーム GPU も測った
        double gpuAccum; int gpuFrames;
    };
    struct GpuSlot
    {
        Microsoft::WRL::ComPtr<ID3D11Query> disjoint;
        std::vector<Microsoft::WRL::ComPtr<ID3D11Query>> begin, end;   // 区間の添字ごと
        std::vector<uint8_t> issued;
        bool used = false;
    };
    int FindIndex(const char* name);
    void ResolveGpuSlot(GpuSlot& slot);

    std::vector<Section> m_Sections;   // 初めて通った順
    int m_Depth = 0;
    int m_Frames = 0;
    int64_t m_WindowStart = 0;
    int64_t m_Freq = 0;
    std::vector<Row> m_Report;
    float m_ReportFrameMs = 0.0f;

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    GpuSlot m_Gpu[kGpuSlots];
    int m_GpuCur = 0;
    bool m_GpuInFrame = false;
};

class ProfileScope
{
public:
    ProfileScope(const char* name, bool gpu = false) : m_Name(name) { FrameProfiler::Get().Begin(name, gpu); }
    ~ProfileScope() { FrameProfiler::Get().End(m_Name); }
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
private:
    const char* m_Name;
};

#define PROFILE_CONCAT_INNER(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_INNER(a, b)
#define PROFILE_SCOPE(name) ProfileScope PROFILE_CONCAT(profileScope_, __LINE__)(name)
#define PROFILE_SCOPE_GPU(name) ProfileScope PROFILE_CONCAT(profileScope_, __LINE__)(name, true)
