// ============================================================
// ShieldBubble.h
// シールドが残っている間、プレイヤーを包む六角の護罩（2026-10-07 ユーザー指定）。
//   ・見た目：fanghuzhao2.FBX（六角格子の半球）を上下 2 枚合わせた卵形。薄い青、ゆっくり回って少し息をする
//   ・明るさはシールドの量に関係なく一定（ユーザー指定）。シールドが 0 になったら消え、戻り始めたら膨らみながら出る
//   ・シールドで受けた瞬間に赤く光って青へ戻る（ユーザー指定）。割れた時は ShieldBreak.json の砕けた護罩に任せる
// 毎フレーム VFXMeshRenderer へ直接積む（エフェクトの json だと色を毎フレーム変えられないので）。
// 時間は gameplay の dt で進める（一時停止中は止まる）。積むのは描画の直前（止まっている間も見える）
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <memory>

class Model;
class VFXMeshRenderer;

class ShieldBubble
{
public:
    // shieldNow = 今のシールドの値、hit = このフレームにシールドで受けた、visible = 出してよいか（死んだら false）
    void Update(float dt, float shieldNow, bool hit, bool visible);
    // pos = プレイヤーの位置（カプセルの中心）
    void Submit(VFXMeshRenderer& renderer, const DirectX::SimpleMath::Vector3& pos);
    void DrawImGui();

    // ---- 見た目（ImGui で調整）----
    bool  enabled = true;
    DirectX::SimpleMath::Vector3 color = { 0.50f, 0.82f, 1.00f };   // 浅い水色（線形。ユーザー 10-07「青すぎる、浅い青に」）
    float alpha = 0.10f;
    float intensity = 1.0f;
    int   blend = 0;                 // 0 加算 / 1 半透明
    float spinDeg = 15.0f;           // 度/秒
    float breathe = 0.15f;           // 明るさの揺れ（割合）
    float breatheSpeed = 2.0f;       // rad/秒
    float appearTime = 0.3f;         // 0 から戻った時に膨らみながら出る秒
    // 受けた瞬間の赤（hitTime 秒で青へ戻る）
    DirectX::SimpleMath::Vector3 hitColor = { 1.00f, 0.12f, 0.08f };
    float hitTime = 0.35f;
    float hitAlpha = 0.50f;          // 受けた瞬間の不透明度（そこから alpha へ戻る）
    float hitScale = 0.08f;          // 受けた瞬間に少し膨らむ（割合）
    float hitFill = 0.35f;           // 受けた瞬間に重ねる半透明の赤の濃さ（加算の赤だけでは草の上で黄色く見える）
    // 形（模型は scale 1 で半径 30.4m・高さ 30m の半球。底が y = 0.3）
    float scaleXZ = 0.036f;          // 横の半径 ≒ 1.1m
    float scaleUp = 0.043f;          // 上の半球の高さ ≒ 1.3m
    float scaleDown = 0.025f;        // 下の半球の深さ ≒ 0.75m（足元まで）
    float offsetY = -0.2f;           // 継ぎ目の高さ（カプセルの中心から）

private:
    std::shared_ptr<Model> m_Model;
    bool  m_Loaded = false;
    float m_Time = 0.0f;
    float m_Hit = 0.0f;              // 1 → 0
    float m_Appear = 1.0f;           // 0 → 1
    bool  m_Shown = false;           // 今出しているか
};
