// ============================================================
// TutorialGuide.cpp
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/TutorialGuide.h"
#include "Audio/AudioSystem.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kSayTime = 2.8f;        // 段の始めの案内を出す秒
    constexpr float kInfoTime = 9.0f;       // 説明だけの段（宝箱・ボス）の長さ
    constexpr float kDoneShow = 3.0f;       // 「完了！」を見せる秒
    constexpr float kMoveNeed = 4.0f;       // 移動の段：これだけ歩いたら次へ（m）
    constexpr uint32_t kKillsNeed = 3;      // 自動攻撃の段：倒す数

    struct StepText { const wchar_t* say; const wchar_t* hint; };
    // 文字の中の <キーボード|パッド> と {K:W} {A} は HUD（InputGlyph）が今の機器の絵にする
    const StepText kTexts[] = {
        { L"まずは動いてみよう",       L"<{K:W} {K:A} {K:S} {K:D}|{LS}> で移動　　カメラは <マウス|右スティック>" },
        { L"ジャンプとスライディング", L"<{K:Space}|{A}> でジャンプ、<{K:Ctrl}|{X}> でスライディング（両方やってみよう）" },
        { L"魔法は自動で撃つ",         L"魔法は近くの敵を自動で狙う。敵を 3 体倒そう" },
        { L"経験玉を拾おう",           L"敵が落とす経験玉を拾ってレベルアップ。4 枚のカードから 1 枚を選ぶ" },
        { L"バックパックに置こう",     L"<{K:Tab}|{START}> でバックパックを開き、手に入れた物を枠に置く（ルーンは隣の魔法を強化）" },
        { L"魔力解放",                 L"<{K:Q}|{Y}> で魔力解放。3 秒間 MP を使わずに撃ち続ける" },
        { L"宝箱と金貨",               L"敵が落とす金貨で宝箱を開けられる。箱の前で <{K:F}|{B}>。値段は開けるたびに上がる" },
        { L"ボスの門",                 L"10 分で最終ウェーブ。その前にボスの門を見つけて呼び出し、倒して次のステージへ" },
        { L"チュートリアル完了！",     nullptr },
    };
    static_assert(sizeof(kTexts) / sizeof(kTexts[0]) == (size_t)TutorialGuide::Step::Count, "step text count");
}

void TutorialGuide::Start()
{
    m_Active = true;
    m_JustFinished = false;
    m_HavePrev = false;
    m_Moved = 0.0f;
    m_Jumped = m_Slid = m_WasInAir = false;
    m_BackpackSeen = false;
    m_DoneLeft = 0.0f;
    Enter(Step::Move);
}

void TutorialGuide::Skip()
{
    if (!m_Active) return;
    m_Step = Step::Done;
    m_SayText = nullptr;
    m_SayLeft = 0.0f;
    m_Active = false;
    m_JustFinished = true;
}

void TutorialGuide::Say(const wchar_t* text, float seconds)
{
    m_SayText = text;
    m_SayLeft = seconds;
    m_SayAge = 0.0f;
}

void TutorialGuide::Enter(Step s)
{
    m_Step = s;
    m_StepTime = 0.0f;
    Say(kTexts[(int)s].say, s == Step::Done ? kDoneShow : kSayTime);
    if (s == Step::Done)
    {
        m_DoneLeft = kDoneShow;
        AudioSystem::Get().Play("ui_select");
    }
    else
        AudioSystem::Get().Play("ui_hover");
}

void TutorialGuide::Update(float dt, const Input& in)
{
    m_JustFinished = false;
    if (!m_Active) return;
    m_StepTime += dt;
    if (m_SayLeft > 0.0f) { m_SayLeft -= dt; m_SayAge += dt; }

    // ---- 段ごとの記録 ----
    if (in.hasPlayer)
    {
        if (m_HavePrev)
        {
            const Vector3 d = in.playerPos - m_PrevPos;
            const float step = std::sqrt(d.x * d.x + d.z * d.z);
            if (step < 2.0f) m_Moved += step;   // 瞬間移動（自動テスト・やり直し）は数えない
        }
        m_PrevPos = in.playerPos;
        m_HavePrev = true;
    }
    if (in.inAir && !m_WasInAir) m_Jumped = true;
    m_WasInAir = in.inAir;
    if (in.sliding) m_Slid = true;
    if (in.backpackOpen) m_BackpackSeen = true;

    // ---- 条件 → 次の段 ----
    // 真ん中の案内が出ている間は進めない（「まずは動いてみよう」を読む前に終わらないように）
    const bool sayShown = m_SayLeft > 0.0f && m_StepTime < 1.0f;
    bool next = false;
    switch (m_Step)
    {
    case Step::Move:       next = m_Moved >= kMoveNeed; break;
    case Step::JumpSlide:  next = m_Jumped && m_Slid; break;
    case Step::AutoAttack: next = in.kills >= m_KillsAtStart + kKillsNeed; break;
    case Step::LevelUp:    next = in.level >= 2 && !in.choosingCard; break;   // カードを選び終えてから
    case Step::Backpack:   next = m_BackpackSeen && !in.backpackOpen; break;  // 開いて、閉じたら
    case Step::Surge:      next = in.surgeActive; break;
    case Step::Crates:     next = m_StepTime >= kInfoTime; break;
    case Step::Boss:       next = m_StepTime >= kInfoTime; break;
    case Step::Done:
        m_DoneLeft -= dt;
        if (m_DoneLeft <= 0.0f)
        {
            m_Active = false;
            m_JustFinished = true;
        }
        return;
    default: break;
    }
    if (!next || sayShown) return;

    // 次の段の記録を始める
    const Step n = (Step)((int)m_Step + 1);
    // 移動の段の間に跳んだ / 滑った分は数えない（この段に入ってから両方やる）
    if (n == Step::JumpSlide) m_Jumped = m_Slid = false;
    if (n == Step::AutoAttack) m_KillsAtStart = in.kills;
    if (n == Step::Backpack) m_BackpackSeen = in.backpackOpen;   // 四択の直後に開いていなければ false
    Enter(n);
}

const wchar_t* TutorialGuide::Hint() const
{
    if (!m_Active) return nullptr;
    return kTexts[(int)m_Step].hint;
}

const wchar_t* TutorialGuide::Announce(float& alpha, float& age) const
{
    alpha = 0.0f;
    age = 0.0f;
    if (!m_SayText || m_SayLeft <= 0.0f) return nullptr;
    // 出入りのフェード（StageDirector::Announce と同じ形：0.25 秒で出て、最後の 0.5 秒で消える）
    alpha = (std::min)(1.0f, (std::min)(m_SayAge / 0.25f, m_SayLeft / 0.5f));
    age = m_SayAge;
    return m_SayText;
}
