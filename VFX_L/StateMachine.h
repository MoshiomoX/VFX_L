// ============================================================
// StateMachine.h
// 汎用階層ステートマシン（HSM / データロジック分離型）
//
// 設計方針：
//   - StateMachine 自身は業務型に依存しない純ロジック（ECS 流用前提）
//   - データ(TContext) と 業務(TOwner) を毎フレーム外部から渡す
//   - フラットも階層も同一実装で扱える（parent を ROOT にすればフラット）
//   - 遷移は Hybrid：Event(キュー型) を先に消化 → Polling(委譲チェーン)
//   - 親の Update は2系統：
//       onUpdateAlways … 毎フレーム必ず実行（共通処理：重力など）
//       onUpdate       … 子が遷移要求しなかった時だけ実行（遷移判断の兜底）
// ============================================================
#pragma once
#include <optional>
#include <unordered_map>
#include <vector>
#include <queue>
#include <functional>
#include <algorithm>

// StateID : enum class を想定（末尾に ROOT 番兵を置く運用）
// TOwner  : 業務データ型（VFXEffect / Player 等）
// TContext: 状態機データ型（current と timeInState を持つ構造体）
template<typename StateID, typename TOwner, typename TContext>
class StateMachine {
public:
    // ------------------------------------------------------------
    // 状態の振る舞い定義
    // Enter/Update/Exit を関数ポインタで保持する
    // ------------------------------------------------------------
    struct Behavior
    {
        StateID parent;                                                         // 親状態（ROOT で最上位）
        void (*onEnter)(TContext&, TOwner&) = nullptr;  // 状態突入時
        void (*onUpdateAlways)(TContext&, TOwner&, float) = nullptr;  // 毎フレーム実行（常時処理）
        std::optional<StateID>(*onUpdate)(TContext&, TOwner&, float) = nullptr;  // 委譲時のみ実行（遷移判断）
        void (*onExit)(TContext&, TOwner&) = nullptr;  // 状態離脱時
    };

    // 状態切替通知（from → to）。外部ロジックへ解耦して伝える。
    using ChangeCallback = std::function<void(StateID from, StateID to)>;

    // ------------------------------------------------------------
    // 設定系
    // ------------------------------------------------------------

    // ROOT 番兵を指定（親チェーンの終端判定に使う）
    void SetRoot(StateID root)
    {
        m_Root = root;
        m_HasRoot = true;
    }

    // 状態を登録
    void RegisterState(StateID id, const Behavior& behavior)
    {
        m_Behaviors[id] = behavior;
    }

    // 状態切替通知の購読を設定
    void SetOnStateChanged(ChangeCallback cb)
    {
        m_OnChanged = std::move(cb);
    }

    // ------------------------------------------------------------
    // 起動
    // ------------------------------------------------------------

    // 初期状態へ突入（ROOT直下から initial まで親→子の順で Enter）
    void Start(StateID initial, TContext& ctx, TOwner& owner)
    {
        ctx.current = initial;
        ctx.timeInState = 0.0f;
        auto path = GetPathFromRoot(initial);  // [最上位 ... initial]
        for (StateID id : path)
        {
            if (auto* b = Find(id); b && b->onEnter) b->onEnter(ctx, owner);
        }
    }

    // ------------------------------------------------------------
    // 毎フレーム駆動（Hybrid）
    // ------------------------------------------------------------
    void Update(TContext& ctx, TOwner& owner, float dt)
    {
        // --- 1) Event を先に消化（キュー型：実行中の不整合を回避）---
        while (!m_EventQueue.empty())
        {
            StateID requested = m_EventQueue.front();
            m_EventQueue.pop();
            DoChange(ctx, owner, requested);
        }

        // --- 2) この状態に入ってからの経過時間を加算 ---
        ctx.timeInState += dt;

        // --- 3) onUpdateAlways：親→子 の順で常時処理を実行 ---
        //        例：親の重力を先に、子の固有移動を後に適用する
        {
            auto path = GetPathToRoot(ctx.current);  // [current ... 最上位]
            for (int i = (int)path.size() - 1; i >= 0; --i)
            {
                // 逆順で走査 = 親→子 の実行順を保証
                if (auto* b = Find(path[i]); b && b->onUpdateAlways)
                {
                    b->onUpdateAlways(ctx, owner, dt);
                }
            }
        }

        // --- 4) onUpdate：子→親 の委譲チェーン（遷移判断）---
        //        子が遷移要求(値あり)を返したら即遷移して終了。
        //        nullopt なら親へ委譲。最上位まで行き着いたら終了。
        StateID s = ctx.current;
        while (true)
        {
            auto* b = Find(s);
            if (!b) break;
            if (b->onUpdate)
            {
                if (auto next = b->onUpdate(ctx, owner, dt); next.has_value())
                {
                    DoChange(ctx, owner, *next);
                    return;
                }
            }
            if (b->parent == s || IsRoot(b->parent)) break;  // 最上位到達
            s = b->parent;                                    // 親へ委譲
        }
    }

