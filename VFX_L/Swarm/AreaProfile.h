// ============================================================
// AreaProfile.h
// 範囲攻撃（爆発・法環）のデータ。投射物エディタの Area ページで作って
// json に保存し、ゲーム本体は名前で引く。ProjectileProfile と同じ作り。
//
// 単発と持続は底では同じ物（SwarmSystem の Area）：
//   OneShot : 出た瞬間に 1 回だけダメージ。duration は「見た目が出終わるまで残す秒数」
//   Lasting : duration の間、tickInterval ごとにダメージ
//
// 使われ方は二つ：
//   1) 範囲型のアイテム（AreaItemDef::profile）… 武器が標的の足元かプレイヤーの位置に出す。
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
        Beam = 2,      // 光線（2026-09-30）：プレイヤーの手から length m のカプセル。duration の間 tickInterval ごとにダメージ。
                       // 判定は GPU のカプセル型範囲（Swarm::kAreaCapsule）、起点は毎フレームプレイヤーに付く。見た目は VFX の Beam entry
    };

    std::string name = "NewArea";
    Kind  kind = Kind::OneShot;

    float radius = 3.0f;
    float halfHeight = 1.5f;       // 中心からの上下の厚み
    float damage = 20.0f;          // 1 回（1 tick）あたり
    float duration = 0.35f;        // OneShot: 残す秒数 / Lasting: 持続秒数
    float tickInterval = 0.25f;    // Lasting のみ
    bool  followCaster = false;    // プレイヤーの位置に出した時、プレイヤーに付いて動く
    bool  stun = true;             // ダメージで被弾硬直を入れる（法環で入れると敵が固まり続けるので注意）
    float slow = 0.0f;             // tick の度に中の敵の移動速度をこれだけ落とす（0〜1、15 段階に丸める。エリート・Boss は半分。2026-10-01 毒の池）
    bool  cameraShake = false;     // 出た時にカメラを揺らす（爆発・光線。2026-10-03：以前は命中の火花・死んだ時の土煙も含め全部揺らしていた）

    // ---- Beam のみ ----
    float length = 18.0f;          // 射程（m）。地形に当たればそこまで
    float chargeTime = 0.5f;       // 撃つ前の溜め（秒）。この間は判定が無く、VFX だけ出ている

    // 見た目。Assets/Data/VFXData/ の json のファイル名（拡張子込み）。空 = 無し
    std::string vfxFile;
    // 出た時の音（Assets/Data/Audio/Sounds.json の cue の名前。空 = 鳴らさない。2026-10-03）。
    // GPU で生まれた範囲は vfxFile のレシピ毎に数えて鳴らす（同じ vfxFile の範囲は同じ音になる）
    std::string sound;

    // ---- エディタの試射用 ----
    bool  previewAtTarget = true;      // true = 一番近い標的の足元 / false = 銃口（= プレイヤー）の位置
    float previewInterval = 1.5f;

    // 実際の tick 間隔（OneShot は 2 回目が来ない値）
    float EffectiveTickInterval() const { return (kind == Kind::OneShot) ? 1.0e9f : tickInterval; }
    bool  IsBeam() const { return kind == Kind::Beam; }
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
