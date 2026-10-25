// ============================================================
// TitleScene.h
// タイトル画面（2026-10-09 作り直し。ユーザー：「タイトルとリザルトが不格好」）。
// 戦闘シーンそのもの（同じ地形生成・草・空・トゥーン・プレイヤーのモデル）を「タイトル」の設定で立ち上げる：
//   決まった seed の草原、箱・門・HUD なし。周りが開けた平らな所に立つプレイヤーが、四方から寄る雑魚を
//   メテオや魔導光線で吹き飛ばし続ける戦いをスローモーションで見せ（無敵・MP 無限）、午後遅くの光の中を
//   カメラが実時間でゆっくり回る。左に名前と文字だけのメニュー（UI/TitleScreenUI）。
// 実装は CollisionTestScene の m_TitleScene フラグ 1 つ（Init の分岐 + CollisionTestSceneTitle.cpp）。
// 以前の TitleScene（参照格子の上に色の四角が漂う画面）は git の履歴にある
// ============================================================
#pragma once
#include "Scene/CollisionTestScene.h"

class TitleScene final : public CollisionTestScene
{
public:
    TitleScene() { m_TitleScene = true; }
};
