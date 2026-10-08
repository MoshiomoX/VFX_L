// ============================================================
// HUD.h
// 常時表示の HUD
//   経験値バー（最上段）、HP / MP バー（左上）、経過時間と撃破数（上の中央）、
//   魔法の欄（下の中央。クールダウンと MP 不足）、その上の魔力解放（Q）の大きな欄、
//   瀕死の赤い縁、画面外の目印（箱・エリート）
//
// UIManager には入れない（モーダルではなく、入力も取らない）。
// 描画は SpriteRenderer / TextRenderer に相乗りし、
// 自前の GPU リソースは 1x1 の白テクスチャだけ。
// バーは白テクスチャを色染めして描く（LevelUpUI と同じ方式）。
//
// 調整用の数値は HUDStyle にまとめ、ImGui から触って
// Assets/Data/HUD.json へ保存する。見た目を詰めるたびに
// コードを書き換えて再コンパイルするのを避けるため。
//
// 位置は「アンカー（画面比 0..1）+ オフセット（ピクセル）」で持つ。
// 生のピクセルだけだと解像度が変わった時に配置が崩れ、
// Layout() が走るたびに ImGui の調整が消えてしまう。
// ============================================================
#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <SimpleMath.h>
#include "SpellID.h"

class SpriteRenderer;
class TextRenderer;
class Texture;
struct HealthComponent;
struct ShieldComponent;
struct ManaComponent;
struct LevelComponent;
struct WandComponent;

// ============================================================
// 画面外の目印を出したい物（世界座標）。シーンが毎フレーム積む
// ============================================================
struct HUDMarker
{
    DirectX::SimpleMath::Vector3 position;
    DirectX::SimpleMath::Vector4 color = { 1, 1, 1, 1 };
};

// ============================================================
// HUD が毎フレーム受け取る、コンポーネント以外の情報
// ============================================================
struct HUDFrameInfo
{
    float    runTime = 0.0f;       // 経過時間（止まっている間は進まない物）
    float    stageTime = 0.0f;     // 面の制限時間。> 0 なら残りを数え下ろし、過ぎたら超過分を赤く「+」で出す
    float    bossHp = -1.0f;       // Boss の HP 条（0..1）。負なら出さない
    uint32_t kills = 0;            // 撃破数（GPU counter の累計）
    int      stage = 0;            // 面の番号（1..）。0 なら出さない
    const wchar_t* stageName = L"";
    int      gold = -1;            // 金貨（2026-10-04）。MP バーの下に出す。負なら出さない
    const wchar_t* objective = nullptr;    // 目標の一行（撃破数の下。null / 空なら出さない。2026-10-07）
    const wchar_t* announce = nullptr;     // 画面の真ん中の案内（null / 空なら出さない。2026-10-07）
    float announceAlpha = 0.0f;            // 0..1（出入りのフェード）
    float announceAge = 1.0f;              // 出てからの秒（出た瞬間の大きさの戻り用）
    const ShieldComponent* shield = nullptr;  // シールド（HP バーの下の細いバー。null / 上限 0 なら出さない）
    const WandComponent* wand = nullptr;   // 魔法の欄。null なら出さない

    // 画面外の目印。viewProj は世界 → クリップ（SimpleMath の行ベクトル順 view * proj）
    DirectX::SimpleMath::Matrix viewProj;
    std::vector<HUDMarker> markers;
};

// ============================================================
// 画面上の一点。pos = anchor * 画面サイズ + offset
// anchor(0,0) = 左上、(1,1) = 右下。右下に置きたい要素は
// anchor(1,1) + 負の offset で表す。
// ============================================================
struct HUDAnchor
{
    DirectX::SimpleMath::Vector2 anchor = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 offset = { 0.0f, 0.0f };

    DirectX::SimpleMath::Vector2 Resolve(float screenW, float screenH) const
    {
        return { anchor.x * screenW + offset.x,
                 anchor.y * screenH + offset.y };
    }
};

// ============================================================
// HUD の見た目パラメータ（純データ）
// 増える時はここに足して、DrawDebugUI と JSON の両方へ足す
// ============================================================
struct HUDStyle
{
    // ---- レイアウト ----
    HUDAnchor expBar = { { 0.0f, 0.0f }, {  0.0f,  0.0f } };
    float expBarHeight = 10.0f;
    float expBarMargin = 0.0f;    // 左右の余白。幅 = 画面幅 - margin * 2