    // ------------------------------------------------------------
    // 遷移トリガ
    // ------------------------------------------------------------

    // Event 駆動：外部からの遷移要求をキューに積む（次の Update で処理）
    void SendEvent(StateID requested)
    {
        m_EventQueue.push(requested);
    }

    // 外部からの強制遷移（即時実行）
    void ChangeState(TContext& ctx, TOwner& owner, StateID next)
    {
        DoChange(ctx, owner, next);
    }

    // 現在状態の取得（読み取り専用）
    StateID GetCurrent(const TContext& ctx) const
    {
        return ctx.current;
    }

private:
    // ------------------------------------------------------------
    // 内部ヘルパー
    // ------------------------------------------------------------

    // 状態IDに対応する Behavior を検索（見つからなければ nullptr）
    const Behavior* Find(StateID id) const
    {
        auto it = m_Behaviors.find(id);
        return (it != m_Behaviors.end()) ? &it->second : nullptr;
    }

    // ROOT 番兵かどうかの判定
    bool IsRoot(StateID id) const
    {
        return m_HasRoot && id == m_Root;
    }

    // ある状態から最上位までの経路を取得 [自身, 親, 祖父, ...]
    std::vector<StateID> GetPathToRoot(StateID id) const
    {
        std::vector<StateID> path;
        StateID s = id;
        while (!IsRoot(s))
        {
            path.push_back(s);
            auto* b = Find(s);
            if (!b || b->parent == s) break;  // 親なし or 自己ループ防止
            s = b->parent;
        }
        return path;  // [自身 ... 最上位状態]
    }

    // 最上位→自身 の順で経路を取得（Enter 用に反転）
    std::vector<StateID> GetPathFromRoot(StateID id) const
    {
        auto path = GetPathToRoot(id);
        std::reverse(path.begin(), path.end());
        return path;  // [最上位状態 ... 自身]
    }

    // ------------------------------------------------------------
    // HSM の肝：LCA(最近共通祖先)を境に Exit/Enter を行う
    // ------------------------------------------------------------
    void DoChange(TContext& ctx, TOwner& owner, StateID next)
    {
        if (next == ctx.current) return;  // 同一状態への遷移は無視

        StateID from = ctx.current;

        // 両方とも ROOT→状態 の方向で経路を取得（方向統一で比較を簡潔に）
        auto fromPath = GetPathFromRoot(from);  // [最上位 ... from]
        auto toPath = GetPathFromRoot(next);  // [最上位 ... next]

        // --- LCA 計算：根から辿り、最後に一致した深さが LCA ---
        size_t minLen = std::min(fromPath.size(), toPath.size());
        size_t lcaDepth = 0;
        for (size_t i = 0; i < minLen; ++i)
        {
            if (fromPath[i] == toPath[i])
            {
                lcaDepth = i;
            }
            else
            {
                break;
            }
        }

        // --- Exit：現在状態から LCA の下一層まで退出（子→親）---
        for (int i = (int)fromPath.size() - 1; i > (int)lcaDepth; --i)
        {
            if (auto* b = Find(fromPath[i]); b && b->onExit)
            {
                b->onExit(ctx, owner);
            }
        }

        // --- 状態切替 + 通知 ---
        ctx.current = next;
        ctx.timeInState = 0.0f;
        if (m_OnChanged)
        {
            m_OnChanged(from, next);
        }

        // --- Enter：LCA の下一層から目標状態まで突入（親→子）---
        for (size_t i = lcaDepth + 1; i < toPath.size(); ++i)
        {
            if (auto* b = Find(toPath[i]); b && b->onEnter)
            {
                b->onEnter(ctx, owner);
            }
        }
    }

    // ------------------------------------------------------------
    // メンバ
    // ------------------------------------------------------------
    std::unordered_map<StateID, Behavior> m_Behaviors;   // 状態テーブル
    std::queue<StateID>                   m_EventQueue;  // Event キュー
    ChangeCallback                        m_OnChanged;   // 切替通知コールバック
    StateID                               m_Root{};      // ROOT 番兵
    bool                                  m_HasRoot = false;
};