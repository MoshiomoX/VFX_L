// ============================================================
// SpellLabScene.h
// 魔法の実験場（F7、2026-10-06 ユーザー：「魔法を任意に組み合わせて試せる場面」）。
// 戦闘シーンそのもの（同じ地形生成・GPU 雑魚・武器・UI）を「実験場」の設定で立ち上げる：
//   平らな野原（山頂・洞窟・台地・木・岩なし、縁の碗だけ）、湧きなし、箱・Boss 門・磁石なし、
//   制限時間・エリート・天候の出来事なし、無敵・MP 無限・経験値 0、9x9 全部に枠。
//   画面に「Spell Lab」の窓が常に開いていて、全部の魔法から選んでマスに置ける（Debug/SpellLab）。
// 実装は CollisionTestScene の m_LabScene フラグ 1 つ（Init の分岐）。
// ============================================================
#pragma once
#include "Scene/CollisionTestScene.h"

class SpellLabScene final : public CollisionTestScene
{
public:
    SpellLabScene() { m_LabScene = true; }
};