    HUDAnchor lvText = { { 0.0f, 0.0f }, { 20.0f, 16.0f } };
    HUDAnchor hpBar = { { 0.0f, 0.0f }, { 20.0f, 44.0f } };
    HUDAnchor mpBar = { { 0.0f, 0.0f }, { 20.0f, 72.0f } };

    // HP と MP は別々に調整できる（片方だけ太くする等）
    DirectX::SimpleMath::Vector2 hpBarSize = { 260.0f, 22.0f };
    DirectX::SimpleMath::Vector2 mpBarSize = { 260.0f, 22.0f };
    float barPadding = 2.0f;      // 中身が背景の内側へ入る量

    // ---- 文字 ----
    float lvTextScale = 0.5f;
    float barTextScale = 0.45f;
    bool  centerBarText = true;   // バーの中央に寄せる
    DirectX::SimpleMath::Vector2 barTextOffset = { 8.0f, 3.0f };  // 非中央時の左上からの位置
    bool  textShadow = true;
    float textShadowOffset = 1.0f;

    // ---- ダメージ遅延バー ----
    // 減った分を残像として残し、少し遅れて追いつかせる。
    // 一撃でどれだけ持っていかれたかを目で追えるようにするため。
    bool  damageTrail = true;
    float trailDelay = 0.25f;     // 減ってから動き出すまでの秒数
    float trailSpeed = 0.9f;      // 追いつく速さ（割合 / 秒）

    // ---- 枠（幻想 UI：古金の細い線）----
    bool  drawBorder = true;
    float borderSize = 1.0f;
    float gemSize = 9.0f;         // HP / MP バーの両端の菱形（0 = 無し）

    // ---- 配色 ----
    // 戦闘の UI はシーンの HDR バッファに描かれ、トーンマップ + ガンマを通る。色は全部線形の値
    // （sRGB の見た目 c なら c^2.2。0.07 でも画面では中間の灰色になる）
    DirectX::SimpleMath::Vector4 bgColor = { 0.003f, 0.0025f, 0.005f, 0.95f };
    DirectX::SimpleMath::Vector4 hpColor = { 0.55f, 0.035f, 0.030f, 1.0f };    // 見た目 #C0342F くらい
    DirectX::SimpleMath::Vector4 mpColor = { 0.050f, 0.16f, 0.75f, 1.0f };     // 見た目 #3F6FE0 くらい
    DirectX::SimpleMath::Vector4 mpSurgeColor = { 1.10f, 0.70f, 0.10f, 1.0f }; // 魔力解放中（金。1 を少し超えて光る。脈打つ）
    DirectX::SimpleMath::Vector4 expColor = { 0.79f, 0.49f, 0.07f, 1.0f };     // 見た目 #E6B84D くらい
    DirectX::SimpleMath::Vector4 trailColor = { 1.00f, 0.85f, 0.60f, 0.50f };
    DirectX::SimpleMath::Vector4 borderColor = { 0.69f, 0.47f, 0.15f, 0.90f }; // 古金（UIDeco と同じ）
    DirectX::SimpleMath::Vector4 textColor = { 0.91f, 0.84f, 0.70f, 1.0f };    // 暖かい白
    DirectX::SimpleMath::Vector4 shadowColor = { 0.00f, 0.00f, 0.00f, 0.85f };

    // ---- 経過時間と撃破数（アンカーは文字の上端の中央）----
    HUDAnchor runInfo = { { 0.5f, 0.0f }, { 0.0f, 16.0f } };
    float timerScale = 0.80f;
    float killScale = 0.42f;
    float runDividerWidth = 200.0f;   // 時間と撃破数の間の細い分割線（0 = 無し）

