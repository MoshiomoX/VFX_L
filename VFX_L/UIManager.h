// ============================================================
// UIManager.h
// モーダル UI の排他をスタックで管理する。
//
// 規則は2つだけ：
//   1. 一番上の UI だけが入力を受け取る
//   2. スタックの順序は「開いた時間の順」で決まる
//
// スタックにする理由：
//   「閉じた後どこへ戻るか」を自分で書かなくて済む。
//   グリッドを開いた状態でレベルアップが割り込んでも、
//   選び終われば自然にグリッドへ戻る。
//
//   優先度の表は持たない。後から開いたものが上、それだけ。
//   「いつ開けるか」は各シーンが条件として書く。
//
// 管理するのはモーダルだけ：
//   HUD は常に描いて入力を取らないので入れない。
//   tooltip は各 UI から呼ばれる部品なので入れない。
//
// シングルトンにしない理由：
//   積まれた中身はシーン専有のもの。
//   全域に置くと切り替え時に手で空にする必要があり、
//   忘れると「タイトル画面が止まっている」という事故になる。
// ============================================================
#pragma once
#include <vector>

enum class UILayer
{
    None,
    Backpack,     // 呪文編成。Tab で開閉
    LevelUp,      // 習得の三択。選ぶまで閉じられない
    // 将来：PauseMenu / GameOver / Shop
};

class UIManager
{
public:
    // 閉じ方の種類
    // 現時点では強制はしていない。宣言だけ用意して、
    // 「勝手に閉じてはいけない」という意図を残す。
    enum class CloseMode
    {
        Free,     // 入力で閉じられる（グリッド、暫停メニュー）
        Forced,   // 処理が終わるまで閉じられない（三択、死亡画面）
    };

    void Push(UILayer layer, CloseMode mode = CloseMode::Free);
    void Pop(UILayer layer);
    void Toggle(UILayer layer, CloseMode mode = CloseMode::Free);
    void Clear();

    // 一番上か（入力を受け取ってよいか）
    bool CanReceiveInput(UILayer layer) const;

    // 積まれているか（描画してよいか）
    bool IsOpen(UILayer layer) const;

    // 何も積まれていないか
    bool IsEmpty() const { return m_Entries.empty(); }

    // 何か積まれているか（ゲームを止めるかの目安）
    bool ShouldPauseGame() const { return !m_Entries.empty(); }

    UILayer Top() const;

    // 描画は積まれた順に行う。後から積んだものが上に重なる。
    // 「三択が最前面」を別途書かなくて済む。
    const std::vector<UILayer>& GetStack() const { return m_Stack; }

    // デバッグ表示用
    static const char* LayerName(UILayer layer);

private:
    struct Entry
    {
        UILayer   layer;
        CloseMode mode;
    };

    std::vector<Entry>   m_Entries;
    std::vector<UILayer> m_Stack;   // 描画用に layer だけ抜いたもの
};