// ============================================================
// ClothChainComponent.h
// 骨の 1 本鎖を Verlet で揺らす（マント。2026-10-04）。
//   根の骨を anchorBone（上背）に固定し、残りのノードを重力 + 空気抵抗 + 風で動かす。
//   体の当たりはカプセル（骨 → 骨 + 半径）。鎖は布の中心線 1 本だけなので、
//   当たりは体の左右方向を無視して測る（脚が横にずれていても布の幅のどこかには当たる）。
//   ClothChainSystem が毎フレーム動かし、結果を SkinnedAnimComponent::boneOverrides に書く。
//   長さは m（世界）、時間は秒
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <SimpleMath.h>

struct ClothChainCollider
{
    std::string boneA, boneB;   // カプセルの両端（骨の原点）
    float radius = 0.1f;        // m
    int a = -1, b = -1;         // 解決した骨番号（ClothChainSystem）
};

struct ClothChainComponent
{
    // ---- 設定 ----
    bool enabled = true;
    std::string anchorBone;               // 根を固定する骨（上背）
    std::vector<std::string> chainBones;  // 根 → 先。最後は長さの終点だけ（なければ前の骨の向きで延ばす）
    std::vector<ClothChainCollider> colliders;

    float gravity = 9.8f;
    float drag = 1.2f;            // 空気抵抗（1/秒）。5 m/s で走ると約 30 度なびく
    float damping = 0.01f;        // 1 ステップ（1/120 秒）あたりの速度の減衰
    float stiffRoot = 0.12f;      // 元の形（上背に付いたままの形）へ戻す強さ。根 → 先で直線に減る
    float stiffTip = 0.0f;
    float bend = 0.25f;           // 1 つ飛ばしのノードの距離を保つ強さ（急な折れ曲がりを抑える）
    float windSpeed = 1.2f;       // m/s（風の基本の速さ）
    float windGust = 0.8f;        // 突風の振れ幅（m/s）
    DirectX::SimpleMath::Vector3 windDir = { 0.6f, 0.0f, 0.8f };
    int   iterations = 4;         // 拘束の反復
    float substep = 1.0f / 120.0f;
    float margin = 0.03f;         // 布の厚み（カプセル半径に足す）
    float groundOffset = 0.04f;   // 地面からの浮かせ

    // ---- 状態（ClothChainSystem）----
    bool ready = false;
    bool failed = false;
    int  anchor = -1;
    std::vector<int> bones;                              // chainBones の骨番号（終点が無ければ -1）
    std::vector<DirectX::SimpleMath::Matrix> bindG;      // スキニングした時の global（モデル空間）
    DirectX::SimpleMath::Matrix anchorBindInv;
    DirectX::SimpleMath::Vector3 rightModel;             // 体の左右（モデル空間）
    DirectX::SimpleMath::Vector3 backModel;              // 体の後ろ（モデル空間）
    std::vector<float> restLen, restLen2;                // 隣 / 1 つ飛ばしの距離（m）
    std::vector<DirectX::SimpleMath::Vector3> pos, prev; // ノード（世界）
    DirectX::SimpleMath::Vector3 lastRoot;
    float prevH = 1.0f / 120.0f;
    float time = 0.0f;
};
