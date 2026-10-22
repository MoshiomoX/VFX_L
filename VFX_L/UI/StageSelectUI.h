// ============================================================
// StageSelectUI.h
// ステージ選択の画面（2026-10-21、Megabonk の map select に倣う）。
//   左：見出し「ステージ選択」→ マップの一覧（草原 / 砂漠 / 遺跡。名前 + 一言 + 未解放の条件）→ Tier の箱 1〜3（鍵 / 選択 / ★クリア済み）
//       → Tier の説明（何面続くか・敵の倍率）→ 出発 / 戻る のボタン → 操作の案内
//   背景は戦闘シーンそのもの（StageSelectScene）。マップのカーソルが動いたらシーンが地形を作り直す（ConsumeMapChanged）
//   キー：W S / 上下 / 十字でマップ、A D / 左右 / 十字でTier、Enter / A で出発、Esc / B で戻る。マウス：行・箱・ボタンをクリック
//   進み具合は Core/GameProgress（開いていないマップ・Tier は選べるが出発できない）
// UI は戦闘と同じくシーンの HDR バッファに描く（色は線形の値）
// ============================================================
#pragma once
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "World/StageConfig.h"
#include <memory>
#include <string>

class Graphics;
class Texture;

class StageSelectUI
{
public:
    enum Action { kNone = -1, kStart = 0, kBack = 1 };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, float screenW, float screenH);
    void Shutdown();
    void Layout(float screenW, float screenH);

    // 決まった事（kNone = まだ）。出発が決まった後はもう何も返さない
    int  Update(float dt);
    void Render(Graphics& gfx);

    int  Map() const { return m_Map; }     // 1..kStageCount
    int  Tier() const { return m_Tier; }   // 1..MaxTier(map)
    void SetSelection(int map, int tier);
    // マップのカーソルが動いて、kSwitchDelay 秒そのまま（連打の途中では作り直さない）：1 回だけ true。シーンが地形を作り直す
    // （2026-10-21 ユーザー：「カーソルが触れる度に地形を読み直して重い」→ マウスの hover では切り替えない + 遅延）
    bool ConsumeMapChanged();
    bool IsStarting() const { return m_Starting; }
    // TEMP-TEST：自動テスト用。選択を変えて「マップが変わった」も立てる（遅延なし）
    void TestSelect(int map, int tier) { SetSelection(map, tier); m_MapChanged = true; m_ChangeTimer = 0.0f; }

private:
    struct Rect
    {
        DirectX::SimpleMath::Vector2 pos, size;
        bool Contains(const DirectX::SimpleMath::Vector2& p) const
        {
            return p.x >= pos.x && p.y >= pos.y && p.x <= pos.x + size.x && p.y <= pos.y + size.y;
        }
    };

    void MoveMap(int dir);
    void MoveTier(int dir);
    void SelectMap(int map);
    void SelectTier(int tier);
    bool TryStart();
    std::wstring TierDescription(int tier) const;

    SpriteRenderer m_Sprite;
    TextRenderer   m_Text;
    std::shared_ptr<Texture> m_White;
    bool  m_Ready = false;

    float m_ScreenW = 1600.0f;
    float m_ScreenH = 900.0f;
    float m_K = 1.0f;       // 文字・余白の倍率（画面短辺 / 900 × UI 全体の倍率）
    float m_Left = 0.0f;    // 左の余白（px）
    float m_Time = 0.0f;
    float m_InputDelay = 0.0f;   // 開いた直後は決定を受けない（押しっぱなしの Enter で即出発しない）
    bool  m_Starting = false;
    bool  m_MapChanged = false;
    float m_ChangeTimer = 0.0f;  // 最後にマップが変わってからの秒（kSwitchDelay を過ぎたら ConsumeMapChanged が答える）
    int   m_Map = 1;
    int   m_Tier = 1;
    float m_DenyFlash = 0.0f;    // 出発できない時の赤い点滅（秒）

    // マウス用の矩形（Layout / Render で置く）
    Rect m_MapRows[StageConfig::kStageCount];
    Rect m_TierBoxes[StageConfig::kMaxTier];
    Rect m_StartBtn, m_BackBtn;
    DirectX::SimpleMath::Vector2 m_LastMouse = { -1.0f, -1.0f };
};