    // ---- 魔法の欄（アンカーは欄の下端の中央）----
    // 丸い欄：暗い円 + 古金の輪 + 後ろでゆっくり回る魔法陣。クールダウンは円を上から暗く
    bool  showSpellBar = true;
    HUDAnchor spellBar = { { 0.5f, 1.0f }, { 0.0f, -26.0f } };
    float slotSize = 50.0f;
    float slotGap = 26.0f;            // 魔法陣が欄より大きいので広め
    float slotCircleScale = 1.5f;     // 魔法陣の直径 / 欄（0 = 無し）
    DirectX::SimpleMath::Vector4 slotBgColor = { 0.003f, 0.0025f, 0.005f, 0.98f };
    DirectX::SimpleMath::Vector4 cooldownColor = { 0.00f, 0.00f, 0.00f, 0.78f };
    DirectX::SimpleMath::Vector4 noManaColor = { 0.010f, 0.020f, 0.14f, 0.70f };   // MP が足りない時に被せる

    // ---- シールド（2026-10-07。HP バーのすぐ下の細いバー。右に絵 + 「今 / 上限」）----
    // 満タン = 明るい水色、減っている（戻り待ち・戻り中）= 暗い青（Megabonk の配色）。満タンに戻った瞬間に一度光る。
    // 出している間は MP バーと金貨を shieldBarGap + shieldBarHeight だけ下へずらす
    bool  showShield = true;
    float shieldBarHeight = 8.0f;
    float shieldBarGap = 3.0f;        // HP バーとの間
    float shieldIconSize = 16.0f;
    float shieldTextScale = 0.34f;
    DirectX::SimpleMath::Vector4 shieldColor = { 0.22f, 0.65f, 1.00f, 1.0f };          // 満タン（線形。画面で #82D3FF くらい）
    DirectX::SimpleMath::Vector4 shieldChargingColor = { 0.025f, 0.09f, 0.30f, 1.0f }; // 減っている（画面で #2D5394 くらい）
    DirectX::SimpleMath::Vector4 shieldTextColor = { 0.55f, 0.85f, 1.00f, 1.0f };

    // ---- 金貨（2026-10-04。MP バーの下、以前の魔力解放の文字の所。アンカーは絵の左上）----
    // 増えた瞬間は少し明るく膨らむ
    bool  showGold = true;
    HUDAnchor goldText = { { 0.0f, 0.0f }, { 20.0f, 100.0f } };
    float goldIconSize = 24.0f;
    float goldTextScale = 0.50f;
    DirectX::SimpleMath::Vector4 goldColor = { 1.00f, 0.62f, 0.10f, 1.0f };   // 線形の金（画面で #FFCF59 くらい）

    // ---- 魔力解放（Q）の大きな欄（2026-10-04 ユーザー指定：MP バーの下の文字をやめ、魔法の欄の上の中央に大きく）----
    // 魔法の欄と同じ作り（暗い円 + 輪 + 後ろの魔法陣）を大きくした物。アンカーは欄の下端の中央。
    // 使える = 明るい + ゆっくり息をする、解放中 = 金に光って残り秒、再使用待ち = 上から暗く + 残り秒。
    // 下の縁に「Q」の小さな札
    bool  showSurgeSkill = true;
    HUDAnchor surgeSkill = { { 0.5f, 1.0f }, { 0.0f, -114.0f } };
    float surgeSkillSize = 72.0f;          // 魔法の欄（50）より一回り大きい
    float surgeCircleScale = 1.45f;        // 魔法陣の直径 / 欄（0 = 無し）
    float surgeNumberScale = 0.62f;        // 残り秒の文字
    float surgeKeyScale = 0.36f;           // 「Q」の札の文字
    // 解放中の光（HDR。1 を超えた分が bloom で滲む）
    DirectX::SimpleMath::Vector4 surgeActiveColor = { 1.60f, 0.95f, 0.20f, 1.0f };

    // ---- 瀕死の赤い縁（HP が lowHpRatio を切ったら出す。低いほど濃い）----
    bool  lowHpVignette = true;
    float lowHpRatio = 0.30f;
    float vignetteWidth = 0.10f;    // 画面短辺に対する縁の太さ
    float vignettePulse = 5.0f;     // 明滅の速さ（rad/s）
    DirectX::SimpleMath::Vector4 vignetteColor = { 0.85f, 0.05f, 0.05f, 0.55f };

    // ---- 魔力解放中の金の縁（解放の間だけ。出入りはフェード）----
    bool  surgeVignette = true;
    float surgeVignetteWidth = 0.09f;
    // 草の緑と混ざると黄緑に寄るので、赤みを多めにした金
    DirectX::SimpleMath::Vector4 surgeVignetteColor = { 1.00f, 0.40f, 0.035f, 0.55f };

