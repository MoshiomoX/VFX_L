// ============================================================
// SceneLighting.h
// 場面の照明一式：主光（太陽 = 平行光）+ 半球の環境光 + 場景光源（位置を持つ点光源 1 つ）
//   ・向きは Unity の回転と同じく「見下ろす角度 pitch」と「水平の向き yaw」（度）で持つ。
//     Unity は左手系、こちらは右手系（SimpleMath の CreateLookAt）で X が鏡写しになる。
//     Unity の (50, -30) と画面上で同じ当たり方（カメラから見て右前上から）にするため yaw は +30
//   ・環境光は半球（上向きの面 = 空の色、下向きの面 = 地面の色）。高光は PS 側（Lighting.hlsli）
//   ・場景光源は既定で切。毎フレーム PointLightManager の先頭に積む（上限 64 の先着順でも必ず入る）。
//     位置は Lighting 面板の数値か 3D ギズモ（左ドラッグ）で動かす
//   ・空（SkyRenderer のグラデーション）と距離の霧。霧の色は既定で空の地平線の色に揃える
//     （遠くの地形が空へ溶ける）。色は全部線形 HDR
// ============================================================
#pragma once
#include "Graphics/Renderer/SkyRenderer.h"
#include <SimpleMath.h>

class Renderer;
class GridWorld;
class CameraBase;

class SceneLighting
{
public:
    // pitch / yaw → 光の進む向き（正規化済み）
    DirectX::SimpleMath::Vector3 SunDirection() const;

    bool Init(ID3D11Device* device);   // 空のシェーダー

    // Render の頭で：平行光・環境光・霧を Renderer へ
    void Apply(Renderer& renderer) const;
    // 場面の終わりに：霧を切り、面板で切ったかもしれない sRGB 解码を既定（入）へ戻す（Renderer は他の場面と共有）
    static void ClearFog(Renderer& renderer);

    // 空を画面全体に描く（場面の描画の一番最初。深度は触らない）
    void DrawSky(ID3D11DeviceContext* context, CameraBase* camera) const;

    // 場景光源を点光源表へ積む。表の Upload（UpdateGameplay の CollectLights か
    // SceneBase::Render）より前に呼ぶこと。一時停止中も積む
    void SubmitPointLights() const;

    // 太陽の目印・場景光源のギズモと目印（ImGui のフレーム内で毎フレーム）。
    // player = 目印を出す位置の基準（null なら太陽の目印は出さない）。
    // allowGizmo = false の間はギズモを出さない（UI が左クリックを使っている時）
    void DrawMarkers(const DirectX::SimpleMath::Vector3* player, const GridWorld& grid,
        CameraBase* camera, bool allowGizmo);

    void DrawImGui(const DirectX::SimpleMath::Vector3* player);

    void SetAlbedoSrgb(bool on) { m_AlbedoSrgb = on; }

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
    float m_AmbientSky[3] = { 0.38f, 0.45f, 0.58f };      // 青い空の照り返し
    float m_AmbientGround[3] = { 0.20f, 0.22f, 0.14f };   // 草地の照り返し
    bool  m_ShowSunMarker = false;  // 玩家の頭上に太陽の目印と光の向きの矢印を出す（調試用。既定で切）

    // ---- 場景光源（位置を持つ点光源。既定は切。松明など局所の光に使う）----
    // PS 側は距離減衰 + 拡散 + GGX の高光（Shader/Common/Lighting.hlsli）
    bool    m_SceneLightOn = false;
    DirectX::SimpleMath::Vector3 m_SceneLightPos = { 0.0f, 6.0f, 0.0f };   // 出生点（原点）の上
    float   m_SceneLightColor[3] = { 1.0f, 0.9f, 0.75f };
    float   m_SceneLightRadius = 20.0f;                  // ここで光が 0 になる
    float   m_SceneLightIntensity = 1.2f;                // 真下の地面が平行光 1.0 の頃と同じくらい
    bool    m_SceneLightGizmo = true;                    // 3D ギズモを出す
    bool    m_SceneLightMarker = true;                   // 電球と地面の照射範囲を線で出す

    // ---- 空と霧（線形 HDR。晴れた昼の野原）----
    SkyRenderer m_Sky;
    bool  m_SkyOn = true;
    float m_SkyZenith[3] = { 0.09f, 0.30f, 0.85f };    // 真上
    float m_SkyHorizon[3] = { 0.51f, 0.71f, 0.91f };   // 地平線（霧の色の既定）
    float m_SkyBelow[3] = { 0.26f, 0.34f, 0.42f };     // 地平線の下
    float m_SkyCurve = 0.5f;
    float m_SunGlow = 0.35f;
    float m_SunGlowPower = 24.0f;
    float m_SunDisk = 4.0f;
    bool  m_AlbedoSrgb = true;    // 模型の色貼図を sRGB として扱う（2026-09-28 から既定で入。切ると旧い白っぽい見た目）
    bool  m_FogOn = true;
    bool  m_FogUseHorizon = true;                      // 霧の色 = 空の地平線の色
    float m_FogColor[3] = { 0.51f, 0.71f, 0.91f };
    float m_FogStart = 40.0f;                          // m
    float m_FogEnd = 170.0f;
    float m_FogMax = 0.8f;
};
