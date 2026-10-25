// ============================================================
// GrassShreds.h
// 風源（竜巻・渦・砂旋・突風）が草地から引きちぎって巻き上げる草屑（2026-10-23、ユーザー：「草地をもう少し細かく、
// 風に巻き上げられるとか」の 3 つ目）。
//   風源の表の CPU の写し（SwarmSystem::GetLocalWind、2〜3 フレーム遅れ）を毎フレーム見て、風源 1 つに
//   VFXData/GrassShred.json（細い草の切れ端 + 葉の欠片。風に 100% 乗る）を 1 つ付けて動かす。
//   表の並びは毎フレーム変わり得る（GPU が InterlockedAdd で詰める）ので、前のフレームの発生源とは位置で対応を取る。
//   出す量は風の強さ（渦の速さか押す速さ × fade）で決め、AreaVFXPlayer::SetPresence に渡す。
//   粒子が渦に巻かれて回るのは、粒子の更新 CS が風源の表を読むから（ここは出して動かすだけ）。
//   草が無い面（草を切った面）では出さない
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include <cstdint>

class AreaVFXPlayer;
class GridWorld;
struct LocalWind;
struct VFXContext;

class GrassShreds
{
public:
    struct Settings
    {
        bool  enabled = true;
        float minWind = 3.0f;        // この m/s（渦の速さか押す速さ）から出始める
        float fullWind = 10.0f;      // この m/s で一番多く
        float refRadius = 5.0f;      // json を作った時の風源の半径 m（これとの比で輪の大きさを合わせる）
        float matchDistance = 2.5f;  // 前のフレームの発生源と同じ物と見なす距離 m
        float lingerTime = 0.3f;     // 表から消えてからこの秒数で止める（リードバックの抜けで点滅しない）
    };

    // 毎フレーム（gameplay とタイトルの景色の更新から）。grassOn = この面に草があるか、tint = 粒子の色に掛ける（面の草の色）
    void Update(float dt, const LocalWind& wind, const GridWorld& grid, bool grassOn,
        const DirectX::SimpleMath::Vector3& tint, AreaVFXPlayer& vfx, const VFXContext& ctx);
    // 地形を作り直した時・シーンの終わり：出ている物を全部止める
    void Clear(AreaVFXPlayer& vfx);

    Settings& GetSettings() { return m_Settings; }
    int ActiveCount() const { return (int)m_Emitters.size(); }
    void DrawImGui();

private:
    struct Emitter
    {
        uint32_t handle = 0;
        DirectX::SimpleMath::Vector3 pos;
        float unseen = 0.0f;   // 表に見当たらない秒数
        bool matched = false;  // このフレームに風源と対応が付いたか
    };
    Settings m_Settings;
    std::vector<Emitter> m_Emitters;
};