    // ---- 画面外の目印（画面の縁に、その方向を指す矢印）----
    bool  showMarkers = true;
    float markerSize = 18.0f;
    float markerMargin = 40.0f;     // 画面の縁からの距離

    // ---- 目標（2026-10-07 ユーザー「任務目標を足す」：撃破数の下に「目標: …」。文はシーンが毎フレーム入れる）----
    bool  showObjective = true;
    float objectiveScale = 0.40f;
    DirectX::SimpleMath::Vector4 objectiveColor = { 1.00f, 0.80f, 0.40f, 1.0f };   // 金（線形）

    // ---- 画面の真ん中の案内（2026-10-07：押し寄せ・エリート・最終ウェーブ。文は StageDirector、UI/HUDAnnounce.cpp）----
    bool  showAnnounce = true;
    float announceY = 0.30f;        // 画面の高さに対する文字の中心
    float announceScale = 0.80f;
    float announcePunch = 0.25f;    // 出た瞬間だけ大きくする量（0.25 = 1.25 倍から縮む）
    DirectX::SimpleMath::Vector4 announceColor = { 1.00f, 0.32f, 0.12f, 1.0f };   // 赤みの橙（線形）

    // ---- 文字の下敷き（2026-10-07 ユーザー：文字だけの所が草の上で読みにくい）----
    // Lv・経過時間〜目標の塊・金貨に、暗い半透明の板 + 薄い古金の枠を敷く（バーの中の文字は対象外）。
    // 色は線形。HDR に混ぜてからトーンマップするので、0.75 だと明るい草が緑に透けた（10-07 自動テストの撮影）→ 0.88
    bool  textPlates = true;
    DirectX::SimpleMath::Vector4 plateColor = { 0.003f, 0.0025f, 0.005f, 0.88f };
    float plateLineAlpha = 0.35f;                          // 枠の濃さ（0 = 枠無し）
    DirectX::SimpleMath::Vector2 platePad = { 10.0f, 3.0f };   // 文字の外接矩形から外へ（px、倍率が掛かる）

    // px の値・文字の倍率を k 倍した物（UIDeco::UIScale()。画面比・色・比率はそのまま）。
    // m_Style は ImGui / JSON の元の値のまま、描く時はこの写しを使う
    HUDStyle Scaled(float k) const;
};

class HUD
{
public:
    bool Initialize(ID3D11Device* device);

    // 画面サイズを記録する（リサイズ時にも呼ぶ）
    // 実際の位置は Draw の時に HUDStyle から解決するので、
    // ImGui でいじった値がここで潰れることはない
    void Layout(float screenW, float screenH);

    // 残像の追従。描画しない間（モーダル表示中）も進める。shield は無ければ null
    void Update(float dt, const HealthComponent& hp, const ManaComponent& mp,
        const ShieldComponent* shield = nullptr);

    // 魔力解放の状態は魔法の欄の上の大きな欄に出す（解放中は MP バーも金）
    void Draw(SpriteRenderer& sprite, TextRenderer& text,
        const HealthComponent& hp, const ManaComponent& mp,
        const LevelComponent& lv, const HUDFrameInfo& info);

    // 魔法の欄のアイコン（無ければアイテムの色の四角）。GameUI がバックパックと同じ物を渡す
    void SetIconLookup(std::function<std::shared_ptr<Texture>(ItemID)> f) { m_IconLookup = std::move(f); }

    // ---- 調整用（ImGui）----
    void DrawDebugUI();

    bool SaveStyle(const char* path = nullptr) const;
    bool LoadStyle(const char* path = nullptr);
    void ResetStyle();

    HUDStyle& Style() { return m_Style; }
    const HUDStyle& Style() const { return m_Style; }
    // 実際に描いている大きさ（m_Style × UIDeco::UIScale()。GameUI が案内文字の位置を合わせる時に見る）
    const HUDStyle& ScaledStyle() const { return m_S; }

private:
    // 減った分だけ遅れて追いつく値（0..1）
    struct BarTrail
    {
        float value = 1.0f;
        float timer = 0.0f;

