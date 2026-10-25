// ============================================================
// EnemyLooks.h
// 遺跡の敵の見た目（2026-10-25、ユーザー「遺跡の雑魚を作って」→ 呪いの鎧・石像ガーゴイル・死霊術師 + 骸骨・鎖の番人）。
// Boss と同じく方块人の改造：皮（Tools/BuildBossSkin.ps1 の armor / gargoyle / necro / skeleton / warden）+ 部品に付く飾り。
//   呪いの鎧   : 鋼の皮 + 覗き穴の兜（紫に光る目・紫の房）・胸当て（紫の紋）・腰の板・肩当て・籠手
//   ガーゴイル : 灰色の石の皮 + 角・耳・鼻面と牙・赤く光る目・背中の翼（羽ばたく、VS の g_AnimMode 2）・尾・爪
//   死霊術師   : 黒紫の衣 + 頭巾（顔は闇、緑に光る目）・肩掛け・骨の首飾り・長い衣の裾・緑の宝石の杖
//   骸骨       : 骨の皮 + 頭蓋（黒い眼窩・赤い点・歯）・肋骨・錆びた剣・木の小盾
//   鎖の番人   : 錆びた鉄と革の皮 + 覗き穴の鉄兜（橙に光る）・棘の首輪・胸に掛けた鎖・前掛け・手の鉤と鎖・左手の枷
// 飾りは SwarmSystem::SetListLook でそのリストに付ける（砕け散りにも付いて行く）
// ============================================================
#pragma once
#include <memory>

class Model;
class SwarmSystem;
struct ID3D11Device;

namespace EnemyLooks
{
    // 遺跡の敵の皮と飾りを全部入れる（雑魚の網がまだ無ければ何もしない）。面に関係なく一度でよい
    void ApplyRuins(ID3D11Device* device, SwarmSystem& swarm);

    // 鎖の番人が投げる鎖（シーンが ECS のモデルで並べる。2026-10-25）：
    //   segment = +Z へ長さ kChainSegLen の鎖の輪の並び（原点が根元）、hook = 鉤（原点が鎖の付け根、+Z が先）
    constexpr float kChainSegLen = 0.5f;
    std::shared_ptr<Model> BuildChainSegment(ID3D11Device* device);
    std::shared_ptr<Model> BuildChainHook(ID3D11Device* device);
}
