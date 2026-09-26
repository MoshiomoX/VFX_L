// ============================================================
// SceneLighting.h
// 場面の照明一式：主光（太陽 = 平行光）+ 半球の環境光 + 場景光源（位置を持つ点光源 1 つ）
//   ・向きは Unity の回転と同じく「見下ろす角度 pitch」と「水平の向き yaw」（度）で持つ。
//     Unity は左手系、こちらは右手系（SimpleMath の CreateLookAt）で X が鏡写しになる。
//     Unity の (50, -30) と画面上で同じ当たり方（カメラから見て右前上から）にするため yaw は +30
//   ・環境光は半球（上向きの面 = 空の色、下向きの面 = 地面の色）。高光は PS 側（Lighting.hlsli）
//   ・場景光源は既定で切。毎フレーム PointLightManager の先頭に積む（上限 64 の先着順でも必ず入る）。
//     位置は Lighting 面板の数値か 3D ギズモ（左ドラッグ）で動かす
// ============================================================
#pragma once
#include <SimpleMath.h>

class Renderer;
class GridWorld;
class CameraBase;

class SceneLighting
{
public:
    // pitch / yaw → 光の進む向き（正規化済み）
    DirectX::SimpleMath::Vector3 SunDirection() const;

    // Render の頭で：平行光と環境光を Renderer へ
    void Apply(Renderer& renderer) const;

    // 場景光源を点光源表へ積む。表の Upload（UpdateGameplay の CollectLights か
    // SceneBase::Render）より前に呼ぶこと。一時停止中も積む
    void SubmitPointLights() const;

    // 太陽の目印・場景光源のギズモと目印（ImGui のフレーム内で毎フレーム）。
    // player = 目印を出す位置の基準（null なら太陽の目印は出さない）。
    // allowGizmo = false の間はギズモを出さない（UI が左クリックを使っている時）
    void DrawMarkers(const DirectX::SimpleMath::Vector3* player, const GridWorld& grid,
        CameraBase* camera, bool allowGizmo);

    void DrawImGui(const DirectX::SimpleMath::Vector3* player);

private:
    void DrawSunMarker(const DirectX::SimpleMath::Vector3& player) const;
    void DrawSceneLightGizmo(const GridWorld& grid, CameraBase* camera, bool allowGizmo);

    // ---- 主光（太陽）。Unity の新規シーンの Directional Light に合わせた初期値 ----
    static constexpr float kSunPitchDefault = 50.0f;
    static constexpr float kSunYawDefault = 30.0f;
    float m_SunPitch = kSunPitchDefault;
    float m_SunYaw = kSunYawDefault;
    float m_LightColor[3] = { 1.0f, 0.957f, 0.839f };
    float m_LightIntensity = 1.0f;
    float m_AmbientSky[3] = { 0.40f, 0.44f, 0.50f };
    float m_AmbientGround[3] = { 0.22f, 0.20f, 0.18f };
    bool  m_ShowSunMarker = true;   // 玩家の頭上に太陽の目印と光の向きの矢印を出す

    // ---- 場景光源（位置を持つ点光源。既定は切。松明など局所の光に使う）----
    // PS 側は距離減衰 + 拡散 + GGX の高光（Shader/Common/Lighting.hlsli）
    bool    m_SceneLightOn = false;
    DirectX::SimpleMath::Vector3 m_SceneLightPos = { 0.0f, 6.0f, 0.0f };   // 出生点（原点）の上
    float   m_SceneLightColor[3] = { 1.0f, 0.9f, 0.75f };
    float   m_SceneLightRadius = 20.0f;                  // ここで光が 0 になる
    float   m_SceneLightIntensity = 1.2f;                // 真下の地面が平行光 1.0 の頃と同じくらい
    bool    m_SceneLightGizmo = true;                    // 3D ギズモを出す
    bool    m_SceneLightMarker = true;                   // 電球と地面の照射範囲を線で出す
};
