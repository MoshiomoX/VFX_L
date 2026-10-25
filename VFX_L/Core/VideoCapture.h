// ============================================================
// VideoCapture.h
// TEMP-TEST: 動画の書き出し（2026-10-23、作品紹介の動画用）。
//
// 環境変数 VFXL_CAPTURE=<出力のパス（拡張子なし）> で起動すると:
//   ・1 フレーム = 1/60 秒に固定（実時間に関係なく進む。重い場面でもコマ落ちしない）
//   ・音は装置を開かず、毎フレーム 1/60 秒ぶんを読み出して <パス>.wav へ（映像とずれない）
//   ・画面（UI まで描いた後のバックバッファ）を毎フレーム読み戻し、ffmpeg の標準入力へ流して <パス>.mp4 へ
// 書き出すのは SetRecording(true) の間だけ（場面の準備中は捨てる）。Finish() で閉じる。
//   VFXL_CAPTURE_W / VFXL_CAPTURE_H = 窓の大きさ（既定 1920×1080）
//   VFXL_FFMPEG = ffmpeg.exe のパス（既定 "ffmpeg"）
//   VFXL_CAPTURE_MUSIC=1 = BGM も録る（既定は効果音だけ。BGM は編集で 1 本通して乗せる）
//   VFXL_CAPTURE_AUTO=<始め秒>,<終わり秒> = 起動からこの間だけ書き出して閉じる（エディタなど、場面が録画を操作しない時）
// ============================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

class VideoCapture
{
public:
    static VideoCapture& Get();

    bool Enabled() const { return m_Enabled; }   // VFXL_CAPTURE が設定されている
    float FrameDt() const { return 1.0f / (float)kFps; }
    int Width() const { return m_Width; }
    int Height() const { return m_Height; }
    bool CaptureMusic() const { return m_Music; }
    // 書き出し中の時計（秒）= 進めたフレーム数 × 1/60。実時間で動く見た目（UI の動き・光線・液面）が使う
    double Time() const { return m_Time; }
    // ゲームの時間の進み（スロー）。1 フレームでゲームに渡す dt = 1/60 × これ。音は等速のまま
    void SetTimeScale(float s) { m_TimeScale = s; }
    float TimeScale() const { return m_TimeScale; }

    void SetRecording(bool on);
    bool Recording() const { return m_Recording; }
    uint32_t FramesWritten() const { return m_Frames; }
    // 書き出しを閉じる（ffmpeg の終了を待つ）。以後は何もしない
    void Finish();

    // Graphics::EndFrame から（Present の前）。録画中ならバックバッファを ffmpeg へ
    void OnFrame(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* backbuffer);
    // Application::Run から毎フレーム（AudioSystem::Update の後）。録画していなくても音は進める
    void PumpAudio();

    static constexpr int kFps = 60;
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;

private:
    VideoCapture();
    ~VideoCapture();
    bool StartVideo(int w, int h);
    void StartAudio();
    void CloseAudio();

    bool m_Enabled = false;
    bool m_Music = false;
    bool m_Recording = false;
    bool m_Finished = false;
    int m_Width = 1920, m_Height = 1080;
    std::string m_Base;          // 出力のパス（拡張子なし）
    std::string m_Ffmpeg = "ffmpeg";
    uint32_t m_Frames = 0;
    double m_Time = 0.0;
    float m_TimeScale = 1.0f;
    float m_AutoStart = -1.0f, m_AutoEnd = -1.0f;   // VFXL_CAPTURE_AUTO（-1 = 使わない）

    // 映像（ffmpeg の子プロセス）
    HANDLE m_PipeWrite = nullptr;
    HANDLE m_Process = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Staging;
    int m_StagingW = 0, m_StagingH = 0;

    // 音（16bit PCM の WAV）
    FILE* m_Wav = nullptr;
    uint32_t m_WavFrames = 0;
    std::vector<float> m_AudioBuf;
    std::vector<int16_t> m_AudioPcm;
};
