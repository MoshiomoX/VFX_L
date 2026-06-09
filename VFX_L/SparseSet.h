// ============================================================
// SparseSet.h
// ECS の中核データ構造（1種類の Component 専用の容器）
//
// 3つの配列で構成：
//   m_Sparse  : Entity番号(下标) → dense の位置(值) を記録する地図（穴あきOK）
//   m_Dense   : Component データ本体（連続、穴なし）
//   m_Entities: m_Dense と並走、各 dense データの持ち主 Entity（逆引き用）
// ============================================================
#pragma once
#include "Entity.h"
#include <vector>
#include <cassert>

// Registry が型を問わず SparseSet を束ねるための基底
// （型消去：SparseSet<Position> も SparseSet<Health> も ISparseSet* で持てる）
struct ISparseSet
{
    virtual ~ISparseSet() = default;
    // Entity 破棄時に、その Entity の Component を消すための共通インターフェース
    virtual void RemoveIfExists(Entity e) = 0;
};

template<typename T>
class SparseSet : public ISparseSet
{
public:
    // --- Component を追加（既にあれば上書き）---
    void Add(Entity e, const T& component)
    {
        uint32_t idx = EntityTraits::GetIndex(e);

        // sparse を必要な長さまで拡張（穴は NULL_ENTITY で埋める）
        if (idx >= m_Sparse.size())
        {
            m_Sparse.resize(idx + 1, EntityTraits::NULL_ENTITY);
        }

        // 既に持ってる → dense の該当位置を上書き
        if (Contains(e))
        {
            m_Dense[m_Sparse[idx]] = component;
            return;
        }

        // 新規：dense 末尾に詰める、3配列を同期して更新
        m_Sparse[idx] = static_cast<uint32_t>(m_Dense.size());
        m_Dense.push_back(component);
        m_Entities.push_back(e);
    }

    // --- Component を削除（swap-and-pop で穴を作らない）---
    void Remove(Entity e)
    {
        if (!Contains(e)) return;

        uint32_t idx = EntityTraits::GetIndex(e);
        uint32_t denseIdx = m_Sparse[idx];                          // 削除対象の dense 位置
        uint32_t lastIdx = static_cast<uint32_t>(m_Dense.size() - 1); // dense 末尾

        // 末尾要素を削除位置へ移動（穴埋め）
        m_Dense[denseIdx] = m_Dense[lastIdx];
        m_Entities[denseIdx] = m_Entities[lastIdx];

        // 移動してきた末尾要素の sparse を新しい位置に更新（忘れるとバグる箇所）
        uint32_t movedIdx = EntityTraits::GetIndex(m_Entities[denseIdx]);
        m_Sparse[movedIdx] = denseIdx;

        // 末尾を削る
        m_Dense.pop_back();
        m_Entities.pop_back();

        // 削除した Entity の sparse を無効化
        m_Sparse[idx] = EntityTraits::NULL_ENTITY;
    }

    // ISparseSet の実装：Registry が Entity 破棄時に型を知らずに呼ぶ
    void RemoveIfExists(Entity e) override
    {
        Remove(e);
    }

    // --- この Entity が Component を持っているか（O(1)、generation チェック込み）---
    bool Contains(Entity e) const
    {
        uint32_t idx = EntityTraits::GetIndex(e);
        if (idx >= m_Sparse.size()) return false;

        uint32_t denseIdx = m_Sparse[idx];
        return denseIdx != EntityTraits::NULL_ENTITY   // sparse が有効
            && denseIdx < m_Entities.size()            // 範囲内
            && m_Entities[denseIdx] == e;              // 本人確認（gen 含む完全一致）
    }

    // --- Component 参照を取得（事前に Contains 確認すること）---
    T& Get(Entity e)
    {
        assert(Contains(e) && "Entity does not have this component");
        return m_Dense[m_Sparse[EntityTraits::GetIndex(e)]];
    }

    const T& Get(Entity e) const
    {
        assert(Contains(e) && "Entity does not have this component");
        return m_Dense[m_Sparse[EntityTraits::GetIndex(e)]];
    }

    // --- 遍历用：連続配列を直接公開 ---
    std::vector<T>& GetDense() { return m_Dense; }
    const std::vector<T>& GetDense() const { return m_Dense; }

    // --- dense[i] の持ち主 Entity 一覧（View の遍历基準に使う）---
    const std::vector<Entity>& GetEntities() const { return m_Entities; }

    size_t Size() const { return m_Dense.size(); }
    bool Empty() const { return m_Dense.empty(); }

    void Clear()
    {
        m_Sparse.clear();
        m_Dense.clear();
        m_Entities.clear();
    }

private:
    std::vector<uint32_t> m_Sparse;    // Entity番号 → dense 位置
    std::vector<T>        m_Dense;     // Component 本体（連続）
    std::vector<Entity>   m_Entities;  // dense と並走：持ち主 Entity
};