// ============================================================
// EliteLooks.h
// 面の固定エリートの見た目（2026-10-25）。Boss・遺跡の敵と同じく方块人の改造：
// 皮（Tools/BuildBossSkin.ps1 の eaxe / earcher / erider / eflame / eshade / elibrarian）+ 部品に付く飾り。
//   草原 A 大斧の戦鬼：角の鉄兜（赤く光る目）・毛皮の肩掛け・革の締め紐・両刃の大斧・棘の籠手
//   草原 B 森の弓手  ：とんがり頭巾（緑に光る目）・口元の布・背中の矢筒・左手の弓・右手の革の籠手
//   砂漠 A 砂塵の騎兵：砂色の頭布と覆面（琥珀の目・額の宝石）・肩の布・青銅の胸当て・槍と旗・丸い盾
//   砂漠 B 炎砂の術師：段の付いたとんがり帽子（橙に光る目）・金の飾り帯・胸の熾火の宝石・炎の杖
//   遺跡 A 影の剣士  ：黒い覆面（紫に光る一筋の目）・後ろへなびく襟巻き・肩当て・紫に光る刃の刀
//   遺跡 B 呪いの司書：深い頭巾と光る眼鏡・金の縁の襟・鎖の本・左手の開いた本・体の周りを回る 3 冊の本
// 飾りは SwarmSystem::SetListLook で描画リスト kDrawListEliteA / EliteB に付ける（面が変わる度に入れ替える）
// ============================================================
#pragma once

class SwarmSystem;
struct ID3D11Device;

namespace EliteLooks
{
    // stage = 1..3（その面の A / B）。雑魚の網がまだ無ければ何もしない
    void Apply(int stage, ID3D11Device* device, SwarmSystem& swarm);
}
