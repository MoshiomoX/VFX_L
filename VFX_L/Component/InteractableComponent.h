// ============================================================
// InteractableComponent.h
// 近づいて F（パッド B）で使う物（純データ）。報酬の箱など。
//
// 使われた時に何が起きるかはシーンが決める（InteractionSystem は
// 「どれが押されたか」を返すだけ）。kind はその振り分け用。
// 浮遊・回転・光の基準値もここに持つ（見た目は TransformComponent を書き換えて出す）
// ============================================================
#pragma once
#include <SimpleMath.h>

enum class InteractKind
{
    RewardChoice,   // 升級と同じ三択を出す（レベルは上がらない）
};

struct InteractableComponent
{
    InteractKind kind = InteractKind::RewardChoice;
    float radius = 2.2f;                    // 玩家の中心（足元）からこの水平距離で使える
    const wchar_t* prompt = L"[F] 開ける";   // 画面下に出す案内

    // ---- 見た目の動き ----
    DirectX::SimpleMath::Vector3 basePos;   // 置いた位置（地面）。浮遊はこの上
    float phase = 0.0f;                     // 浮遊の位相（箱ごとにずらす）

    // ---- 目印の光（PointLightManager へ毎フレーム積む）----
    DirectX::SimpleMath::Vector3 lightColor = { 1.0f, 0.75f, 0.35f };   // 暖かい黄
    float lightRadius = 3.5f;
    float lightIntensity = 1.0f;            // 数個が近くに固まっても白飛びしない程度
    float lightHeight = 0.9f;               // basePos からの高さ
};
