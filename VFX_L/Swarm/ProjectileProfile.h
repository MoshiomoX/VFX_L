// ============================================================
// ProjectileProfile.h
// 投射物そのものの定義。投射物エディタ（ProjectileEditorScene）が
// 作って json に保存し、ゲーム本体は名前で引く。
//
//   ここにある物  … 弾が「何であるか」: 飛び方、命中で出す範囲、見た目（VFX）、
//                   威力・速さ・判定半径・寿命
//   アイテム（Items） … 弾を「どう撃つか」: 発射数・連発・間隔・マナ、UI 情報。
//                   profile 名でここを参照するだけ
//   機能ルーン        … 集約時に上の基礎値へ修飾を掛ける（BackpackAggregateSystem）
//
// 1 プロファイル = GPU の運動表（Swarm::Motion）の 1 行。
// 表の 0 番は常に組み込みの直進（json が 1 個も無くても弾は飛ぶ）。
//
// 三つの型：
//   Straight  : 直進。曲線を使わない
//   CurveOnce : 発射時に 1 回だけ捕捉して曲線で飛ぶ。
//               標的が死んだら、その時の向きのまま直進する
//   Track     : 標的が死んでも、自分に一番近い敵を探して追い続ける
//   Drop      : 撃った時の標的の位置へ空から落ちる（隕石）。c1 = (高さ m, 銃口側への水平距離 m, 未使用)
//
// 捕捉する相手は今のところ「プレイヤーに一番近い敵」固定（武器の自動照準と同じ）
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include "VFX_Editor/VFXId.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

struct ProjectileProfile
{
    // 曲線の左右をどう決めるか（制御点の横ずれ c.y の符号）
    enum class Mirror : int
    {
        Fixed = 0,       // いつも同じ側
        Alternate = 1,   // 1 発ごとに左右交互
        Random = 2,      // 毎回乱数
    };

    std::string       name = "NewProjectile";
    Swarm::MotionMode mode = Swarm::MotionMode::Straight;

    // 制御点（along, side, up）。side / up は射距離に対する割合。
    // Drop だけは c1 = (高さ m, 銃口側への水平距離 m, 未使用)、c2 は使わない
    DirectX::SimpleMath::Vector3 c1 = { 0.30f, 0.35f, 0.0f };
    DirectX::SimpleMath::Vector3 c2 = { 0.70f, 0.35f, 0.0f };
    Mirror mirror = Mirror::Alternate;

    float retargetRadius = 0.0f;   // Track の再捕捉半径。0 = 無制限

    // ---- 命中した場所に出す範囲（爆発など）----
    // AreaProfile の名前。空 = 出さない。
    // 位置を知っているのは GPU だけなので、見た目は粒子のみ（VFXDatabase に登録した VFX）
    std::string hitArea;
    bool hitAreaOnExpire = true;   // 寿命切れ・壁に当たった時も出す

    // ---- 弾の性能（基礎値。機能ルーンはこの上に掛かる）----
    float damage = 10.0f;
    float speed = 20.0f;
    float radius = 0.25f;     // 当たり判定
    float lifetime = 3.0f;

    // ---- 見た目 ----
    // VFX json のファイル名（Assets/Data/VFXData/ の中。VFXDatabase に登録済みの物だけ GPU で出る）
    std::string vfxFile = "Fireball.json";
    float visualSize = 0.9f;        // 判定とは独立（派手に見せても判定は安っぽくしない）
    float visualStretch = 0.0f;     // 進行方向への引き伸ばし（0 = 円形）
    // 撃った時の音（Assets/Data/Audio/Sounds.json の cue の名前。空 = 鳴らさない。2026-10-03）
    std::string castSound;

    Swarm::Motion ToMotion() const;
    VFXId ResolveVFX() const;       // vfxFile → VFXId（未登録なら None）

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);

    // GPU（SwarmBuildPath）と同じ式で 4 制御点を出す。エディタの曲線表示用。
    // sideSign は +1 / -1
    void BuildPreview(const DirectX::SimpleMath::Vector3& from,
        const DirectX::SimpleMath::Vector3& to, float sideSign,
        DirectX::SimpleMath::Vector3 out[4]) const;

    // BuildPreview の逆。世界座標の点から制御点（along, side, up）を求めて書き込む。
    // エディタで制御点を 3D で掴んで動かした時用。which は 1 か 2
    void SetControlFromWorld(int which,
        const DirectX::SimpleMath::Vector3& from,
        const DirectX::SimpleMath::Vector3& to, float sideSign,
        const DirectX::SimpleMath::Vector3& worldPos);
};

// ============================================================
// 全プロファイルの置き場。添字 = GPU の運動表の番号。
// 0 番は組み込みの "Straight"（保存も削除もできない）
// ============================================================
namespace ProjectileProfileDB
{
    inline constexpr const char* kDir = "Assets/Data/ProjectileData/";

    // 運動表の最後の 1 行はエディタの「乱数曲線の試射」用に空けておく。
    // プロファイルはここまでしか増やせない
    inline constexpr int kScratchRow = (int)Swarm::kMaxMotions - 1;

    // フォルダの json を全部読む（ファイル名順）。何度呼んでもよい
    void LoadAll();

    int  Count();
    ProjectileProfile& At(int index);

    // 名前 → 番号。無ければ 0（直進）。アイテムが撃つ時に呼ぶ
    int  IndexOf(const std::string& name);

    // 新規追加して番号を返す。表が一杯なら -1
    int  Add(const ProjectileProfile& p);

    // <name>.json へ保存。0 番は保存しない
    bool Save(int index);

    // GPU へ上げる表。SwarmSystem::SetMotions にそのまま渡す
    std::vector<Swarm::Motion> BuildMotions();

    // この 1 発を左右反転するか。Alternate / Random の状態はここが持つ
    bool NextMirror(int index);
}
