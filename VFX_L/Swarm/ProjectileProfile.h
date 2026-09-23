// ============================================================
// ProjectileProfile.h
// 投射物の「飛び方」のデータ。投射物編集器（ProjectileEditorScene）が
// 作って json に保存し、ゲーム本体は名前で引く。
//
// 1 プロファイル = GPU の運動表（Swarm::Motion）の 1 行。
// 表の 0 番は常に組み込みの直進（json が 1 個も無くても弾は飛ぶ）。
//
// 三つの型：
//   Straight  : 直進。曲線を使わない
//   CurveOnce : 発射時に 1 回だけ捕捉して曲線で飛ぶ。
//               標的が死んだら、その時の向きのまま直進する
//   Track     : 標的が死んでも、自分に一番近い敵を探して追い続ける
//
// 捕捉する相手は今のところ「玩家に一番近い敵」固定（武器の自動照準と同じ）
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
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

    // 制御点（along, side, up）。side / up は射距離に対する割合
    DirectX::SimpleMath::Vector3 c1 = { 0.30f, 0.35f, 0.0f };
    DirectX::SimpleMath::Vector3 c2 = { 0.70f, 0.35f, 0.0f };
    Mirror mirror = Mirror::Alternate;

    float retargetRadius = 0.0f;   // Track の再捕捉半径。0 = 無制限

    // ---- 命中した場所に出す範囲（爆発など）----
    // AreaProfile の名前。空 = 出さない。
    // 位置を知っているのは GPU だけなので、見た目は粒子のみ（VFXDatabase に登録した VFX）
    std::string hitArea;
    bool hitAreaOnExpire = true;   // 寿命切れ・壁に当たった時も出す

    // ---- 編集器の試射用 ----
    // ゲーム本体では速さ・寿命・威力は道具（SpellStats）が決めるので使わない
    float previewSpeed = 14.0f;
    float previewLifetime = 4.0f;
    float previewRadius = 0.25f;
    float previewDamage = 10.0f;

    Swarm::Motion ToMotion() const;

    nlohmann::json ToJson() const;
    void FromJson(const nlohmann::json& j);

    // GPU（SwarmBuildPath）と同じ式で 4 制御点を出す。編集器の曲線表示用。
    // sideSign は +1 / -1
    void BuildPreview(const DirectX::SimpleMath::Vector3& from,
        const DirectX::SimpleMath::Vector3& to, float sideSign,
        DirectX::SimpleMath::Vector3 out[4]) const;

    // BuildPreview の逆。世界座標の点から制御点（along, side, up）を求めて書き込む。
    // 編集器で制御点を 3D で掴んで動かした時用。which は 1 か 2
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

    // 運動表の最後の 1 行は編集器の「乱数曲線の試射」用に空けておく。
    // プロファイルはここまでしか増やせない
    inline constexpr int kScratchRow = (int)Swarm::kMaxMotions - 1;

    // フォルダの json を全部読む（ファイル名順）。何度呼んでもよい
    void LoadAll();

    int  Count();
    ProjectileProfile& At(int index);

    // 名前 → 番号。無ければ 0（直進）。道具が撃つ時に呼ぶ
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
