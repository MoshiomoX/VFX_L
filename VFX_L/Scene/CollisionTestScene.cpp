// ============================================================
// CollisionTestScene.cpp
// 戦闘シーンの入口：初期化の流れ・終了・画面サイズ・小さな問い合わせ。
// 2026-10-25 に 1540 行あったのを分けた（ユーザー「長すぎる」。中身はそのまま、順番も同じ）:
//   CollisionTestSceneInit.cpp      初期化の各段（描画 → 面 → 地形 → GPU の群れ → プレイヤー → 面の流れ）と面の引き継ぎ
//   CollisionTestSceneUpdate.cpp    毎フレームの流れ（UI・メニュー・案内・死亡 → リザルト）・HUD の目印・EndRun
//   CollisionTestSceneGameplay.cpp  gameplay の System の実行順（止まっている間は走らない）
//   CollisionTestSceneRender.cpp    描画の順番・松明の光
//   CollisionTestSceneField.cpp     フィールドの物：世界の風・突風の風源・報酬の箱 / 門 / 磁石・雑魚用のマスの塞ぎ
// ImGui のパネルとデバッグ描画は CollisionTestSceneDebug.cpp
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Debug/AutoTest/BattleAutoTest.h"   // TEMP-TEST
#include "Component/TransformComponent.h"
#include "Component/HealthComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Item/ItemDatabase.h"
#include "Scene/RunResult.h"
#include "Debug/DebugManager.h"
#include "Core/Application.h"
#include "Core/DevUI.h"

// ============================================================
// Init
// 順番に意味がある（ItemDatabase は UI より先、地形は GPU の群れより先、箱はプレイヤーの後）。
// 各段の中身は CollisionTestSceneInit.cpp
// ============================================================
void CollisionTestScene::Init()
{
    std::cout << "[CollisionTestScene] Init" << std::endl;
    if (IsMenuScene()) g_RunCarry.Reset();   // タイトル・ステージ選択からは最初から（面の引き継ぎを捨てる）
    else if (!m_LabScene)
    {
        // 新しい面：リザルトの背景の写真を撮り直す
        SceneSnapshot::Get().Clear();
        m_Highlight.Reset();
    }

    // DebugManager の y = 0 の参照格子は消す（地面と同じ高さで深度が競り合い、地面に 1m の縞が出ていた。
    // 9-30 から「地面に格子線」と記録していた物の正体）。Shutdown で元に戻す（エディタのシーンは使う）
    m_PrevDebugGrid = DebugManager::Get().GetShowGrid();
    DebugManager::Get().SetShowGrid(false);

    auto& gfx = Application::Get().GetGraphics();
    auto* device = gfx.GetDevice();
    auto* context = gfx.GetContext();

    // ---------- 画面サイズを最初に確定させる ----------
    m_ScreenW = gfx.GetWidth();
    m_ScreenH = gfx.GetHeight();

    // ---------- アイテム定義の登録（他より先に行う）----------
    ItemDatabase::Initialize();

    // ---------- Camera（遮蔽の射線は地形の衝突へ）----------
    m_Camera.Init(m_ScreenW / m_ScreenH, &m_CollisionSystem);
    SetCamera(&m_Camera.Camera());
    {
        char env[64] = {};   // TEMP-TEST（自動テストの名前。16 だと 16 文字以上の名前が切れて feedback へ落ちていた）
        m_AutoTest = !IsMenuScene()   // タイトル・ステージ選択では回さない。Demo は負荷試験だけ（DevUI::AutoTestAllowed）
            && GetEnvironmentVariableA("VFXL_BATTLE_AUTOTEST", env, sizeof(env)) > 0
            && DevUI::AutoTestAllowed(env);
        m_AutoRunner = m_AutoTest ? BattleAutoTest::Create(env, *this) : nullptr;   // 値 = 自動テストの名前（Debug/AutoTest/）
        m_AutoStep = 0;
        m_AutoTime = 0.0f;
        if (m_AutoTest) AutoTestLog("start");
    }

    InitRenderers(device, context);                // 粒子・VFX の描画・UI・エリートの的・アウトライン・影
    InitStage();                                   // 何面目か・Tier・面の照明
    const StageDef& stageDef = StageConfig::Get(m_StageIndex);
    InitTerrainWorld(stageDef, device, context);   // 地形・ミニマップ・草・落ち葉・風に流される物
    InitSwarm(stageDef, device, context);          // GPU の雑魚・弾・範囲の表、難度、敵の混ざり方
    InitPlayer(stageDef, device);                  // プレイヤー・前の面の引き継ぎ・反応のエフェクト
    InitStageFlow(stageDef, device);               // 箱・門・Boss・天候、実験場 / タイトル / 選択、チュートリアル
}

