// ============================================================
// CloudShadows.h
// 雲の影：地面に流れる太陽の陰り。形は Lighting.hlsli の CloudSun（値ノイズ 2 段）、
// ここは風で流れた距離を足し、面ごとの強さを持ち、毎フレーム LightBuffer へ書く
// ============================================================
#pragma once
#include <SimpleMath.h>

class Renderer;
struct WindState;

class CloudShadows
{
public:
    struct Settings
    {
        bool  enabled = true;
        float strength = 0.55f;   // 雲の下で太陽を弱める割合
        float size = 30.0f;       // 雲 1 つのおおよその大きさ（m）
        float coverage = 0.5f;    // 空を覆う割合
        float softness = 0.035f;  // 縁のぼかし（小さいほどくっきり）
        float speedMul = 3.5f;    // 地上の風に対する雲の速さの倍率（上空の風は速い）
    };

    // 面の種類（TerrainGenerator::Biome の番号：0 草原 / 1 砂漠 / 2 遺跡）で既定値を選ぶ。毎フレーム呼んでよい（変わった時だけ選び直す）
    void SetBiome(int biome);
    // 風で流す。time = 草の風の時計（一時停止中は止まる）
    void Update(const WindState& wind, float time);
    void Apply(Renderer& renderer) const;
    static void Clear(Renderer& renderer);
    void DrawImGui();

    Settings& Get() { return m_S; }

private:
    Settings m_S;
    DirectX::SimpleMath::Vector2 m_Offset = { 0.0f, 0.0f };
    float m_LastTime = -1.0f;
    int   m_Biome = -1;
};
