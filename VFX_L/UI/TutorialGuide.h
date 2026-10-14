// ============================================================
// TutorialGuide.h
// 初回のチュートリアル（2026-10-13 ユーザー：「新手引導を作って、ゲームの機能を紹介して」→ 推奨の
//   「最初の 1 戦の中で段階的に案内」+「いつでも読める遊び方のページ（HowToPlayUI）」）。
//   普段の戦闘の上に案内を重ねるだけで、ゲームは止めない（バックパックの段はバックパックを開けば元々止まる）。
//   各段 = 画面の真ん中の案内（HUD の announce。「まずは動いてみよう」）+ 目標の行（HUD の objective。やる事と操作）。
//   条件を満たすと次の段へ。最後の 2 段（宝箱・ボスの門）は説明だけで数秒で進む。終わると「チュートリアル完了！」。
//   始めるのはタイトルから第 1 ステージに入った最初の 1 回だけ（GameSettings::tutorialDone。VFXL_TUTORIAL=1 / 0 で強制）。
//   一時停止のメニューの「チュートリアルを飛ばす」でいつでも終われる（Skip）。
//   シーンが毎フレーム Input を埋めて Update を呼び、Hint / Announce を HUD へ渡す。
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <cstdint>

class TutorialGuide
{
public:
    // シーンが毎フレーム埋める（UI が開いて止まっている間も）
    struct Input
    {
        DirectX::SimpleMath::Vector3 playerPos;
        bool  hasPlayer = false;
        bool  inAir = false;          // ジャンプ中（MoveStateID::Jump）
        bool  sliding = false;        // PlayerStateComponent::slideActive
        uint32_t kills = 0;           // 累計（SwarmCounters::killCount）
        int   level = 1;
        bool  backpackOpen = false;   // GameUI::IsBackpackOpen
        bool  surgeActive = false;    // ManaComponent::surgeTime > 0
        bool  choosingCard = false;   // 四択を開いている
    };

    enum class Step { Move, JumpSlide, AutoAttack, LevelUp, Backpack, Surge, Crates, Boss, Done, Count };

    void Start();
    void Skip();                       // 一時停止のメニューから。完了扱いにする
    void Update(float dt, const Input& in);

    bool Active() const { return m_Active; }
    bool JustFinished() const { return m_JustFinished; }   // この Update で終わった（シーンが設定を保存する）
    Step CurrentStep() const { return m_Step; }

    // HUD の目標の行（「目標: 」は HUD が付ける）。無ければ null
    const wchar_t* Hint() const;
    // 画面の真ん中の案内。出ていなければ null（alpha / age は HUDAnnounce の出入り用）
    const wchar_t* Announce(float& alpha, float& age) const;

private:
    void Enter(Step s);
    void Say(const wchar_t* text, float seconds);

    bool  m_Active = false;
    bool  m_JustFinished = false;
    Step  m_Step = Step::Move;
    float m_StepTime = 0.0f;          // 今の段に入ってからの秒

    // 段ごとの進み具合
    bool  m_HavePrev = false;
    DirectX::SimpleMath::Vector3 m_PrevPos;
    float m_Moved = 0.0f;
    bool  m_Jumped = false, m_Slid = false, m_WasInAir = false;
    uint32_t m_KillsAtStart = 0;
    bool  m_BackpackSeen = false;
    bool  m_LevelUpSeen = false;

    // 真ん中の案内
    const wchar_t* m_SayText = nullptr;
    float m_SayLeft = 0.0f;
    float m_SayAge = 0.0f;
    float m_DoneLeft = 0.0f;          // 「完了！」を見せてから Active を下ろすまで
};