// ============================================================
// Shutdown
// ============================================================
void CollisionTestScene::Shutdown()
{
    m_Audio.StopMusic();   // 次のシーン（タイトル・結果）が自分の曲を流す
    m_Weather.Shutdown();  // 環境音・粒子を止め、草と経験球の基準値を戻す
    m_PlayerVFX.Shutdown();   // 速さの尾を離す
    m_AreaVFX.StopAll();      // 再生中のエフェクトを粒子システムが生きているうちに放す（メンバの破棄は粒子システムの方が先）
    SceneLighting::ClearFog(Application::Get().GetRenderer());   // Renderer は他のシーンと共有
    m_Shadows.Disable(Application::Get().GetRenderer());         // 同上（影を切らないと他のシーンが真っ暗）
    m_Shadows.Unbind(Application::Get().GetGraphics().GetContext());
    m_Shadows.Shutdown();
    m_Swarm.Shutdown();
    m_StaticProps.Shutdown();
    m_Tumbleweeds.Clear(m_Registry);   // 実体を消す（次の Init の実体と番号が被らないうちに）
    ClearRuinsFoes();                  // 鎖の実体も同じく（2026-10-25）
    m_GrassShreds.Clear(m_AreaVFX);    // 草屑の発生源の番号を捨てる（AreaVFXPlayer はシーンと一緒に終わる）
    m_Dandelions.Shutdown();
    m_Debris.Shutdown();
    m_Water.Shutdown();
    m_GrassFire.Shutdown();
    m_Grass.Shutdown();
    m_GameUI.Shutdown();
    m_TitleUI.Shutdown();
    m_SelectUI.Shutdown();
    DebugManager::Get().SetShowGrid(m_PrevDebugGrid);   // 参照格子を元に戻す（DebugManager もシーン間で共有）
    std::cout << "[CollisionTestScene] Shutdown" << std::endl;
}

// ============================================================
// 画面サイズの変化に追従する
// UI は全部ピクセル指定なので、変わった時だけ組み直す。
// ============================================================
void CollisionTestScene::UpdateScreenSize()
{
    auto& gfx = Application::Get().GetGraphics();
    float w = gfx.GetWidth();
    float h = gfx.GetHeight();

    if (w == m_ScreenW && h == m_ScreenH) return;
    if (w <= 0.0f || h <= 0.0f) return;

    m_ScreenW = w;
    m_ScreenH = h;

    m_GameUI.Layout(w, h);
    m_Camera.Resize(w / h);

    std::cout << "[CollisionTestScene] screen resized: "
        << (int)w << "x" << (int)h << std::endl;
}

// ============================================================
// 小さな問い合わせ
// ============================================================
bool CollisionTestScene::IsPlayerDead()
{
    return m_Registry.IsValid(m_Player) && m_Registry.Has<PlayerStateComponent>(m_Player)
        && m_Registry.Get<PlayerStateComponent>(m_Player).IsDead();
}

const Vector3* CollisionTestScene::PlayerPos()
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<TransformComponent>(m_Player)) return nullptr;
    return &m_Registry.Get<TransformComponent>(m_Player).position;
}

// 被弾：HP の減りを見る。雑魚（GPU）・エリートの接触・デバッグの被弾、経路を問わず拾える。
// 無敵中は TryApplyHit が HP を減らさないので、揺れ・斬撃も自然に止まる
float CollisionTestScene::TrackPlayerHpLoss()
{
    if (!m_Registry.IsValid(m_Player) || !m_Registry.Has<HealthComponent>(m_Player)) return 0.0f;
    const float hpNow = m_Registry.Get<HealthComponent>(m_Player).current;
    const float lost = (m_PrevPlayerHp >= 0.0f) ? m_PrevPlayerHp - hpNow : 0.0f;
    m_PrevPlayerHp = hpNow;
    return (lost > 0.0f) ? lost : 0.0f;
}

// 爆発（GPU の範囲・Boss のスラム）：カメラを揺らし、リザルトの写真の山場として数える
void CollisionTestScene::OnExplosions(int count)
{
    m_Camera.OnShakeAreas(count);
    m_Highlight.OnExplosions(count, m_RunTime);
}
