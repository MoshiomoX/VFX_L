// ============================================================
// View.h
// 複数 Component を全部持つ Entity を走査するオブジェクト
//
// 2つの走査方法を提供：
//   Each()     : 高速版。参照で走査。走査中の Add/Remove/Destroy 禁止。
//   EachSafe() : 安全版。コピーで走査。走査中の増減OK（その分やや遅い）。
//
// 使い方：
//   auto view = registry.CreateView<Position, Velocity>();
//   view.Each([](Entity e, Position& p, Velocity& v) { ... });
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "ECS/SparseSet.h"
#include <tuple>

class Registry;  // 前方宣言（実装で完全定義が要るので末尾で include）

template<typename... Components>
class View
{
public:
    explicit View(Registry& registry)
        : m_Registry(registry)
    {
        static_assert(sizeof...(Components) > 0, "View needs at least one component");
    }

    // ========================================================
    // 高速版：参照で走査
    // 【重要】このループ中に Destroy / Add / Remove を呼んではいけない。
    //         基準 pool の配列が変化してループが壊れるため。
    //         走査中に増減が必要なら EachSafe を使うこと。
    // ========================================================
    template<typename Func>
    void Each(Func func);

    // ========================================================
    // 安全版：Entity 一覧をコピーして走査
    // 走査中に Destroy / Add / Remove を呼んでも安全。
    // コピーのコストがある分、高速版よりやや遅い。
    // ========================================================
    template<typename Func>
    void EachSafe(Func func);

    // Each と同じだが、Base の pool を回す（2026-10-03）。Each は先頭の型の pool を全部回すので、
    // CreateView<TransformComponent, X>() だと Transform を持つ全実体（装飾物込みで 1500 個ほど）を毎回見る。
    // X が少ない時は EachFrom<X> で X の数だけ回す（Debug で 1 実体 0.2µs ほど掛かる）。
    // func の中で Base を外したり実体を消したりしないこと（回している pool が詰まる）。順番は Base の pool の順
    template<typename Base, typename Func>
    void EachFrom(Func func);

private:
    Registry& m_Registry;
};

// ============================================================
// 実装（Registry の完全定義が必要なのでここで include）
// ============================================================
#include "ECS/Registry.h"

// --- 高速版：参照走査 ---
template<typename... Components>
template<typename Func>
void View<Components...>::Each(Func func)
{
    // 基準 pool = 最初の Component 型の pool
    using First = std::tuple_element_t<0, std::tuple<Components...>>;
    auto& basePool = m_Registry.GetPool<First>();

    // 参照で受ける（コピーしない＝高速）
    const auto& entities = basePool.GetEntities();

    // entities は basePool 内部への参照。
    // ループ中に構造を変えると壊れるので、func 内での増減は禁止。
    for (size_t i = 0; i < entities.size(); ++i)
    {
        Entity e = entities[i];

        // 全 Component を持っているか（畳み込み式: Contains<C1> && Contains<C2> && ...）
        if ((m_Registry.GetPool<Components>().Contains(e) && ...))
        {
            // 全 Component の参照を展開して func に渡す
            func(e, m_Registry.GetPool<Components>().Get(e)...);
        }
    }
}

template<typename... Components>
template<typename Base, typename Func>
void View<Components...>::EachFrom(Func func)
{
    const auto& entities = m_Registry.GetPool<Base>().GetEntities();
    for (size_t i = 0; i < entities.size(); ++i)
    {
        Entity e = entities[i];
        if ((m_Registry.GetPool<Components>().Contains(e) && ...))
            func(e, m_Registry.GetPool<Components>().Get(e)...);
    }
}

// --- 安全版：コピー走査 ---
template<typename... Components>
template<typename Func>
void View<Components...>::EachSafe(Func func)
{
    using First = std::tuple_element_t<0, std::tuple<Components...>>;

    // Entity 一覧をコピー（スナップショット）。
    // 以降 func 内で Destroy/Add/Remove が起きても、このコピーは変わらない。
    auto entities = m_Registry.GetPool<First>().GetEntities();

    for (Entity e : entities)
    {
        // コピーなので、走査中に破棄された Entity が残っている可能性がある
        // → IsValid で弾く（ここでは IsValid が意味を持つ）
        if (!m_Registry.IsValid(e)) continue;

        // 破棄されてなくても、Component が外された可能性があるので Contains で再確認
        if ((m_Registry.GetPool<Components>().Contains(e) && ...))
        {
            func(e, m_Registry.GetPool<Components>().Get(e)...);
        }
    }
}