        void Update(float dt, float ratio, const HUDStyle& style);
    };

    // 背景 → 残像 → 中身 → 枠 の順で1本のバーを描く
    // ratio / trailRatio は 0..1 に丸めて渡すこと
    // gems: 両端に菱形を付ける（HP / MP。画面幅いっぱいの経験値バーには付けない）
    void DrawBar(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size,
        float ratio, float trailRatio,
        const DirectX::SimpleMath::Vector4& fillColor, bool gems = false);

    void DrawBorder(SpriteRenderer& sprite,
        const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size);

    // 文字の下敷き（textPlates が切れていれば何もしない）。pos / size = 文字の外接矩形
    void Plate(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector2& pos,
        const DirectX::SimpleMath::Vector2& size) const;

    // 影付きの一行。影を切ると 1 回分の Draw で済む
    void DrawLabel(TextRenderer& text, const std::wstring& str,
        const DirectX::SimpleMath::Vector2& pos, float scale);

    // バーの中に重ねる文字（centerBarText で中央寄せ）
    void DrawBarLabel(TextRenderer& text, const std::wstring& str,
        const DirectX::SimpleMath::Vector2& barPos,
        const DirectX::SimpleMath::Vector2& barSize);

    HUDStyle m_Style;
    HUDStyle m_S;      // m_Style × UIDeco::UIScale()（Update / Draw の頭で作り直す。描く側はこちらを見る）

    std::shared_ptr<Texture> m_WhiteTex;
    std::function<std::shared_ptr<Texture>(ItemID)> m_IconLookup;
    std::shared_ptr<Texture> m_SurgeIcon;   // 魔力解放の欄の絵（Res::Icon::ManaSurge）
    std::shared_ptr<Texture> m_GoldIcon;    // 金貨の絵（Res::Icon::Gold）
    std::shared_ptr<Texture> m_ShieldIcon;  // シールドの絵（Res::Icon::ShieldUp。能力カードと同じ）
    BarTrail m_ShieldTrail;
    bool  m_ShieldWasFull = true;           // 満タンに戻った瞬間を見つける
    float m_ShieldFullFlash = 0.0f;         // その時の光（1 → 0）
    int   m_LastGold = -1;                  // 増えた瞬間を見つける
    float m_GoldPulseAt = -10.0f;           // 最後に増えた時刻（m_Time）
    float m_Time = 0.0f;   // 明滅用（Update で進める）
    // 魔力解放が使えるようになった瞬間の光（1 → 0）。開始時は使える状態なので光らせない
    bool  m_SurgeWasReady = true;
    float m_SurgeReadyFlash = 0.0f;

    void DrawRunInfo(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info);
    void DrawSpellBar(SpriteRenderer& sprite,
        const WandComponent& wand, const ManaComponent& mp);
    void DrawSurgeSkill(SpriteRenderer& sprite, TextRenderer& text, const ManaComponent& mp);
    void DrawGold(SpriteRenderer& sprite, TextRenderer& text, int gold, float yShift);
    // HP バーの下のシールドのバー（UI/HUDShieldBar.cpp）。戻り値 = 下の物をずらす量（px）
    float DrawShieldBar(SpriteRenderer& sprite, TextRenderer& text, const ShieldComponent& shield,
        const DirectX::SimpleMath::Vector2& hpPos);
    void UpdateShield(float dt, const ShieldComponent* shield);
    void DrawLowHpVignette(SpriteRenderer& sprite, const HealthComponent& hp);
    void DrawSurgeVignette(SpriteRenderer& sprite, const ManaComponent& mp);
    // 画面の四辺のぼかし（外端の濃さ edgeAlpha、太さ = 画面短辺 × widthRatio）
    void DrawEdgeGlow(SpriteRenderer& sprite, const DirectX::SimpleMath::Vector4& color,
        float edgeAlpha, float widthRatio);
    void DrawMarkers(SpriteRenderer& sprite, const HUDFrameInfo& info);
    // 画面の真ん中の案内（UI/HUDAnnounce.cpp）
    void DrawAnnounce(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info);
    void DrawAnnounceImGui();

    BarTrail m_HpTrail;
    BarTrail m_MpTrail;

    float m_ScreenW = 1920.0f;
    float m_ScreenH = 1080.0f;
};
