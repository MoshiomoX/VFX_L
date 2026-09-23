// ============================================================
// ProjectileEditorScene.h
// 投射物の「飛び方」専用の編集シーン。
//
// 戦闘は無い。銃口（= GPU 側の「玩家」位置）から標的の雑魚へ試射を繰り返し、
// 曲線の制御点・型・左右の振り方を調整して json に保存する。
// 弾は本番と同じ SwarmSystem（GPU）で飛ぶので、ここで見た軌道がそのままゲームに出る。
//
// 画面の線：
//   明るい線 = 今の設定で次に飛ぶ曲線（CPU で GPU と同じ式を引いている）
//   暗い線   = 左右反転した側（Mirror が Fixed 以外の時）
//   細い線   = 制御点の取っ手（p0-p1 / p3-p2）
// ============================================================
#pragma once
#include "Scene/SceneBase.h"
#include "Camera/CameraBase.h"
#include "Particle/GPUParticleSystem.h"
#include "Swarm/SwarmSystem.h"
#include "Swarm/ProjectileProfile.h"
#include "Swarm/AreaProfile.h"
#include "Swarm/AreaVFXPlayer.h"
#include "VFX_Editor/EntryType.h"
#include "VFX_Editor/VFXMeshRenderer.h"
#include <string>
#include <vector>
#include "World/GridWorld.h"
#include "VFX_Editor/VFXId.h"
#include <memory>

class ProjectileEditorScene : public SceneBase
{
public:
    void Init()     override;
    void Shutdown() override;
    void Update(float dt) override;
    void Render(Renderer& renderer) override;

private:
    void DrawUI();
    void DrawProjectileTab();     // 投射物の飛び方
    void DrawAreaTab();           // 範囲攻撃（爆発・法環）
    void DrawCommonUI();          // 標的・表示・状態（両方の頁で共通）
    void CastArea();              // 範囲を 1 回出す
    void SelectArea(int index);
    void RefreshVfxFiles();       // Assets/Data/VFXData の json を数え直す
    void DrawGuides();            // 曲線・制御点・銃口・地面の格子
    void Fire(bool randomCurve);  // 1 発。randomCurve = 制御点を乱数で振った曲線で撃つ
    void DrawGizmos();            // 銃口と制御点を 3D で掴んで動かす
    void RespawnTargets();        // 標的を並べ直す（弾も消える）
    void PushMotions();           // 編集中の値を GPU の運動表へ
    void SelectProfile(int index);

private:
    CameraBase m_Camera;

    GPUParticleSystem        m_ParticleSystem;
    std::shared_ptr<Texture> m_ParticleTexture;
    SwarmSystem              m_Swarm;
    GridWorld                m_Grid;          // 全面歩ける平地（壁で弾が消えないように）

    float m_TotalTime = 0.0f;

    // ---- 編集対象 ----
    int  m_Selected = 0;           // ProjectileProfileDB の番号（0 = 組み込みの直進）
    char m_NameBuf[64] = {};
    bool m_Dirty = false;          // 保存していない変更がある

    // ---- 試射 ----
    DirectX::SimpleMath::Vector3 m_Muzzle = { 0.0f, 0.9f, -8.0f };
    bool  m_AutoFire = true;
    float m_FireInterval = 0.6f;
    float m_FireTimer = 0.0f;
    int   m_Volley = 1;            // 1 回の発射で撃つ数（左右交互の見え方の確認用）
    float m_VolleyDelay = 0.08f;
    int   m_VolleyLeft = 0;
    float m_VolleyTimer = 0.0f;
    int   m_VfxIndex = 0;          // VFXDatabase::At の番号

    // ---- 乱数曲線の試射 ----
    // プロファイルは書き換えない。運動表の予備行（kScratchRow）に毎回違う制御点を入れて撃つ。
    // 曲線は生成時に GPU が組むので、次の弾で行を上書きしても飛行中の弾の軌道は変わらない
    bool  m_RandomEachShot = false;    // 連射も全部乱数曲線にする
    bool  m_NextVolleyRandom = false;  // 今の volley が乱数曲線か
    float m_RandSide = 0.6f;           // 横ずれの範囲（± 射距離比）
    float m_RandUp = 0.3f;             // 上ずれの範囲（0 〜 射距離比）
    Swarm::Motion m_RandomMotion;      // 予備行の中身
    DirectX::SimpleMath::Vector3 m_LastRandomCurve[4];   // 直前の乱数曲線（表示用）
    float m_LastRandomTimer = 0.0f;    // > 0 の間だけ線を出す

    // ---- 3D ギズモ ----
    bool  m_ShowGizmos = true;
    float m_GizmoSnap = 0.0f;          // 0 = 無し
    bool  m_ShowDebugSpheres = true;   // 粒子が無い VFX でも弾の位置が見えるように

    // ---- 標的 ----
    int   m_TargetCount = 3;
    float m_TargetDistance = 14.0f;    // 銃口からの距離
    float m_TargetSpread = 8.0f;       // 横に並べる幅
    float m_TargetHp = 30.0f;
    float m_TargetSpeed = 0.0f;        // 0 = 動かない。>0 で銃口へ寄ってくる
    bool  m_AutoRespawn = true;
    float m_RespawnTimer = 0.0f;

    // ============================================================
    // 範囲攻撃の頁
    // 判定は本番と同じ GPU（SwarmSystem の Area）。見た目は本番と同じく CPU 側で VFX を再生する。
    // 輪の線は CPU 側で同じ時計を回して描いているだけの目安（GPU からは読み戻していない）
    // ============================================================
    int   m_Tab = 0;                   // 0 = 投射物 / 1 = 範囲。開いている頁だけが自動で撃つ
    int   m_TabRequest = -1;           // キー（1 / 2）で頁を切り替える依頼。-1 = 無し
    int   m_AreaSelected = 0;          // AreaProfileDB の番号（0 = 無し）
    char  m_AreaNameBuf[64] = {};
    bool  m_AreaDirty = false;
    bool  m_AreaAutoCast = true;
    float m_AreaCastTimer = 0.0f;
    int   m_AreaPlace = 0;             // 0 = 一番近い標的 / 1 = 銃口（= 玩家。追従の確認用）/ 2 = 目印（ギズモで動かす）
    DirectX::SimpleMath::Vector3 m_AreaMarker = { 4.0f, 0.9f, 4.0f };
    std::vector<std::string> m_VfxFiles;

    struct LiveArea
    {
        DirectX::SimpleMath::Vector3 center;
        float radius = 0.0f, halfHeight = 0.0f;
        float timeLeft = 0.0f, tickTimer = 0.0f, tickInterval = 0.0f;
        float flash = 0.0f;            // tick した直後だけ > 0（輪を白く光らせる）
        bool  follow = false;
    };
    std::vector<LiveArea> m_LiveAreas;

    AreaVFXPlayer   m_AreaVFX;
    VFXMeshRenderer m_MeshRenderer;    // 法環の Mesh entry 用
    VFXContext      m_VFXContext;

    // ---- 表示 ----
    bool  m_ShowCurve = true;
    bool  m_ShowGrid = true;
    float m_LightDir[3] = { 0.5f, -1.0f, 0.5f };
};
