// ============================================================
// CollisionTestSceneUpdate.cpp
// CollisionTestScene の毎フレームの流れ（2026-10-25 に CollisionTestScene.cpp から分けた。中身はそのまま）:
//   UI・トレーニングの依頼・メニュー・チュートリアル → gameplay（止まっていなければ）→ 案内・HUD → 死亡 / クリア → リザルト
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Debug/AutoTest/BattleAutoTest.h"   // TEMP-TEST
#include "Component/TransformComponent.h"
#include "Component/HealthComponent.h"
#include "Component/InteractableComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "Enemy/EnemyTags.h"
#include "ECS/View.h"
#include "Scene/RunResult.h"
#include "Core/GameSettings.h"
#include "Debug/FrameProfiler.h"
#include "Core/Application.h"

// ============================================================
// Update
// ============================================================
void CollisionTestScene::Update(float dt)
{
    if (m_TitleScene) { UpdateTitle(dt); return; }     // タイトル：景色とメニューだけ（CollisionTestSceneTitle.cpp）
    if (m_SelectScene) { UpdateSelect(dt); return; }   // ステージ選択：景色と選択の画面（CollisionTestSceneSelect.cpp）
    SceneBase::Update(dt);
    m_TotalTime += dt;

    UpdateScreenSize();

    // ---- UI（開閉・入力・プレイヤー消失時の後始末は全部 GameUI の中）----
    m_GameUI.Update(m_Registry, m_Player, dt);

    // ---- トレーニング（実験場）のメニューの依頼：的・群れ・Boss・木箱（メニューを開いたまま＝止まっている間も）----
    if (m_LabScene)
        for (const TrainingRequest& q : m_GameUI.ConsumeTrainingRequests())
        {
            // フィールドの切替は地形ごと建て直すのでシーンが受け持つ（2026-10-20）。他は SpellLab
            if (q.kind == TrainingRequest::Kind::Field)
                m_GameUI.SetTrainingMessage(EnterTrainingField(q.field));
            else if (q.kind == TrainingRequest::Kind::Elite)
            {
                // 面の固定エリート（2026-10-25）：その面の A / B、詞缀は 0〜2 個を乱数で
                uint32_t affixes = 0;
                const int want = rand() % 3;
                for (int n = 0, guard = 0; n < want && guard < 32; ++guard)
                {
                    const uint32_t bit = 1u << (rand() % kEliteAffixCount);
                    if (affixes & bit) continue;
                    affixes |= bit;
                    ++n;
                }
                const Vector3* pp = PlayerPos();
                const bool ok = pp && m_StageElites.Spawn(m_Grid, m_Swarm, *pp, q.eliteRanged, affixes, m_Stage.eliteHp * m_Mobs.GetHpMul(), 10.0f, 14.0f);
                m_GameUI.SetTrainingMessage(ok ? std::wstring(m_StageElites.LastSpawnName()) + L" を呼んだ" : std::wstring(L"エリートを置ける所が無い"));
            }
            else
                m_GameUI.SetTrainingMessage(m_SpellLab.Apply(q, m_Registry, m_Player, m_Swarm, m_Mobs, m_Stage, m_Grid));
        }

    // ---- マウスの捕獲（UI の開閉を見るので GameUI の後）----
    // UI（グリッド・三択・一時停止）が開いている間と死んだ後はマウスで操作するのでカーソルを出す
    m_Camera.UpdateMouseCapture(m_GameUI.IsModalOpen() || IsPlayerDead());

    // ---- 一時停止のメニューで選ばれたシーンの切替 ----
    switch (m_GameUI.ConsumeMenuAction())
    {
    case PauseMenuUI::Action::Restart:   // 実験場ではやり直しも実験場
        Application::Get().GetGame().GetSceneManager().RequestChangeScene(m_LabScene ? SceneType::SPELL_LAB : SceneType::COLLISION_TEST);
        break;
    case PauseMenuUI::Action::Title:
        Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::TITLE);
        break;
    case PauseMenuUI::Action::SkipTutorial:
        m_Tutorial.Skip();
        break;
    default:
        break;
    }

    // ---- 初回のチュートリアル（止まっている間も進める：バックパックを開いたかを見る）----
    if (m_Tutorial.Active())
    {
        TutorialGuide::Input in;
        if (const Vector3* pp = PlayerPos()) { in.playerPos = *pp; in.hasPlayer = true; }
        if (m_Registry.Has<PlayerStateComponent>(m_Player))
        {
            const auto& ps = m_Registry.Get<PlayerStateComponent>(m_Player);
            in.inAir = ps.move == MoveStateID::Jump;
            in.sliding = ps.slideActive;
        }
        in.kills = m_Swarm.GetCounters().killCount;
        if (m_Registry.Has<LevelComponent>(m_Player)) in.level = m_Registry.Get<LevelComponent>(m_Player).level;
        if (m_Registry.Has<ManaComponent>(m_Player)) in.surgeActive = m_Registry.Get<ManaComponent>(m_Player).surgeTime > 0.0f;
        in.backpackOpen = m_GameUI.IsBackpackOpen();
        in.choosingCard = LevelUpSystem::IsAnyoneChoosing(m_Registry);
        m_Tutorial.Update(dt, in);
    }
    if (m_Tutorial.JustFinished())
    {
        GameSettings::Get().tutorialDone = true;
        GameSettings::Get().Save();
    }
    m_GameUI.SetTutorialActive(m_Tutorial.Active());

    // ---- 集約: グリッドが変わっていれば杖を組み直す ----
    // UI の直後に置く。編成した結果を同じフレームで反映させるため
    m_BackpackAggregate.Update(m_Registry);

    // ---- シーン光源: 点光源表は UpdateGameplay の中（CollectLights）で GPU へ上がるので、その前に積む。
    // 一時停止中も積む（その時は SceneBase::Render が上げる）
    m_Lighting.SubmitPointLights();
    SubmitTorchLights();                      // 遺跡の松明（プレイヤーに近い物だけ）
    m_Interaction.SubmitLights(m_Registry);   // 報酬の箱の目印（止まっている間も消さない）
    m_Pickups.SubmitLights();                 // 磁石の目印

    // TEMP-TEST: UI の自動テストはバックパック・一時停止を開くので（gameplay が止まる）、ここで回す
    if (m_AutoRunner) m_AutoRunner->UpdateFrame(dt);   // TEMP-TEST: バックパック・一時停止・選択の画面を開いたまま進める自動テスト

    if (!m_GameUI.ShouldPauseGame())
    {
        PROFILE_SCOPE("Gameplay");
        UpdateGameplay(dt);

        // 音：GPU の範囲（命中・爆発・撃破）、プレイヤーの跳び / 着地 / 滑り、BGM の切り替え、一回物
        BattleAudio::State as;
        as.bossAlive = m_Stage.IsBossAlive();
        as.bossDefeated = m_Stage.IsBossDefeated();
        as.finalWave = m_Stage.stageTime > 0.0f && m_RunTime >= m_Stage.stageTime;
        as.playerDead = IsPlayerDead();
        as.cleared = m_Stage.IsCleared();
        m_Audio.Update(dt, m_Swarm, m_Registry, m_Player, as);
    }
    else
    {
        // 止まっている間（バックパック・四択・一時停止）は AreaVFX の Update が走らない。
        // Mesh / Sprite / 光線 / 液面 / 点光源は毎フレーム出し直さないと消えるので、時間を進めずに出し直す（2026-10-25）
        m_AreaVFX.SubmitStill();
    }

    // ---- 画面下の操作案内（近くに使える物がある時だけ）----
    // 報酬の箱は値段入り（2026-10-04、金貨で開ける）。足りなければ「金貨が足りない 所持 / 値段」
    {
        const wchar_t* prompt = m_Interaction.HasFocus() ? m_Interaction.GetPrompt() : nullptr;
        const Entity focus = m_Interaction.GetFocus();
        if (prompt && m_Registry.IsValid(focus) && m_Registry.Has<InteractableComponent>(focus)
            && m_Registry.Get<InteractableComponent>(focus).kind == InteractKind::RewardChoice
            && !m_Crates.IsFree(focus)   // 倒したエリートの金色の宝箱は無料（案内は箱の物のまま）
            && m_Registry.Has<WalletComponent>(m_Player))
        {
            const int price = m_Crates.Price();
            const int coins = m_Registry.Get<WalletComponent>(m_Player).Coins();
            wchar_t buf[64];
            if (coins >= price) swprintf_s(buf, L"<{K:F}|{B}>  開ける     金貨 %d", price);
            else                swprintf_s(buf, L"金貨が足りない     %d / %d", coins, price);
            m_PromptBuf = buf;
            prompt = m_PromptBuf.c_str();
        }
        m_GameUI.SetPrompt(prompt);
    }
    if (m_Registry.Has<WalletComponent>(m_Player))
        m_GameUI.SetGold(m_Registry.Get<WalletComponent>(m_Player).Coins());

    // ---- HUD：経過時間・撃破数と、画面外の目印 ----
    m_GameUI.SetRunInfo(m_RunTime, m_Swarm.GetCounters().killCount, m_Stage.stageTime);
    m_GameUI.SetStage(m_StageIndex - m_RunStart + 1, m_RunTier, StageConfig::Get(m_StageIndex).name);   // 「ステージ n/Tier 名前」
    m_GameUI.SetBossBar(m_Stage.IsBossAlive() ? m_Stage.BossHpRatio() : -1.0f);
    UpdateHudMarkers();

    // ---- 目標の一行（2026-10-07 ユーザー「任務目標を足す」）：門の状態で変わる。門までの距離付き ----
    {
        const wchar_t* obj = nullptr;
        if (!m_LabScene)
        {
            const Vector3* pp = PlayerPos();
            float dist = -1.0f;
            if (pp && m_Registry.IsValid(m_Stage.GetPortal()))
            {
                const Vector3 d = m_Stage.GetPortalCenter() - *pp;
                dist = std::sqrt(d.x * d.x + d.z * d.z);
            }
            if (m_Stage.IsCleared())
                obj = nullptr;
            else if (m_Stage.IsBossDefeated())
                swprintf_s(m_ObjectiveBuf, HasNextStage()
                    ? L"門に戻って 次のステージへ　%.0f m" : L"門に戻って 冒険を終える　%.0f m", (std::max)(0.0f, dist));
            else if (m_Stage.IsBossAlive())
                swprintf_s(m_ObjectiveBuf, L"ボスを倒す");
            else if (dist >= 0.0f)
                swprintf_s(m_ObjectiveBuf, L"ボスの門を見つけて ボスを呼ぶ　%.0f m", dist);
            else
                swprintf_s(m_ObjectiveBuf, L"ボスの門を見つけて ボスを呼ぶ");
            if (!m_Stage.IsCleared()) obj = m_ObjectiveBuf;
            if (m_Tutorial.Active() && m_Tutorial.Hint()) obj = m_Tutorial.Hint();   // チュートリアル中はその段のやる事
        }
        else
            obj = L"<{K:T}|{RB}>　トレーニングメニュー";   // 実験場：メニューの開き方（画面下の案内は体に重なるのでここ）
        m_GameUI.SetObjective(obj);
    }
    // 画面の真ん中の案内（押し寄せ・エリート・最終ウェーブ。2026-10-07。チュートリアルの段の見出しが優先）
    {
        float alpha = 0.0f, age = 0.0f;
        const wchar_t* ann = m_LabScene ? nullptr : m_Stage.Announce(alpha, age);
        float ta = 0.0f, tage = 0.0f;
        if (const wchar_t* t = m_Tutorial.Announce(ta, tage)) { ann = t; alpha = ta; age = tage; }
        m_GameUI.SetAnnounce(ann, alpha, age);
    }

    // ---- 死亡 → 倒れた姿を少し見せてからリザルトへ ----
    // 一時停止中でも進める（三択を開いたまま死ぬ事は無いが、止まると戻れない）
    // Boss を倒したら同じ流れで「ステージクリア」の幕 → リザルト（倒した後に死んでもクリア扱い）
    // 爆発の山場が 1 度も写っていなければ、終わった瞬間をリザルトの背景にする
    if ((m_Stage.IsCleared() || IsPlayerDead()) && !m_EndShotChecked && !m_LabScene)
    {
        m_EndShotChecked = true;
        if (m_Highlight.Best() <= 0)
        {
            m_SnapshotThisFrame = true;
            m_SnapshotIsHighlight = false;
        }
    }
    if (m_Stage.IsCleared())
    {
        m_DeathTimer += dt;
        m_GameUI.SetGameOver(m_DeathTimer, true);
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }
    else if (IsPlayerDead())
    {
        m_DeathTimer += dt;
        m_GameUI.SetGameOver(m_DeathTimer);   // 「力尽きた」の幕
        if (m_DeathTimer >= kDeathToResult) EndRun();
    }
    else
    {
        m_GameUI.SetGameOver(-1.0f);
    }

    PROFILE_SCOPE("Debug panels (ImGui)");
    DrawDebugUI();
}

