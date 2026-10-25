// ============================================================
// VideoCapture.cpp
// TEMP-TEST: 動画の書き出し（VideoCapture.h の説明）
// ============================================================
#include "Core/VideoCapture.h"
#include "Audio/AudioSystem.h"

namespace
{
    std::string Env(const char* name)
    {
        char buf[1024] = {};
        const DWORD n = GetEnvironmentVariableA(name, buf, sizeof(buf));
        return (n > 0 && n < sizeof(buf)) ? std::string(buf) : std::string();
    }

    void WriteU32(FILE* f, uint32_t v) { fwrite(&v, 4, 1, f); }
    void WriteU16(FILE* f, uint16_t v) { fwrite(&v, 2, 1, f); }
}

VideoCapture& VideoCapture::Get()
{
    static VideoCapture s;
    return s;
}

VideoCapture::VideoCapture()
{
    m_Base = Env("VFXL_CAPTURE");
    m_Enabled = !m_Base.empty();
    if (!m_Enabled) return;
    const std::string w = Env("VFXL_CAPTURE_W"), h = Env("VFXL_CAPTURE_H");
    if (!w.empty()) m_Width = (std::max)(320, atoi(w.c_str()));
    if (!h.empty()) m_Height = (std::max)(240, atoi(h.c_str()));
    const std::string ff = Env("VFXL_FFMPEG");
    if (!ff.empty()) m_Ffmpeg = ff;
    m_Music = Env("VFXL_CAPTURE_MUSIC") == "1";
    // VFXL_CAPTURE_AUTO=<始め秒>,<終わり秒>：場面の側で何もしなくても、起動からその秒の間だけ書き出して閉じる（エディタの撮影）
    const std::string autoRange = Env("VFXL_CAPTURE_AUTO");
    if (!autoRange.empty() && sscanf_s(autoRange.c_str(), "%f,%f", &m_AutoStart, &m_AutoEnd) != 2)
        m_AutoStart = m_AutoEnd = -1.0f;
    std::cout << "[Capture] " << m_Base << " " << m_Width << "x" << m_Height << " @" << kFps << " fps"
        << (m_Music ? " (with music)" : " (sfx only)") << std::endl;
}

VideoCapture::~VideoCapture()
{
    Finish();
}

void VideoCapture::SetRecording(bool on)
{
    if (!m_Enabled || m_Finished || on == m_Recording) return;
    m_Recording = on;
    if (on && !m_Wav) StartAudio();
    std::cout << "[Capture] recording " << (on ? "on" : "off") << " at frame " << m_Frames << std::endl;
}

bool VideoCapture::StartVideo(int w, int h)
{
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 16u << 20))
    {
        std::cout << "[Capture] CreatePipe failed" << std::endl;
        return false;
    }
    SetHandleInformation(writeEnd, HANDLE_FLAG_INHERIT, 0);   // 書く側は子へ渡さない（閉じた時に EOF が届くように）

    const std::string log = m_Base + "_ffmpeg.log";
    HANDLE logFile = CreateFileA(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
        "\"%s\" -hide_banner -loglevel warning -y -f rawvideo -pix_fmt rgba -video_size %dx%d -framerate %d -i - "
        "-vf scale=out_color_matrix=bt709:out_range=tv -c:v libx264 -preset fast -crf 12 -pix_fmt yuv420p "
        "-colorspace bt709 -color_primaries bt709 -color_trc bt709 -movflags +faststart \"%s.mp4\"",
        m_Ffmpeg.c_str(), w, h, kFps, m_Base.c_str());

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = readEnd;
    si.hStdOutput = logFile != INVALID_HANDLE_VALUE ? logFile : nullptr;
    si.hStdError = logFile != INVALID_HANDLE_VALUE ? logFile : nullptr;
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(readEnd);
    if (logFile != INVALID_HANDLE_VALUE) CloseHandle(logFile);
    if (!ok)
    {
        std::cout << "[Capture] could not start ffmpeg: " << m_Ffmpeg << std::endl;
        CloseHandle(writeEnd);
        return false;
    }
    CloseHandle(pi.hThread);
    m_Process = pi.hProcess;
    m_PipeWrite = writeEnd;
    std::cout << "[Capture] ffmpeg started -> " << m_Base << ".mp4" << std::endl;
    return true;
}

