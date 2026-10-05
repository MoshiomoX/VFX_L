// ============================================================
// MapEditMode.h
// ステージ編集シーン（F6）の「戦闘の地図」モード（2026-10-05、地図エディタの 2 歩目）。
//
// 戦闘の地図（World/MapData、Assets/Data/MapData/<名前>.vmap）を開いて、置物を編集する:
//   選ぶ   … 左クリック（光線 × 回転込みの包囲箱）
//   動かす … 移動ギズモ（Unity と同じ矢印）。水平に動かすと地面の高さに付いていく
//   回す / 拡縮 / 衝突の付け方 … 右の窓（置物 1 個の物だけ。R キーでも回せる）
//   足す   … 素材一覧（LevelEditorScene の Level Assets）で選んでクリック
//   消す   … Delete
//   機能付きの置き物（報酬の箱・Boss の門）… 窓のボタンで足す。戦闘シーンが中身ごと作る
// 動かす度にその物の衝突の箱と塞いだマス（雑魚の通行）を作り直す（World/MapEdit）。
// 選んだ物の衝突は緑の線、塞いだマスは赤い枠で見せる。
//
// 地形そのもの（床・台地・坂・外周）は見た目だけ建てて表示する（編集は 4 歩目）。
// LevelEditorScene が持ち、カメラ・素材一覧・置く物の向き / 大きさはシーンの物を借りる
// ============================================================
#pragma once
#include "ECS/Registry.h"
#include "World/GridWorld.h"
#include "World/MapData.h"
#include "World/MapEdit.h"
#include "World/MapTerrainEdit.h"
#include <SimpleMath.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Model;
class Renderer;
class FlyCamera;

class MapEditMode
{
public:
    using Vector3 = DirectX::SimpleMath::Vector3;
    using Matrix = DirectX::SimpleMath::Matrix;

    bool IsActive() const { return m_Active; }
    void SetActive(bool on) { m_Active = on; }
    void Shutdown();

    // 置こうとしている素材（シーンの素材一覧で選んだ物）。path が null なら選択モード
    struct PlaceRequest
    {
        const std::string* path = nullptr;
        float scale = 1.0f;     // ファイルの単位を m に直した後に掛ける倍率
        float yawDeg = 0.0f;
    };

    // 操作（ギズモ・クリック・ショートカット）。置いたフレームは true（シーンが次の向き・大きさを振り直す）
    bool Update(FlyCamera& camera, const PlaceRequest& place, float snap);
    void Render(Renderer& renderer);
    void DrawWindow(FlyCamera& camera);

    // 置くのを止める要求（X キー）。シーンが素材の選択を外す
    bool ConsumeStopPlacing() { const bool v = m_StopPlacing; m_StopPlacing = false; return v; }

    // TEMP-TEST: 自動テスト（VFXL_MAPEDIT_AUTOTEST）
    bool TestOpen(uint32_t seed, int biome);              // seed から作って開く
    MapData::Map& TestMap() { return m_Map; }
    void TestSelectGroup(uint32_t group) { m_Sel = { SelType::Group, group, -1 }; }
    void TestSelectPlacement(int i) { m_Sel = { SelType::Placement, 0, i }; }
    void TestFocus(FlyCamera& camera) { FocusSelection(camera); }
    bool TestApplyMove(const Vector3& delta) { return ApplyMove(delta); }
    void TestRebuildView() { m_ViewDirty = false; RebuildView(); }
    void TestFocusAt(FlyCamera& camera, const Vector3& pos, float radius);
    void TestFinishEdits() { if (m_ReseatDirty) { m_ReseatDirty = false; MapTerrainEdit::ReseatAll(m_Map); } m_ViewDirty = false; RebuildView(); }
    int  TestAddPlacement(MapData::PlaceType type, const Vector3& pos) { return AddPlacement(type, pos); }
    std::shared_ptr<Model> TestModel(const std::string& path) { return GetModel(path); }

private:
    static constexpr float kCrateSize = 0.9f;     // RewardCrateSystem::m_Size の既定
    static constexpr float kGateHeight = 5.6f;    // StageDirector::portalHeight の既定

    enum class SelType { None, Group, Placement };
    struct Selection { SelType type = SelType::None; uint32_t group = 0; int placement = -1; };

    // ---- 地図 ----
    bool Generate(uint32_t seed, int biome);
    bool Load(const std::string& name);
    bool Save();
    void RebuildView();                      // 地形の見た目（床・合成モデル）を建て直す
    void SetStatus(const std::string& msg);

