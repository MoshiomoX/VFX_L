// ============================================================
// StageSelectScene.h
// ステージ選択（2026-10-21 ユーザー：「ステージ選択を Megabonk に完全に倣う」→ 独立したシーン）。
//   マップ（草原 / 砂漠 / 遺跡）× Tier（そこから何面続けて戦うか）を選んで出発する。
//   背景はタイトルと同じ作り = 戦闘シーンそのもの（選んだマップの地形・光・草、決まった seed）をカメラが回る。
//   3 つのマップの地形は入った時に全部建てて預かり、マップを切り替えたら出し入れするだけ（建て直さない）。敵・箱・門・HUD なし。
//   実装は CollisionTestScene の m_SelectScene フラグ（Init の分岐 + CollisionTestSceneSelect.cpp）、画面は UI/StageSelectUI。
//   進み具合（開いたマップ・クリアした Tier）は Core/GameProgress
// ============================================================
#pragma once
#include "Scene/CollisionTestScene.h"

class StageSelectScene final : public CollisionTestScene
{
public:
    StageSelectScene() { m_SelectScene = true; }
};
