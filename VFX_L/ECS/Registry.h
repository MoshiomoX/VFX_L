// ============================================================
// Registry.h
// ECS の司令塔。世界に1個。
//   - Entity の生成/破棄（ID 発行・回収、generation 管理）
//   - 各種 Component の SparseSet を束ねる
//   - Add / Remove / Get / Has / CreateView の API を提供
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "ECS/SparseSet.h"
#include <vector>
#include <memory>
#include <cstdint>

// 前方宣言（View は別ファイル、相互依存を避ける）
template<typename... Components>
class View;

class Registry
{
public:
    // ========================================================
    // 型ごとに一意な ComponentID（型を key にする仕組み）
    // Position→0, Velocity→1 ... のように型ごとに固定の番号を発行
    // ========================================================
private:
    static uint32_t NextComponentID()
    {
        static uint32_t id = 0;
        return id++;
    }

    template<typename T>
    static uint32_t ComponentID()
    {
        // 型 T ごとに独立した static → 型ごとに固定 ID
        static uint32_t id = NextComponentID();
        return id;
    }

public:
    // ========================================================
    // Entity の生成 / 破棄
    // ========================================================

    // 新しい Entity を作る（回収済み index を優先再利用）
    Entity Create()
    {
        if (!m_FreeIndices.empty())
        {
            uint32_t idx = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            // generation は前回 Destroy 時に +1 済み
            return EntityTraits::Make(idx, m_Generations[idx]);
        }

        uint32_t idx = static_cast<uint32_t>(m_Generations.size());
        m_Generations.push_back(0);
        return EntityTraits::Make(idx, 0);
    }

    // Entity を破棄（全 Component を削除、index 回収、generation を進める）
    void Destroy(Entity e)
    {
        if (!IsValid(e)) return;

        uint32_t idx = EntityTraits::GetIndex(e);

        // この Entity の全 Component を各 pool から削除
        for (auto& pool : m_Pools)
        {
            if (pool) pool->RemoveIfExists(e);
        }

        // generation を進める → 古い handle は以降 IsValid で弾かれる
        m_Generations[idx]++;
        m_FreeIndices.push_back(idx);
    }

    // この Entity ハンドルが今も有効か（generation 比較で過去の handle を弾く）
    bool IsValid(Entity e) const
    {
        uint32_t idx = EntityTraits::GetIndex(e);
        if (idx >= m_Generations.size()) return false;
        return m_Generations[idx] == EntityTraits::GetGeneration(e);
    }

    // ========================================================
    // Component の Add / Remove / Get / Has
    // ========================================================
    template<typename T>
    void Add(Entity e, const T& component)
    {
        GetPool<T>().Add(e, component);
    }

    template<typename T>
    void Remove(Entity e)
    {
        GetPool<T>().Remove(e);
    }

    template<typename T>
    T& Get(Entity e)
    {
        return GetPool<T>().Get(e);
    }

    template<typename T>
    bool Has(Entity e)
    {
        return GetPool<T>().Contains(e);
    }

    // ========================================================
    // View を作る
    // ========================================================
    template<typename... Components>
    View<Components...> CreateView()
    {
        return View<Components...>(*this);
    }

private:
    // 型 T の SparseSet を取り出す（無ければ作る）
    template<typename T>
    SparseSet<T>& GetPool()
    {
        uint32_t id = ComponentID<T>();

        if (id >= m_Pools.size())
        {
            m_Pools.resize(id + 1);
        }

        if (!m_Pools[id])
        {
            m_Pools[id] = std::make_unique<SparseSet<T>>();
        }

        return *static_cast<SparseSet<T>*>(m_Pools[id].get());
    }

    // View が GetPool / IsValid にアクセスできるように friend 宣言
    template<typename... Components>
    friend class View;

private:
    std::vector<std::unique_ptr<ISparseSet>> m_Pools;   // ComponentID を下标に格納
    std::vector<uint32_t> m_Generations;                // index ごとの現在世代
    std::vector<uint32_t> m_FreeIndices;                // 回収済み index（再利用待ち）
};