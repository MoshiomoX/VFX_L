// ============================================================
// UIManager.cpp
// ============================================================
#include "UIManager.h"

// ============================================================
// 積む
//
// 二重に積まないこと。
// レベルアップの判定は毎フレーム走るので、
// 検査が無いと選び終わるまで積み続けてしまう。
// そうなると Pop 1回では消えず、ゲームが止まったままになる。
// ============================================================
void UIManager::Push(UILayer layer, CloseMode mode)
{
    if (layer == UILayer::None) return;
    if (IsOpen(layer)) return;

    m_Entries.push_back({ layer, mode });
    m_Stack.push_back(layer);
}

// ============================================================
// 下ろす
//
// 一番上でなくても消せるようにしてある。
// 「死亡したので全部閉じる」「プレイヤーが消えたので
// プレイヤー依存の UI を下ろす」といった割り込みがあるため。
//
// ただし通常の流れでは必ず一番上を下ろすこと。
// 途中を抜くと、被さっている方が残って下だけ消えるという
// 見た目になる。
// ============================================================
void UIManager::Pop(UILayer layer)
{
    for (int i = (int)m_Entries.size() - 1; i >= 0; --i)
    {
        if (m_Entries[i].layer != layer) continue;

        m_Entries.erase(m_Entries.begin() + i);
        m_Stack.erase(m_Stack.begin() + i);
        return;
    }
}

void UIManager::Toggle(UILayer layer, CloseMode mode)
{
    if (IsOpen(layer)) Pop(layer);
    else               Push(layer, mode);
}

void UIManager::Clear()
{
    m_Entries.clear();
    m_Stack.clear();
}

// ============================================================
// 入力を受け取ってよいか
//
// 一番上だけが true。
// 下の UI は描かれているが操作できない。
// これで「三択の最中にグリッドを触れてしまう」が起きない。
//
// 何も積まれていない時は None が true。
// 「UI が無い状態」も1つの状態として扱えるようにする。
// ============================================================
bool UIManager::CanReceiveInput(UILayer layer) const
{
    if (m_Entries.empty()) return layer == UILayer::None;
    return m_Entries.back().layer == layer;
}

bool UIManager::IsOpen(UILayer layer) const
{
    for (const auto& e : m_Entries)
        if (e.layer == layer) return true;
    return false;
}

UILayer UIManager::Top() const
{
    return m_Entries.empty() ? UILayer::None : m_Entries.back().layer;
}

const char* UIManager::LayerName(UILayer layer)
{
    switch (layer)
    {
    case UILayer::Backpack: return "Backpack";
    case UILayer::LevelUp:  return "LevelUp";
    case UILayer::None:
    default:                return "None";
    }
}