// ============================================================
// HUD の画面外の目印（報酬の箱 = 黄、エリート = 赤）
// ============================================================
void CollisionTestScene::UpdateHudMarkers()
{
    std::vector<HUDMarker> markers;
    m_Registry.CreateView<InteractableComponent>()
        .Each([&](Entity, InteractableComponent& it)
            {
                // 箱は黄、Boss の門は紫、倒した後の出口は金（種類ごとの絵は HUDMarkerKind）
                if (it.kind == InteractKind::BossPortal)
                    markers.push_back({ it.basePos + Vector3(0.0f, 2.0f, 0.0f), { 0.75f, 0.35f, 1.0f, 1.0f }, HUDMarkerKind::Portal });
                else if (it.kind == InteractKind::StageExit)
                    markers.push_back({ it.basePos + Vector3(0.0f, 2.0f, 0.0f), { 1.0f, 0.85f, 0.40f, 1.0f }, HUDMarkerKind::Exit });
                else
                    markers.push_back({ it.basePos + Vector3(0.0f, 0.6f, 0.0f), { 1.0f, 0.78f, 0.35f, 1.0f }, HUDMarkerKind::Chest });
            });
    // 磁石（赤）
    for (const Vector3& p : m_Pickups.GetPositions())
        markers.push_back({ p, { 1.0f, 0.25f, 0.20f, 1.0f }, HUDMarkerKind::Magnet });
    // 面の Boss（GPU のリードバックの位置）
    if (m_Stage.IsBossAlive())
        markers.push_back({ m_Stage.BossPos() + Vector3(0.0f, 2.0f, 0.0f), { 0.85f, 0.25f, 1.0f, 1.0f }, HUDMarkerKind::Boss });
    // 押し寄せが来る方向（赤橙。プレイヤーから 30m 先。2026-10-07）
    if (const Vector3* pp = PlayerPos(); pp && m_Stage.SurgeActive(m_RunTime))
    {
        const float yaw = m_Stage.SurgeYaw();
        markers.push_back({ *pp + Vector3(std::cos(yaw) * 30.0f, 1.0f, std::sin(yaw) * 30.0f), { 1.0f, 0.35f, 0.12f, 1.0f }, HUDMarkerKind::Surge });
    }
    // 面の固定エリート（GPU のリードバックの位置。2026-10-25）
    {
        std::vector<Vector3> ep;
        m_StageElites.GetPositions(ep);
        for (const Vector3& p : ep)
            markers.push_back({ p + Vector3(0.0f, 2.0f, 0.0f), { 1.0f, 0.30f, 0.30f, 1.0f }, HUDMarkerKind::Elite });
    }
    // 計測用の的（無敵）も EliteTag を持つので外す
    m_Registry.CreateView<EliteTag, TransformComponent, HealthComponent>()
        .Each([&](Entity, EliteTag&, TransformComponent& tf, HealthComponent& hp)
            {
                if (hp.invincible || hp.current <= 0.0f) return;
                markers.push_back({ tf.position + Vector3(0.0f, 1.0f, 0.0f), { 1.0f, 0.30f, 0.30f, 1.0f }, HUDMarkerKind::Elite });
            });
    auto& cam = m_Camera.Camera();   // GetViewMatrix は const ではない
    m_GameUI.SetMarkers(cam.GetViewMatrix() * cam.GetProjectionMatrix(), std::move(markers));
}

