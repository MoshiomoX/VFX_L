// ============================================================
// CollisionTestSceneGameplay.cpp
// CollisionTestScene::UpdateGameplay：System の実行順（2026-10-25 に CollisionTestScene.cpp から分けた。中身はそのまま）。
// バックパック・三択・一時停止の間は呼ばれない（Update が GameUI::ShouldPauseGame を見る）
// ============================================================
#include "Scene/CollisionTestScene.h"
#include "Component/TransformComponent.h"
#include "Component/ColliderComponent.h"
#include "Component/RigidbodyComponent.h"
#include "Component/HealthComponent.h"
#include "Component/InteractableComponent.h"
#include "Player/PlayerStatsComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Player/LevelComponent.h"
#include "Player/WalletComponent.h"
#include "VFX_Editor/VFXId.h"
#include "Debug/DebugManager.h"
#include "Audio/AudioSystem.h"
#include "Debug/FrameProfiler.h"
#include "Manager/InputMap.h"
#include "Core/Application.h"
#include "Core/DevUI.h"

// ============================================================
// 実際の gameplay 更新
// ============================================================
void CollisionTestScene::UpdateGameplay(float dt)
{
    if (m_AutoTest) UpdateAutoTest(dt);   // TEMP-TEST

    // ---- 遊んでいる時間（死んだら止める）----
    if (!IsPlayerDead())
        m_RunTime += dt;

    // ---- 負荷テスト（自動補充と小分けの生成）----
    m_Stress.Update(dt, m_Registry, m_Player, m_Swarm);
    m_SpellLab.Update(m_Registry, m_Player, m_Swarm, m_Mobs, m_Grid);   // 実験モードの間：MP 無限・経験値 0・群れの残り

    // ============================================================
    // System の実行順（固定）
    // 操作 → 湧き依頼 → 衝突 → 物理 → ステートマシン → 杖 → アニメ → マナリザルト → レベル判定
    //      → 箱 → 反応のエフェクト → カメラ → GPU gameplay Flush → 粒子 Flush
    //
    // ※雑魚の AI は GPU（SwarmEnemyAICS）。CPU には無い。
    //
    // ※ステートマシンは物理の後。isGrounded / velocity が
    //   今フレームの最終値になっている必要があるため。
    //   物理より前に置くと接地判定が1フレーム古くなり、
    //   着地の見た目がずれる。
    //
    // ※能力値の注入（SetMoveSpeed 等）は行わない。
    //   PlayerControlSystem が PlayerStatsComponent を直接読む。
    // ============================================================
    {
        PROFILE_SCOPE("Player control + spawn");
        m_PlayerControlSystem.Update(m_Registry, dt, GetCamera());

        // ---- 湧き管理（雑魚は GPU。数えるのは GPU の生存数）----
        if (const Vector3* pp = PlayerPos())
        {
            m_Mobs.Update(m_Grid, *pp, dt, m_RunTime, m_Swarm);
            m_Stage.Update(m_Grid, *pp, m_RunTime, dt, m_Mobs, m_Swarm);   // 時間で起きる出来事（エリート・最終ウェーブ・Boss）
            UpdateBossAttacks(dt, *pp);                                    // Boss の技（CollisionTestSceneBoss.cpp）
            UpdateRuinsFoes(dt, *pp);                                      // 遺跡の敵の出来事・鎖（CollisionTestSceneRuins.cpp。2026-10-25）
            UpdateElites(dt, *pp);                                         // 面の固定エリートの技・宝箱（CollisionTestSceneElites.cpp。2026-10-25。Boss の輪の後）
        }
    }
    {
        PROFILE_SCOPE("Collision (broadphase)");
        m_CollisionSystem.Update(m_Registry);
    }
    {
        PROFILE_SCOPE("Physics");
        m_PhysicsSystem.SetGravity(m_Gravity);
        m_PhysicsSystem.Update(m_Registry, dt, m_CollisionSystem);
    }

    static const char* const kRestSection = "State / weapon / anim / crates";   // GPU の Flush の前まで
    FrameProfiler::Get().Begin(kRestSection);
    m_PlayerStateSystem.Update(m_Registry, dt);

    m_WeaponSystem.Update(m_Registry, dt, m_CollisionSystem);

    // ---- 見た目のアニメ（杖の後: 「今フレーム撃った」を拾うため）----
    m_PlayerAnimSystem.Update(m_Registry, dt);
    m_SkinnedAnimSystem.Update(m_Registry, dt);
    m_ClothChainSystem.Update(m_Registry, dt);   // マント（今フレームのポーズで揺らす）

    // 草の風と踏み跡（物理の後の位置・接地で）
    if (const Vector3* pp = PlayerPos())
    {
        const bool grounded = m_Registry.Has<RigidbodyComponent>(m_Player) && m_Registry.Get<RigidbodyComponent>(m_Player).isGrounded;
        const bool sliding = m_Registry.Has<PlayerStateComponent>(m_Player) && m_Registry.Get<PlayerStateComponent>(m_Player).slideActive;
        m_Grass.Update(dt, *pp, grounded, sliding);
    }
    m_GrassFire.Tick(dt);   // 草の火の時計（一時停止中は進まない）
    m_Debris.Tick(dt);      // 落ち葉も
    UpdateWindToys(dt);     // 転がり草・タンポポ（2026-10-22）

    m_ManaSystem.Update(m_Registry, dt);

    // 投射物の生成・移動・命中・撃破報酬は全部 GPU（SwarmSystem）。
    // CPU 側にはもう無い。エリートの弾を CPU に戻す時はここに書く

    m_Stress.RecordEmitterStats(m_ParticleSystem.GetPendingEmitterCount(),
        m_ParticleSystem.GetDroppedEmitterCount());

    // ============================================================
    // レベルアップの判定（候補の抽選まで）
    // ============================================================
    m_LevelUpSystem.Update(m_Registry);

    // ============================================================
    // 近くの物を使う（報酬の箱: レベルアップと同じ三択。レベルは上がらない）
    // レベル判定の後に置く: 同じフレームでレベルアップが三択を出していたら、
    // 箱の三択は出せない（OfferChoices が false）→ 箱は消さずに残す
    // ============================================================
    {
        const bool blocked = DebugManager::Get().IsUsingDebugCamera()
            || DevUI::WantKeyboard();
        const bool pressed = !blocked && !IsPlayerDead() && (InputMap::GetInteractTrigger() || m_AutoInteract);
        m_AutoInteract = false;   // TEMP-TEST

        const Entity used = m_Interaction.Update(m_Registry, m_Player, dt, pressed);
        Vector3 openedPos;
        if (m_Crates.TryOpen(m_Registry, used, m_Player, m_LevelUpSystem, m_Interaction, openedPos))
        {
            m_Feedback.OnCrateOpened(openedPos);
            AudioSystem::Get().Play("gold_spend");   // 金貨を払った
        }
        else if (m_Crates.ConsumeDenied())
            AudioSystem::Get().Play("gold_deny");    // 金貨が足りない（鍵が開かない音）
        else if (const Vector3* pp = PlayerPos())
            m_Stage.TryUsePortal(m_Registry, used, m_Grid, *pp, m_Mobs, m_Swarm, m_Interaction);
        UpdatePropBlocks();   // 開けた箱の下のマスを雑魚に戻す

        // 門が使われた（Boss を呼んだ。パネルの「Summon Boss Now」も含めて、門から使える印が消えた）ら渦を止める
        const Entity portal = m_Stage.GetPortal();
        if (m_PortalVfx && (!m_Registry.IsValid(portal) || !m_Registry.Has<InteractableComponent>(portal)))
        {
            m_AreaVFX.StopInstance(m_PortalVfx);
            m_PortalVfx = 0;
        }
        // Boss を倒したら同じ門を出口にする（2026-10-07 ユーザー：もう一度押して次の面へ）。渦は金色で出し直す
        if (m_Stage.IsBossDefeated() && !m_Stage.IsCleared() && !m_Stage.IsExitOpen())
        {
            const wchar_t* prompt = HasNextStage() ? L"<{K:F}|{B}>  次のステージへ" : L"<{K:F}|{B}>  冒険を終える";   // 計画（Tier）の最後の面なら終える
            if (m_Stage.OpenExit(m_Registry, prompt))
            {
                if (m_PortalVfx) m_AreaVFX.StopInstance(m_PortalVfx);
                m_PortalVfx = m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::BossPortal), m_Stage.GetPortalCenter(), 1.0e9f, false, m_VFXContext,
                    Vector3(1.0f, 0.78f, 0.40f));
                if (m_PortalVfx) m_AreaVFX.RotateInstance(m_PortalVfx, m_Stage.GetPortalYaw());
            }
        }

        // 磁石（触れたら場の経験値オーブを全部吸い寄せる）
        if (const Vector3* pp = PlayerPos(); pp && !IsPlayerDead())
            m_Pickups.Update(m_Registry, m_Grid, *pp, dt, m_RunTime, m_Swarm);
    }

    // ---- 被弾とレベルアップの反応（揺れ・斬撃・レベルアップの光）----
    const float hpLost = TrackPlayerHpLoss();
    // ダメージの数字（2026-10-17）：敵の分は GPU のリードバック、プレイヤーの分は hpLost
    {
        auto& dn = m_GameUI.Damage();
        dn.Update(dt);
        std::vector<Swarm::DamageText> texts;
        m_Swarm.ConsumeDamageTexts(texts);
        for (const auto& d : texts)
        {
            const float amount = (float)(d.packed & Swarm::kDtxAmountMask) * 0.1f;
            if (d.packed & Swarm::kDtxKill)   // 死体の場所（死霊術師が骸骨を起こす。2026-10-25）
                m_Mobs.NoteKill(Vector3(d.position.x, m_Grid.SampleHeight(d.position.x, d.position.z), d.position.z));
            const DamageNumbers::Kind kind = (d.packed & Swarm::kDtxKill) ? DamageNumbers::Kind::Kill
                : (d.packed & Swarm::kDtxPoison) ? DamageNumbers::Kind::Poison : DamageNumbers::Kind::Normal;
            dn.Add(d.position, amount, kind, (int)((d.packed >> Swarm::kDtxElementShift) & 0xFu));
        }
        if (hpLost > 0.0f && m_Registry.Has<TransformComponent>(m_Player))
            dn.Add(m_Registry.Get<TransformComponent>(m_Player).position + Vector3(0.0f, 1.2f, 0.0f), hpLost, DamageNumbers::Kind::Player);
    }
    // ---- 吸血のルーン（2026-10-25）：印の付いた敵が失った HP の 3% を、1 秒あたり最大 HP の 2% まで戻す ----
    {
        const float stolen = m_Swarm.ConsumeLifesteal();
        if (m_Registry.Has<HealthComponent>(m_Player))
        {
            auto& hp = m_Registry.Get<HealthComponent>(m_Player);
            const LifestealHeal::Result r = m_Lifesteal.Update(dt, stolen, hp, !IsPlayerDead());
            if (r.healed > 0.0f) m_PrevPlayerHp = hp.current;   // 戻した分を次のフレームの被弾に混ぜない
            if (r.showAmount > 0.0f && m_Registry.Has<TransformComponent>(m_Player))
            {
                const Vector3 p = m_Registry.Get<TransformComponent>(m_Player).position;
                m_GameUI.Damage().Add(p + Vector3(0.35f, 1.5f, 0.0f), r.showAmount, DamageNumbers::Kind::Heal);
                if (r.playFx) m_AreaVFX.Play(VFXDatabase::GetFile(VFXId::LifestealAbsorb), p, 1.0f, true, m_VFXContext);
                if (r.playFx) AudioSystem::Get().Play("lifesteal");
            }
        }
    }
    m_Camera.OnPlayerHit(hpLost);
    m_Feedback.Update(m_Registry, m_Player, dt, hpLost);
    m_PlayerVFX.Update(m_Registry, m_Player, dt, hpLost, m_WeaponSystem.GetCastFx());   // 詠唱の光・土煙・着地・尾・被弾の閃き
    for (const auto& g : m_WeaponSystem.GetGustFx())   // 突風：草が一斉に倒れ、落ち葉が舞う
    {
        m_Grass.AddGust(g.pos, g.radius, g.dir, g.length);
        m_Debris.AddGust(g.pos, g.radius, g.dir, g.length);
        AddGustWindBurst(g.pos, g.dir, g.length, g.radius);
    }
    m_SpellFx.Update(dt, m_WeaponSystem.GetTriggeredFx(),                                // アイススパイクの氷のトゲ（地面の高さに 1 本ずつ）
        [this](float x, float z) { return m_Grid.SampleHeight(x, z); });
    OnExplosions(m_Swarm.ConsumeShakeAreas());   // 爆発・光線だけ（命中の火花・土煙では揺らさない）
    // リザルトの背景：爆発の山場を写す（生きている間だけ。実験場では撮らない）
    if (!m_LabScene && !IsPlayerDead() && !m_Stage.IsCleared() && m_Highlight.ShouldCapture(m_RunTime))
    {
        m_SnapshotThisFrame = true;
        m_SnapshotIsHighlight = true;
    }

    // ---- カメラ追従（最後）----
    m_Camera.Update(dt, PlayerPos());
    FrameProfiler::Get().End(kRestSection);

    // ============================================================
    // GPU 側 gameplay の Flush（粒子と同じ位置、粒子より前）
    //
    // 中で固定ステップを回す。渡すのは実 dt でよい。
    // プレイヤーの位置はここまでで確定しているので、この時点の値を上げる。
    //
    // リードバックは 1〜2 フレーム古い。被弾は TryApplyHit へ流し、
    // 無敵時間の判定は向こうに任せる（Boss・エリートの技、鎖と同じ窓口）
    // ============================================================
    PushWind();   // 世界の風を草・マント・粒子へ、燃えている草の粒子の入力を GPU へ（Flush の前）
    if (m_Registry.IsValid(m_Player))
    {
        const auto& ptf = m_Registry.Get<TransformComponent>(m_Player);

        float playerRadius = 0.4f;
        if (m_Registry.Has<ColliderComponent>(m_Player))
            playerRadius = m_Registry.Get<ColliderComponent>(m_Player).radius;

        const bool playerAlive = !IsPlayerDead();

        // プレイヤーの体格は PlayerStats が持つ。接触判定用に毎フレーム GPU 側へ渡す
        if (m_Registry.Has<PlayerStatsComponent>(m_Player))
            m_Swarm.GetAIParams().playerCapsuleHalf =
            m_Registry.Get<PlayerStatsComponent>(m_Player).height * 0.5f;

        {
            PROFILE_SCOPE_GPU("Swarm Flush (GPU gameplay)");
            m_Swarm.Flush(ptf.position, playerRadius, playerAlive, dt, m_TotalTime);
        }

        // GPU 上で受けたダメージを CPU のプレイヤーへ反映
        const float gpuDamage = m_Swarm.ConsumePlayerDamage();
        if (gpuDamage > 0.0f)
            PlayerStateSystem::TryApplyHit(m_Registry, m_Player, gpuDamage);
        // ノックバック（打撃の向きは別のリードバック。ダメージと前後して届くことがある）。
        // 無敵中は下がらない。ただし無敵が始まったばかり（この打撃のダメージが先に届いた）なら下がる。
        // 同じフレームに爆発があれば爆発の方（遠く・少し浮く）
        const auto hits = m_Swarm.ConsumePlayerHits();
        if ((hits.melee > 0 || hits.blasts > 0 || hits.spikes > 0 || hits.winds > 0) && playerAlive && m_Registry.Has<PlayerStateComponent>(m_Player))
        {
            const auto& st = m_Registry.Get<PlayerStateComponent>(m_Player);
            const bool fresh = !st.IsInvincible() || st.invincibleTimer > st.invincibleAfterHit - 0.2f;
            if (fresh)
            {
                // 砂旋 > トゲ玉 > 爆発 > 殴り（2026-10-17）
                if (hits.winds > 0)       PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.windDir, PlayerControlSystem::KnockKind::Wind);
                else if (hits.spikes > 0) PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.spikeDir, PlayerControlSystem::KnockKind::Spike);
                else if (hits.blasts > 0) PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.blastDir, PlayerControlSystem::KnockKind::Blast);
                else                      PlayerControlSystem::ApplyKnockback(m_Registry, m_Player, hits.meleeDir, PlayerControlSystem::KnockKind::Melee);
            }
        }
        // GPU 上で拾った経験値を CPU のプレイヤーへ反映。
        // レベルアップの判定は LevelUpSystem（次フレーム頭）に任せて、ここは足すだけ
        const float gpuExp = m_Swarm.ConsumeExp() * m_ExpMul;   // Tier の経験値倍率（2026-10-21）。金貨も同じ値から
        if (gpuExp > 0.0f) m_ExpGained += gpuExp;   // 戦績用
        if (gpuExp > 0.0f) m_Feedback.OnExpPicked(ptf.position);   // 拾った時のきらめき
        if (gpuExp > 0.0f && m_Registry.Has<LevelComponent>(m_Player))
            m_Registry.Get<LevelComponent>(m_Player).experience += gpuExp;
        // 経験値オーブは金貨も兼ねる（2026-10-04、Brotato 風）
        if (gpuExp > 0.0f && m_Registry.Has<WalletComponent>(m_Player))
        {
            auto& wallet = m_Registry.Get<WalletComponent>(m_Player);
            wallet.Earn(gpuExp * wallet.goldPerExp);
        }
    }

    // ---- 死亡 → 燃焼消滅（HP が尽きた CPU 実体。消え終わったら MeshVFXSystem が破棄）----
    m_Elites.UpdateDeaths(m_Registry, m_MeshVFXSystem, m_VFXContext);
    m_MeshVFXSystem.Update(m_Registry, dt, m_VFXContext);

    // Mesh 発射の動作確認（Flush の前に積む）
    m_Stress.UpdateMeshEmitTest(dt, m_Registry, m_Player, m_ParticleSystem);

    // ============================================================
    // 粒子は1フレームに1回だけ Flush する。
    // Flush はコマンドを積むだけの処理なので、
    // 0 に近いままのはず。伸びるなら GPU を待っている。
    // ============================================================
    {
        PROFILE_SCOPE_GPU("Particles + lights");
        auto t0 = std::chrono::high_resolution_clock::now();

        // 天候：時刻と出来事で照明を上書き、雨 / 砂嵐の発射器をカメラへ付ける（AreaVFX の Update より前）
        if (CameraBase* cam = GetCamera())
            m_Weather.Update(dt, m_RunTime, cam->GetPosition(), cam->GetForward());

        // 範囲攻撃・反応のエフェクトの見た目（emitter を積むので粒子の Flush より前）
        const Vector3* pp = PlayerPos();
        m_AreaVFX.Update(dt, pp ? *pp : Vector3::Zero);

        m_ParticleSystem.Flush(dt, m_TotalTime);

        // 点光源: CPU 側の VFX（範囲・エリート）はもう積み終わっている。弾と範囲の分を GPU で追記
        m_Swarm.CollectLights();
        {   // TEMP-TEST crowd
            static int f = 0;
            if (f++ % 120 == 0) { Vector3 np, nv; float nd = 0; m_Swarm.GetNearestEnemy(np, nv, nd); std::cout << "[crowd] fps=" << Application::Get().GetTimer().GetFPS() << " alive=" << m_Swarm.GetCounters().aliveEnemies << " nearest=" << nd << " flushMs=" << m_Stress.GetFlushMsAvg() << std::endl; }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        m_Stress.RecordFlushMs(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    PROFILE_SCOPE("Gameplay debug draw");
    DrawGameplayDebug();
}
