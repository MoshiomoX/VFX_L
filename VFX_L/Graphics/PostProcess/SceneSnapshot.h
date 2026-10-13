// ============================================================
// SceneSnapshot.h
// 戦闘の一コマの写真（2026-10-09 ユーザー：「リザルトの背景はエフェクトの爆発の一コマ」）。
//   戦闘シーンが爆発（メテオ・爆発・光線・Boss のスラム）の山場で、シーンの HDR RT を
//   UI を描く前に写し取っておき（Capture）、リザルトがそれを背景に敷く（DrawBackdrop）。
//   シーンをまたいで持つので 1 つだけ（Get）。中身は HDR の値のまま（R16G16B16A16_FLOAT）なので、
//   リザルトの HDR RT へ描けば、トーンマップ・bloom を通って元の画面と同じ見え方になる。
//
// どの瞬間を写すかは HighlightPicker（戦闘シーンが持つ）：爆発が出たら少し待って（火球が膨らむまで）、
// 直前 1 秒の爆発の数がそれまでで一番多ければ写す（同点なら新しい方 = 後半の派手な所が残りやすい）。
// 1 回も爆発が無かったプレイは、力尽きた / クリアの瞬間を写す（戦闘シーン側）。
//
// DrawBackdrop はタイトルの暗い幕（左側を暗く。文字を読みやすく）にも使う（Backdrop::scrim）。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <deque>
#include <memory>

class Graphics;
class VertexShader;
class PixelShader;

class SceneSnapshot
{
public:
    static SceneSnapshot& Get();

    // 今のシーンの HDR RT（ここまで描いた分）を写す。UI を描く前に呼ぶ
    bool Capture(Graphics& gfx);
    bool Has() const { return m_SRV != nullptr; }
    void Clear();      // 写真を捨てる（新しいプレイの始め）
    void Shutdown();   // アプリの終わり（デバイスより先に放す）

    // 写した一コマの情報（リザルトの「ハイライト」の時刻）
    float shotTime = 0.0f;     // 遊んだ時間（秒）
    bool  highlight = false;   // true = 爆発の山場、false = 最後の瞬間

    struct Backdrop
    {
        bool  scrim = false;   // true = 写真を使わず、今の画面の上に黒い幕だけ（タイトル）
        DirectX::SimpleMath::Vector2 photoCenter = { 0.62f, 0.5f };   // 写真の真ん中を置く画面の位置（0〜1）
        float zoom = 0.8f;             // 写真の何割を画面の幅に出すか（小さいほど寄る）
        float brightness = 0.9f;
        float shadeStrength = 0.85f;   // 左側の暗さ（0〜1）
        float shadeFrom = 0.25f;       // 画面の横位置：ここより左が一番暗い
        float shadeTo = 0.72f;         // ここより右は暗くしない
        float vignette = 0.45f;
        float desaturate = 0.12f;
        float bottomShade = 0.35f;     // 下端（操作の案内）の暗さ
    };
    // シーンの HDR RT へ全画面で描く（写真モードは不透明、scrim は乗算済みアルファで重ねる）
    void DrawBackdrop(ID3D11DeviceContext* ctx, Graphics& gfx, const Backdrop& b);

private:
    bool EnsurePass(ID3D11Device* device);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Tex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
    UINT m_Width = 0, m_Height = 0;

    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader>  m_PS;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_ScrimBlend;   // 乗算済みアルファ（色は足さず、下を暗くする）
    bool m_PassTried = false;
};

// ============================================================
// どの瞬間を写すか（戦闘シーンが 1 つ持つ）
// ============================================================
class HighlightPicker
{
public:
    float window = 1.0f;   // この秒数の中の爆発を数える
    float delay = 0.15f;   // 爆発が出てから写すまで（火球・閃光が膨らむのを待つ）

    void Reset() { m_Times.clear(); m_PendingAt = -1.0f; m_Best = 0; }
    // このフレームに出た爆発の数（SwarmSystem::ConsumeShakeAreas など）。t = 遊んだ時間
    void OnExplosions(int count, float t)
    {
        for (int i = 0; i < count; ++i) m_Times.push_back(t);
        if (count > 0 && m_PendingAt < 0.0f) m_PendingAt = t + delay;
    }
    // 今のフレームで写すべきか（true を返したら、そのフレームの描画で Capture する）
    bool ShouldCapture(float t)
    {
        if (m_PendingAt < 0.0f || t < m_PendingAt) return false;
        m_PendingAt = -1.0f;
        while (!m_Times.empty() && m_Times.front() < t - window) m_Times.pop_front();
        const int score = (int)m_Times.size();
        if (score <= 0 || score < m_Best) return false;
        m_Best = score;
        return true;
    }
    int Best() const { return m_Best; }

private:
    std::deque<float> m_Times;
    float m_PendingAt = -1.0f;
    int   m_Best = 0;
};