// ============================================================
// プレイ終了 → リザルトへ
// 戦績を g_LastRun に写してから切替を依頼する。2 回目以降は何もしない
// ============================================================
void CollisionTestScene::EndRun()
{
    if (m_RunEnded) return;
    m_RunEnded = true;

    g_LastRun.valid = true;
    g_LastRun.survivedSec = m_RunTime;   // バックパック・三択で止めていた時間は含めない（HUD の表示と同じ）
    g_LastRun.level = (m_Registry.IsValid(m_Player) && m_Registry.Has<LevelComponent>(m_Player))
        ? m_Registry.Get<LevelComponent>(m_Player).level : 1;
    g_LastRun.kills = m_Swarm.GetCounters().killCount;   // リードバックなので 1〜2 フレーム古い。許容
    g_LastRun.expGained = m_ExpGained;
    g_LastRun.cleared = m_Stage.IsCleared();
    g_LastRun.stage = m_StageIndex;
    g_LastRun.startStage = m_RunStart;
    g_LastRun.tier = m_RunTier;
    g_LastRun.hasNextStage = g_LastRun.cleared && HasNextStage();
    // 進み具合：Tier を最後まで戦い抜いた → 次の Tier が開く（2026-10-21、ステージ選択）
    if (g_LastRun.cleared && !g_LastRun.hasNextStage && !m_LabScene)
    {
        GameProgress::Get().OnTierCleared(m_RunStart, m_RunTier);
        GameProgress::Get().Save();
    }
    g_LastRun.cratesOpened = m_Crates.Opened();
    g_LastRun.goldEarned = m_Registry.Has<WalletComponent>(m_Player)
        ? (int)(m_Registry.Get<WalletComponent>(m_Player).earned - m_GoldAtStart) : 0;
    g_LastRun.backpack = m_Registry.Has<BackpackComponent>(m_Player)
        ? m_Registry.Get<BackpackComponent>(m_Player) : BackpackComponent{};

    // ---- 次の面への引き継ぎ（クリアした時だけ。力尽きたら最初から）。計画（開始マップ・Tier）はリザルトが見るので残す ----
    g_RunCarry.Reset();
    g_RunCarry.startStage = m_RunStart;
    g_RunCarry.tier = m_RunTier;
    if (g_LastRun.hasNextStage && m_Registry.IsValid(m_Player))
    {
        g_RunCarry.stage = m_StageIndex + 1;
        g_RunCarry.hasPlayer = true;
        if (m_Registry.Has<BackpackComponent>(m_Player))    g_RunCarry.backpack = m_Registry.Get<BackpackComponent>(m_Player);
        if (m_Registry.Has<SpellbookComponent>(m_Player))   g_RunCarry.spellbook = m_Registry.Get<SpellbookComponent>(m_Player);
        if (m_Registry.Has<LevelComponent>(m_Player))       g_RunCarry.level = m_Registry.Get<LevelComponent>(m_Player);
        if (m_Registry.Has<WalletComponent>(m_Player))      g_RunCarry.wallet = m_Registry.Get<WalletComponent>(m_Player);
        if (m_Registry.Has<ManaComponent>(m_Player))        g_RunCarry.mana = m_Registry.Get<ManaComponent>(m_Player);
        if (m_Registry.Has<HealthComponent>(m_Player))      g_RunCarry.health = m_Registry.Get<HealthComponent>(m_Player);
        if (m_Registry.Has<ShieldComponent>(m_Player))      g_RunCarry.shield = m_Registry.Get<ShieldComponent>(m_Player);
        if (m_Registry.Has<PlayerStatsComponent>(m_Player)) g_RunCarry.stats = m_Registry.Get<PlayerStatsComponent>(m_Player);
        g_RunCarry.killsBefore = g_LastRun.kills;
    }

    Application::Get().GetGame().GetSceneManager().RequestChangeScene(SceneType::RESULT);
    std::cout << "[CollisionTestScene] run ended: " << (int)m_RunTime << "s, kills " << g_LastRun.kills << std::endl;
}
