// ============================================================
// BossLooks.h
// 面の Boss の見た目（2026-10-24、ユーザー「Boss を区別する」→ 見た目は方块人の改造（ダウンロードしない）。
// 砂漠は推奨した「砂岩の巨像」：砂岩の皮 + 金・瑠璃の縞のファラオの頭巾・付け髭・光る目 + 胸の襟飾りと宝石・金の帯 +
// 石の肩当て + 体の周りを回る浮き岩。
// 草原（2026-10-25）は「苔むした岩の巨人」：灰色の岩と苔の皮 + 苔の帽子・草と花・石の角・緑に光る目 + 光る紋の胸板 +
// 苔の載った肩の大岩・岩の拳。
// 皮は Tools/BuildBossSkin.ps1 が Kenney Blocky H / O から塗り直した物。飾りは焼いた雑魚の網の部品の寸法
// （Model::SubMesh の boundsMin / Max）に合わせて HexahedronBatch で積み、SwarmSystem::SetBossLook に渡す
// ============================================================
#pragma once
#include "Enemy/BossBrain.h"

class SwarmSystem;
struct ID3D11Device;

namespace BossLooks
{
    // 面の Boss の皮と飾りを入れる。雑魚の網がまだ無ければ何もしない（雑魚の皮のまま・飾り無し）
    void Apply(BossType type, ID3D11Device* device, SwarmSystem& swarm);
}
