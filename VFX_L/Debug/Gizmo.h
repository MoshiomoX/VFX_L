// ============================================================
// Gizmo.h
// 3D 空間の点をマウスで掴んで動かす移動ギズモ。
//
// 依存は ImGui とカメラの行列だけ。シーンの種類を問わず使える
// （投射物エディタの制御点、将来のシーン構築の配置物 など）。
//
// 使い方（毎フレーム、ImGui のフレーム内 = シーンの Update で）：
//     Gizmo::BeginFrame(camera->GetViewMatrix(), camera->GetProjectionMatrix());
//     if (Gizmo::Translate("muzzle", m_Muzzle)) { ...動かされた... }
//     if (Gizmo::Translate("p1", p1, opt))      { ... }
//
// 取っ手：
//     3 本の軸（X 赤 / Y 緑 / Z 青）… その軸に沿って動かす
//     3 枚の面（軸の間の小さい四角）… その平面内で動かす。XZ は地面に物を置く時用
//     中央の丸                       … 画面と平行な面で自由に動かす
//
// 描画は ImGui の背景 draw list（3D の上、ImGui の窓の下）。
// 大きさは画面上で一定（遠くの点でも掴める）。
//
// 掴まない条件：マウスが ImGui の窓の上 / Alt 押下中（デバッグカメラの操作と衝突させない）
// ============================================================
#pragma once
#include <SimpleMath.h>

namespace Gizmo
{
    struct Options
    {
        float sizePixels = 90.0f;      // 軸の長さ（画面上の px）
        bool  showPlanes = true;       // 面の取っ手を出す
        bool  showCenter = true;       // 中央の自由移動を出す
        float snap = 0.0f;             // > 0 なら、この刻みへ丸める（世界単位）。Ctrl で一時的に無効
        const char* label = nullptr;   // 点の横に出す文字
    };

    // フレームの頭で 1 回。カメラの行列を渡す（転置しない、SimpleMath のまま）
    void BeginFrame(const DirectX::SimpleMath::Matrix& view,
        const DirectX::SimpleMath::Matrix& proj);

    // pos を動かしたフレームは true。id は同じフレーム内で一意な文字列
    bool Translate(const char* id, DirectX::SimpleMath::Vector3& pos, const Options& opt = Options());

    // どれかのギズモを掴んでいる最中（カメラ操作や他のクリック処理を止める判断に使う）
    bool IsUsing();
    // どれかの取っ手の上にマウスがある
    bool IsHovering();

    // 今のマウス位置から世界へ伸びる光線（BeginFrame の行列で計算。dir は正規化済み）。
    // 物体のクリック選択などをギズモと同じ座標換算でやるため
    void GetMouseRay(DirectX::SimpleMath::Vector3& origin, DirectX::SimpleMath::Vector3& dir);
}
