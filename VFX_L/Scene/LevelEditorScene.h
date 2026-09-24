// ============================================================
// LevelEditorScene.h
// ステージ編集シーン（F6）。素材（モデル）を地面に並べて、json に保存する。
//
//   素材     … kPropFolders の .fbx を起動時に数えて一覧にする（読み込みは初めて使う時）
//   置く     … 素材一覧で選ぶ → マウスの下の地面に本物の姿で出る →
//               左クリックで置く（続けて置ける）。X / 同じ素材をもう一度押すと止める
//               （Esc はエンジン全体でアプリ終了なので使わない）
//   選ぶ     … 左クリックで物を選ぶ（光線 × 回転込みの包囲箱）。一覧からも選べる
//   動かす   … 移動ギズモ（格子吸着あり）。回転 / 拡縮は Inspector か R キー
//   保存     … Assets/Data/LevelData/<名前>.json（World/LevelData）
//
//   カメラは Unity と同じ（右ドラッグで見回し + WASD / QE、ホイール、F で寄る）。
//   地面は戦闘シーンと同じ 200m 四方（GridWorld 100x100 マス × 2m）
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/FlyCamera.h"
#include "World/LevelData.h"
#include <SimpleMath.h>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

class Model;

class LevelEditorScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Matrix = DirectX::SimpleMath::Matrix;

    // ---- 素材 ----
    struct PropAsset
    {
        std::string path;    // "Assets/Model/.../xxx.fbx"
        std::string name;    // 表示名（"_Color1" などを落とした物）
        std::string group;   // 名前の最初の "_" か "-" まで（Bush / Tree / wall / roof ...）
        std::string pack;    // 素材パック名（一覧の一番上の段）
        float defaultScale = 1.0f;   // 置く時の既定倍率（パック毎）
    };
    void ScanAssets();
    std::shared_ptr<Model> GetModel(const std::string& path);   // ResourceManager の cache

    // ---- 毎フレームの操作 ----
    void UpdateScreenSize();
    void UpdateEditing();             // ギズモ・クリック・ショートカット
    bool MouseOnGround(Vector3& hit); // マウスの光線と y = 0 の交点
    int  PickObject();                // マウスの下の物（無ければ -1）
    Vector3 Snap(const Vector3& p) const;
    float  UnitOf(const std::string& path);      // モデルのファイル単位 → m（読めなければ 1）
    Matrix WorldOf(const LevelObject& o);        // 単位・拡縮・回転・位置込みの世界行列

    // ---- 編集 ----
    void PlaceGhost();                // 置く素材を今の幽霊の位置へ
    void RerollGhost();               // 次に置く物の向き・大きさを振り直す
    void Select(int index);
    void DeleteSelected();
    void DuplicateSelected();
    void FocusSelected();
    void RotateStep(float sign);      // 選択中（無ければ幽霊）を m_RotateStep だけ回す
    void MarkDirty() { m_Dirty = true; }

    // ---- 保存 ----
    void NewLevel();
    void SaveLevel();
    void LoadLevel(const std::string& name);
    void RefreshLevelList();
    void SetStatus(const std::string& msg);

    // ---- UI / 表示 ----
    void DrawUI();
    void DrawAssetWindow();
    void DrawLevelWindow();
    void DrawDiscardPopup();
    void DrawBox(const Model& model, const Matrix& world, const DirectX::SimpleMath::Color& color);

private:
    FlyCamera m_Camera;
    float m_ScreenW = 0.0f;
    float m_ScreenH = 0.0f;

    // 地面（戦闘シーンと同じ広さ）
    std::shared_ptr<Model> m_Ground;
    static constexpr float kGroundHalf = 100.0f;

    // 素材
    std::vector<PropAsset> m_Assets;
    // 読んだモデル（パス → モデル。読めなかった物は nullptr で覚えて、毎フレーム読み直さない）
    std::unordered_map<std::string, std::shared_ptr<Model>> m_Models;
    char m_Filter[64] = {};
    int  m_PlaceAsset = -1;           // 置いている素材（-1 = 置かない = 選択モード）

    // 次に置く物（マウスの下に本物の姿で出す）
    bool    m_GhostValid = false;
    Vector3 m_GhostPos = { 0, 0, 0 };
    float   m_GhostYaw = 0.0f;
    float   m_GhostScale = 1.0f;

    // 置き方
    float m_Snap = 1.0f;              // 格子吸着（m）。0 = 無し
    float m_RotateStep = 45.0f;       // R キーで回す角度
    bool  m_RandomYaw = true;         // 置く度に向きを乱数（木や岩を自然に）
    bool  m_RandomScale = false;
    float m_ScaleMin = 0.85f;
    float m_ScaleMax = 1.15f;
    std::mt19937 m_Rng{ 12345u };

    // ステージ本体
    LevelData m_Level;
    int  m_Selected = -1;
    bool m_Dirty = false;             // 保存していない変更がある
    char m_NameBuf[64] = "untitled";
    std::vector<std::string> m_LevelFiles;

    // 未保存の変更を捨てる確認（New / Load の前）
    enum class Pending { None, New, Load };
    Pending     m_Pending = Pending::None;
    std::string m_PendingName;

    std::string m_Status;             // 一行の結果表示（保存しました 等）
    float       m_StatusTimer = 0.0f;

    // 照明（戦闘シーンと同じ Unity 風の太陽）
    float m_SunPitch = 50.0f;
    float m_SunYaw = 30.0f;
};