void VideoCapture::OnFrame(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* backbuffer)
{
    if (!m_Enabled || !m_Recording || m_Finished || !backbuffer) return;

    D3D11_TEXTURE2D_DESC bd = {};
    backbuffer->GetDesc(&bd);
    if (!m_Staging || m_StagingW != (int)bd.Width || m_StagingH != (int)bd.Height)
    {
        if (m_PipeWrite && (m_StagingW != (int)bd.Width || m_StagingH != (int)bd.Height))
        {
            std::cout << "[Capture] backbuffer size changed, frame skipped" << std::endl;
            return;   // 途中で大きさは変えられない
        }
        D3D11_TEXTURE2D_DESC sd = bd;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.BindFlags = 0;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.MiscFlags = 0;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        m_Staging.Reset();
        if (FAILED(device->CreateTexture2D(&sd, nullptr, &m_Staging))) return;
        m_StagingW = (int)bd.Width;
        m_StagingH = (int)bd.Height;
    }
    if (!m_PipeWrite && !StartVideo(m_StagingW, m_StagingH))
    {
        Finish();   // 録画を始めた時に開いた WAV も閉じる（長さを書き戻す。m_Finished だけ立てると Finish が素通りになっていた）
        return;
    }

    ctx->CopyResource(m_Staging.Get(), backbuffer);
    D3D11_MAPPED_SUBRESOURCE ms = {};
    if (FAILED(ctx->Map(m_Staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) return;
    const size_t rowBytes = (size_t)m_StagingW * 4;
    const uint8_t* src = static_cast<const uint8_t*>(ms.pData);
    bool failed = false;
    if (ms.RowPitch == rowBytes)
    {
        DWORD written = 0;
        failed = !WriteFile(m_PipeWrite, src, (DWORD)(rowBytes * m_StagingH), &written, nullptr);
    }
    else
    {
        for (int y = 0; y < m_StagingH && !failed; ++y)
        {
            DWORD written = 0;
            failed = !WriteFile(m_PipeWrite, src + (size_t)y * ms.RowPitch, (DWORD)rowBytes, &written, nullptr);
        }
    }
    ctx->Unmap(m_Staging.Get(), 0);
    if (failed)
    {
        std::cout << "[Capture] pipe write failed (ffmpeg exited?)" << std::endl;
        Finish();
        return;
    }
    ++m_Frames;
}

void VideoCapture::StartAudio()
{
    const std::string path = m_Base + ".wav";
    if (fopen_s(&m_Wav, path.c_str(), "wb") != 0 || !m_Wav) { m_Wav = nullptr; return; }
    // 中身の長さは閉じる時に書き直す
    fwrite("RIFF", 1, 4, m_Wav); WriteU32(m_Wav, 0); fwrite("WAVE", 1, 4, m_Wav);
    fwrite("fmt ", 1, 4, m_Wav); WriteU32(m_Wav, 16); WriteU16(m_Wav, 1); WriteU16(m_Wav, (uint16_t)kChannels);
    WriteU32(m_Wav, kSampleRate); WriteU32(m_Wav, kSampleRate * kChannels * 2); WriteU16(m_Wav, (uint16_t)(kChannels * 2)); WriteU16(m_Wav, 16);
    fwrite("data", 1, 4, m_Wav); WriteU32(m_Wav, 0);
    m_WavFrames = 0;
}

void VideoCapture::CloseAudio()
{
    if (!m_Wav) return;
    const uint32_t dataBytes = m_WavFrames * kChannels * 2;
    fseek(m_Wav, 4, SEEK_SET); WriteU32(m_Wav, 36 + dataBytes);
    fseek(m_Wav, 40, SEEK_SET); WriteU32(m_Wav, dataBytes);
    fclose(m_Wav);
    m_Wav = nullptr;
}

void VideoCapture::PumpAudio()
{
    if (!m_Enabled) return;
    const uint32_t frames = kSampleRate / kFps;   // 800
    m_AudioBuf.assign((size_t)frames * kChannels, 0.0f);
    AudioSystem::Get().ReadOffline(m_AudioBuf.data(), frames);   // 装置が無い時は無音のまま
    m_Time += 1.0 / (double)kFps;
    if (m_AutoStart >= 0.0f && !m_Finished)
    {
        if (!m_Recording && m_Time >= m_AutoStart && m_Time < m_AutoEnd) SetRecording(true);
        if (m_Time >= m_AutoEnd)
        {
            Finish();
            PostQuitMessage(0);
            return;
        }
    }
    if (!m_Recording || m_Finished || !m_Wav) return;
    m_AudioPcm.resize(m_AudioBuf.size());
    for (size_t i = 0; i < m_AudioBuf.size(); ++i)
    {
        const float s = (std::max)(-1.0f, (std::min)(1.0f, m_AudioBuf[i]));
        m_AudioPcm[i] = (int16_t)std::lround(s * 32767.0f);
    }
    fwrite(m_AudioPcm.data(), sizeof(int16_t), m_AudioPcm.size(), m_Wav);
    m_WavFrames += frames;
}

void VideoCapture::Finish()
{
    if (!m_Enabled || m_Finished) return;
    m_Finished = true;
    m_Recording = false;
    CloseAudio();
    if (m_PipeWrite)
    {
        CloseHandle(m_PipeWrite);   // ffmpeg に EOF
        m_PipeWrite = nullptr;
    }
    if (m_Process)
    {
        WaitForSingleObject(m_Process, 120000);
        DWORD code = 0;
        GetExitCodeProcess(m_Process, &code);
        CloseHandle(m_Process);
        m_Process = nullptr;
        std::cout << "[Capture] done: " << m_Frames << " frames, ffmpeg exit " << code << std::endl;
    }
    m_Staging.Reset();
}
