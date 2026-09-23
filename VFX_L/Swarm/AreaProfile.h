// ============================================================
// AreaProfile.h
// 範囲攻撃（爆発・法環）のデータ。投射物編集器の Area ページで作って
// json に保存し、ゲーム本体は名前で引く。ProjectileProfile と同じ作り。
//
// 単発と持続は底では同じ物（SwarmSystem の Area）：
//   OneShot : 出た瞬間に 1 回だけダメージ。duration は「見た目が出終わるまで残す秒数」
//   Lasting : duration の間、tickInterval ごとにダメージ
//
// 使われ方は二つ：
//   1) 範囲型の道具（AreaItemDef::profile）… 武器が標的の足元か玩家の位置に出す。
//      見た目は CPU 側で VFX を再生する（Mesh entry も使える）
//   2) 投射物プロファイルの hitArea … 弾が命中した場所に GPU が自分で出す。
//      位置を知っているのが GPU だけなので、見た目は粒子のみ（VFXDatabase に登録した VFX）
//
// 添字 = GPU の雛形表（Swarm::AreaDef）の番号。0 番は常に「無し」
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include "VFX_Editor/VFXId.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

class SwarmVFXTable;

struct AreaProfile
{
    enum class Kind : int
    {
        OneShot = 0,   // 爆発
        Lasting = 1,   // 法環・燃える地面
    };

    std::string name = "NewArea";
    Kind  kind = Kind::OneShot;

    float radius = 3.0f;
    float halfHeight = 1.5f;       // 中心からの上下の厚み
    float damage = 20.0f;          // 1 回（1 tick）あたり
    float duration = 0.35f;        // OneShot: 残す秒数 / Lasting: 持続秒数
    float tickInterval = 0.25f;    // Lasting のみ
    bool  followCaster = false;    // 玩家の位置に出した時、玩家に付いて動く
    bool  stun = true;             // ダメージで被弾硬直を入れる（法環で入れると敵が固まり続けるので注意）

    // 見た目。Assets/Data/VFXData/ の json のファイル名（拡張子込み）。空 = 無し
    std::string vfxFile;

    // ---- 編集器の試射用 ----
    bool  previewAtTarget = true;      // true = 一番近い標的の足元 / false = 銃口（= 玩家）の位置
    float previewInterval = 1.5f;

    // 実際の tick 間隔（OneShot は 2 回目が来ない値）
    float EffectiveTickInterval() const { return (kind == Kind::OneShot) ? 1.0e9f : tickInterval; }
    uint32_t Flags(bool atCaster) const;

    // CPU から出す時の形。vfxType は 0（見た目は CPU が再生する）
    Swarm::Area MakeArea(const DirectX::SimpleMath::Vector3& center, bool atCaster) const;

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);
};

namespace AreaProfileDB
{
    inline constexpr const char* kDir = "Assets/Data/AreaData/";
    inline constexpr const char* kVfxDir = "Assets/Data/VFXData/";

    void LoadAll();

    int  Count();
    AreaProfile& At(int index);

    // 名前 → 番号。無ければ 0（無し）
    int  IndexOf(const std::string& name);

    int  Add(const AreaProfile& p);
    bool Save(int index);

    // GPU の雛形表。弾の命中で出す時に引かれる。
    // vfxFile が VFXDatabase に登録済みなら、その粒子が GPU 側で出る（未登録なら見た目無し）
    std::vector<Swarm::AreaDef> BuildDefs(const SwarmVFXTable& vfxTable);

    // vfxFile → VFXId。VFXDatabase に無ければ None
    VFXId FindVFXId(const std::string& vfxFile);
}