    // ---- 選択・編集 ----
    bool RayGround(Vector3& hit) const;      // マウスの光線と地面（高さ場）
    Selection Pick();
    bool SelectionPos(Vector3& pos) const;
    bool ApplyMove(const Vector3& delta);    // 選択中を動かす（衝突・塞ぐマスも）
    void RotateSelection(float deg);
    void DeleteSelection();
    void DuplicateSelection();
    void FocusSelection(FlyCamera& camera);
    void RefreshCollision(uint32_t group, MapEdit::Collision mode);
    // 地形の部品（台地・高台。坂も同じ 1 個）の group か。山頂・洞窟の坂は含めない（区域の一部）
    bool IsPartGroup(uint32_t group) const;
    void DrawPartInspector();                // 地形の部品の中身（MapEditModeUI.cpp）
    void AddPart(FlyCamera& camera, bool terrace);
    void AddZoneRamp(FlyCamera& camera, bool summit);   // 山頂の長い坂 / 洞窟の下り坂を画面の真ん中へ
    void DrawZoneRampInspector();
    bool UpdateTools(bool mouseFree, bool placing);     // 区域の筆・起伏の筆（MapEditModeTools.cpp）
    void DrawToolsUI(FlyCamera& camera);
    void DrawHillMarks();                               // 丘の部品の輪
    void DrawHillInspector();                           // 選んだ丘の中身
    void ApplyReliefChange();                           // 起伏の設定・丘を変えた後（作り直し + 後でまとめる印）
    int  AddPlacement(MapData::PlaceType type, const Vector3& pos);
    Vector3 ScreenCenterGround(FlyCamera& camera) const;

    // ---- 表示 ----
    std::shared_ptr<Model> GetModel(const std::string& path);
    std::shared_ptr<Model> PlacementModel(const MapData::Placement& p, Matrix& outWorld);
    Matrix PropWorld(const MapData::Prop& p) const;
    void DrawSelectionMarks();
    void DrawInspector(FlyCamera& camera);

    bool m_Active = false;
    bool m_Loaded = false;
    bool m_Dirty = false;

    MapData::Map m_Map;
    Registry     m_Reg;                      // 地形の見た目の実体だけ
    GridWorld    m_Grid;
    std::vector<Entity> m_Terrain;

    Selection m_Sel;
    bool m_GroundFollow = true;              // 水平に動かした時、地面の高さに付いていく
    bool m_ShowCollision = true;
    bool m_ShowVolumes = true;               // 手で置いた見えない体積を線で見せる（見せている時だけ選べる）
    bool m_StopPlacing = false;
    bool m_ViewDirty = false;                // 地形の部品を変えた：見た目（床・合成モデル）を建て直す
    int  m_PaintZone = -1;                   // 区域の筆：-1 = 切、0 平原 / 1 山頂 / 2 洞窟
    int  m_PaintRadius = 2;                  // 筆の半径（マス）
    bool m_ZoneDirty = false;                // 塗っている最中（離した時に作り直す）
    int   m_BrushMode = -1;                  // 起伏の筆：-1 = 切、0 上げる / 1 下げる / 2 均す / 3 平らにする
    float m_BrushRadius = 8.0f;              // m
    float m_BrushStrength = 3.0f;            // 上げ下げは m/秒、均し・平らは寄せる速さ
    float m_BrushFlattenY = 0.0f;            // 平らにする高さ（押し始めの所の地面）
    bool  m_BrushStroke = false;             // 押している最中
    bool  m_BrushDirty = false;              // 起伏を書き換えた（離した時に作り直す）
    bool  m_ShowHills = true;                // 丘の部品を輪で見せる（見せている時だけ選べる）
    bool  m_ReseatDirty = false;             // 起伏を変えた：離した時に、台地などの足元を今の地面へ合わせ直す

    // 置く物（マウスの下）
    bool    m_GhostValid = false;
    Vector3 m_GhostPos = { 0, 0, 0 };
    PlaceRequest m_Ghost;
    std::string  m_GhostPath;

    std::unordered_map<std::string, std::shared_ptr<Model>> m_Models;

    char m_NameBuf[64] = "map01";
    int  m_GenSeed = 12345;
    int  m_GenBiome = 0;
    std::vector<std::string> m_Files;
    std::string m_Status;
};
