// ============================================================
// VFXLiquidDef.h
// Liquid entry の見た目（2026-10-02）。CPU の VFXLiquidRenderer と GPU の範囲
// （SwarmVFXTable → SwarmLiquidVS）が同じ物を StructuredBuffer で読み、
// 同じ VFXLiquidPS で描く。HLSL 側は Shader/Common/LiquidCommon.hlsli の LiquidDef（160 bytes）。
//
// 形は SDF（符号付き距離、m、内側が負）。液滴 1 つ = 楕円の SDF、それを多項式の
// smooth union で繋ぐ。距離なので縁・深さ・外側の光の幅を m で指定できる。
// 液滴の動き（飛び散る → 寄り集まる → ゆらぐ → 乾く）は (seed, 年齢, 残り時間) の式で、
// 状態は持たない
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <nlohmann/json.hpp>
#include <cstdint>

struct VFXLiquidDef
{
    DirectX::SimpleMath::Vector4 deepColor = { 0.010f, 0.060f, 0.008f, 0.92f };  // 深い所の色（linear）、a = 不透明度
    DirectX::SimpleMath::Vector4 edgeColor = { 0.06f, 0.28f, 0.025f, 0.80f };    // 縁の色、a = 不透明度
    DirectX::SimpleMath::Vector4 glowColor = { 0.010f, 0.050f, 0.005f, 2.0f };   // 自発光、a = 縁が何倍余計に光るか
    DirectX::SimpleMath::Vector4 skyColor = { 0.35f, 0.45f, 0.40f, 0.6f };       // 斜めから見た時に映る色、a = fresnel の強さ

    float radius = 0.0f;       // m。0 = 範囲の半径を使う（GPU）。CPU では 0 なら 2m
    float fill = 0.92f;        // 半径のうち液が届く割合（判定の円の内側に収める）
    float spread = 0.6f;       // 前（投げた向き）へどれだけ余計に飛ぶか（0 = 丸い）
    float wobble = 0.03f;      // 液滴のゆらぎ（届く半径に対する割合）

    float blend = 0.18f;       // smooth union の繋ぎの幅（届く半径に対する割合）
    float rimWidth = 0.12f;    // m：縁の内側の明るい帯
    float depthWidth = 0.35f;  // m：縁から深さが最大になるまで（表面はこの幅で丸く下がる）
    float bump = 0.06f;        // m：その丸みの高さ（縁の法線の傾き）

    float splashTime = 0.22f;  // 秒：液滴が飛び散る時間
    float dryTime = 0.7f;      // 秒：終わりのこれだけで縮んで消える
    float specGain = 1.0f;     // 太陽の照り返し
    float roughness = 0.35f;   // GGX の粗さ（0.18 だと照り返しの峰が HDR 7〜8 になり bloom で白く飛んだ。10-02）

    float bubbles = 0.45f;     // 泡のあるマスの割合（深い所ほど多い）
    float bubbleGain = 0.35f;  // 泡の明るさ
    float swirl = 0.25f;       // ゆっくり動くノイズの明暗・さざ波
    float swirlScale = 1.6f;   // そのノイズの細かさ 1/m

    float lift = 0.035f;       // m：地面から浮かせる（Z ファイト避け）
    float cliff = 0.6f;        // m：中心の地面からこれ以上高い / 低い所は切る（崖に垂れない）
    float haloWidth = 0.35f;   // m：縁の外の地面にぼんやり光る幅
    float haloGain = 0.6f;

    uint32_t lobes = 7;        // 本体の周りの液滴（0〜7）
    uint32_t spatter = 4;      // 前へ飛ぶ小さな飛沫（0〜8）
    uint32_t flags = 0;        // 予約
    // 点光源の効き（拡散 + 照り返し）。毒の池は自分の緑の点光源を 0.5m 上に持つので、
    // そのままだと液面に強い照り返しが出る（10-02 用户「太亮了」）
    float    pointGain = 0.3f;

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);
    // ImGui で全部の値をいじる（VFX 編集器の Inspector）
    void DrawImGui();
};
static_assert(sizeof(VFXLiquidDef) == 160, "LiquidDef layout mismatch");
