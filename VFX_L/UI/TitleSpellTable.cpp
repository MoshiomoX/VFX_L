// ============================================================
// TitleSpellTable.cpp
// タイトル画面の右下の魔法の表（UI/TitleSpellTable.h を参照）
// ============================================================
#include "UI/TitleSpellTable.h"
#include "UI/UIDeco.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/InputManager.h"
#include "Manager/ResourceManager.h"
#include "Audio/AudioSystem.h"
#include "ResourcePaths.h"

using DirectX::SimpleMath::Vector2;
using DirectX::SimpleMath::Vector4;

void TitleSpellTable::Initialize()
{
    m_Grid.Initialize(ResourceManager::Get().LoadTexture(Res::Tex::BlockSolo));
    m_Grid.LoadIcons();
    m_Grid.SetDragContext(&m_Drag);
    m_Grid.SetSpellbook(nullptr);   // 何個でも置ける

    // 候補：攻撃魔法・範囲魔法 → ルーン → 召喚物（枠と能力値は出さない）
    m_Palette.clear();
    for (ItemCategory want : { ItemCategory::Projectile, ItemCategory::Area, ItemCategory::Function, ItemCategory::Summon })
        for (ItemID id : ItemDatabase::GetAllIDs())
            if (const ItemCommon* c = ItemDatabase::GetCommon(id); c && c->category == want)
                m_Palette.push_back(id);
    m_Ready = true;
}

void TitleSpellTable::Layout(float screenW, float screenH, float k)
{
    m_K = k;
    const float extent = screenH * 0.36f;       // 盤（縁込み）の一辺
    const float margin = screenH * 0.025f;
    const float bottom = screenH * 0.05f;      // 下は開発用の案内（F キー）の分あける
    const Vector2 gridPos = { screenW - margin - extent, screenH - bottom - extent };
    m_Grid.PlaceAt(gridPos, extent);

    const float pitch = m_Grid.GetCellPitch();
    m_Icon = pitch * 0.92f;
    m_Columns = BackpackUI::GRID_SIZE;
    const int rows = ((int)m_Palette.size() + m_Columns - 1) / m_Columns;
    const float header = screenH * 0.058f;     // 見出しと操作の案内の 2 行
    const float gap = pitch * 0.35f;
    const float left = gridPos.x + (extent - pitch * (float)m_Columns) * 0.5f;
    m_PaletteOrigin = { left, gridPos.y - gap - pitch * (float)rows };
    m_PanelMin = { gridPos.x, m_PaletteOrigin.y - header - gap };
    m_PanelMax = { gridPos.x + extent, gridPos.y + extent };
}

Vector2 TitleSpellTable::PaletteCell(int index) const
{
    const float pitch = m_Grid.GetCellPitch();
    return { m_PaletteOrigin.x + pitch * (float)(index % m_Columns), m_PaletteOrigin.y + pitch * (float)(index / m_Columns) };
}

bool TitleSpellTable::TestPaletteCenter(ItemID id, Vector2& out) const
{
    for (int i = 0; i < (int)m_Palette.size(); ++i)
        if (m_Palette[i] == id) { out = PaletteCell(i) + Vector2(m_Icon * 0.5f, m_Icon * 0.5f); return true; }
    return false;
}

Vector2 TitleSpellTable::TestCellCenter(int row, int col) const
{
    Vector2 mn, mx;
    m_Grid.PanelRect(mn, mx);
    const float pitch = m_Grid.GetCellPitch();
    const float cell = pitch / (1.0f + m_Grid.cellGapRatio);
    const float pad = ((mx.x - mn.x) - (cell * BackpackUI::GRID_SIZE + (pitch - cell) * (BackpackUI::GRID_SIZE - 1))) * 0.5f;
    return { mn.x + pad + pitch * (float)col + cell * 0.5f, mn.y + pad + pitch * (float)row + cell * 0.5f };
}

bool TitleSpellTable::PaletteAt(const Vector2& p, int& index) const
{
    for (int i = 0; i < (int)m_Palette.size(); ++i)
    {
        const Vector2 c = PaletteCell(i);
        if (p.x >= c.x && p.x < c.x + m_Icon && p.y >= c.y && p.y < c.y + m_Icon) { index = i; return true; }
    }
    return false;
}

// ============================================================
// 毎フレーム：候補から掴む → 盤の操作（BackpackUI。置く・動かす・外す・回す）
// ============================================================
bool TitleSpellTable::Update(BackpackComponent* bp)
{
    if (!m_Ready) return false;
    auto& input = InputManager::Get();
    const DirectX::XMFLOAT2 mp = input.GetMousePos();
    const Vector2 mouse(mp.x, mp.y);
    m_MouseOver = mouse.x >= m_PanelMin.x && mouse.x <= m_PanelMax.x && mouse.y >= m_PanelMin.y && mouse.y <= m_PanelMax.y;
    m_HoverPalette = -1;
    int idx = -1;
    if (PaletteAt(mouse, idx)) m_HoverPalette = idx;
    if (!bp || input.IsUsingPad()) return false;
    const uint32_t before = bp->revision;

    // 候補から掴む（魔法書から取り出すのと同じ扱い：置けなければ何も起きない）
    if (!m_Drag.IsActive() && input.GetMouseTrigger(0) && m_HoverPalette >= 0)
    {
        const float pitch = m_Grid.GetCellPitch();
        m_Drag.source = DragSource::Spellbook;
        m_Drag.id = m_Palette[m_HoverPalette];
        m_Drag.rotation = 0;
        m_Drag.originalIndex = -1;
        m_Drag.grabOffset = { pitch * 0.5f, pitch * 0.5f };
        AudioSystem::Get().Play("item_pick");
    }

    // 盤の操作。枠は動かさない・外さない（タイトルの盤はいつも全部のマスに枠）
    int row = -1, col = -1;
    const bool onGrid = m_Grid.ScreenToCell(mouse, row, col);
    const bool rightOnEmpty = input.GetMouseTrigger(1) && (!onGrid || BackpackLogic::GetItemAt(*bp, row, col) < 0);
    if (!rightOnEmpty) m_Grid.HandleInput(*bp);
    if (m_Drag.IsActive() && m_Drag.source == DragSource::BackpackGrid && ItemDatabase::IsFrame(m_Drag.id))
        m_Drag.Reset();
    return bp->revision != before;
}

// ============================================================
// 描く：候補の板（見出し・候補の絵）→ 盤（ドラッグ中の物は盤が一番上に描く）→ 名前
// ============================================================
void TitleSpellTable::Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackComponent* bp, float fade)
{
    if (!m_Ready || !bp || fade <= 0.01f) return;
    const auto& tex = UIDeco::Tex();
    const float k = m_K;
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);

    // ---- 候補の板 ----
    Vector2 gridMin, gridMax;
    m_Grid.PanelRect(gridMin, gridMax);
    {
        UIDeco::PanelStyle ps;
        ps.innerInset = (std::max)(3.0f, 4.0f * k);
        const Vector2 pos = m_PanelMin;
        const Vector2 size = { m_PanelMax.x - m_PanelMin.x, gridMin.y - m_PanelMin.y - 2.0f };
        UIDeco::DrawPanel(sprite, pos, size, gold, ps);
    }
    const float ts = 0.34f * k;
    text.Draw(L"魔法を組む", { m_PanelMin.x + 10.0f * k, m_PanelMin.y + 5.0f * k }, { 1.0f, 0.88f, 0.60f, fade }, ts);
    {
        const std::wstring hint = L"ドラッグで置く・右クリックで外す・ホイールで回す";
        const float hs = 0.24f * k;
        text.Draw(hint, { m_PanelMin.x + 10.0f * k, m_PanelMin.y + 5.0f * k + text.Measure(L"魔", ts).y }, { 0.72f, 0.68f, 0.60f, 0.9f * fade }, hs);
    }
    for (int i = 0; i < (int)m_Palette.size(); ++i)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(m_Palette[i]);
        if (!c) continue;
        const Vector2 p = PaletteCell(i);
        const bool hover = (i == m_HoverPalette) && !m_Drag.IsActive();
        Vector4 bg = c->color;
        bg.w = (hover ? 0.95f : 0.70f) * fade;
        if (tex.white) sprite.Draw(tex.white, p, { m_Icon, m_Icon }, bg);
        Vector4 edge = UIDeco::CategoryColor(c->category);
        edge.w = (hover ? 1.0f : 0.6f) * fade;
        UIDeco::DrawFrameLines(sprite, p, { m_Icon, m_Icon }, edge, hover ? 2.0f : 1.0f);
        if (const auto icon = m_Grid.FindIcon(m_Palette[i]))
        {
            const float s = m_Icon * 0.78f;
            sprite.Draw(icon, p + Vector2((m_Icon - s) * 0.5f, (m_Icon - s) * 0.5f), { s, s }, { 1.0f, 1.0f, 1.0f, fade });
        }
    }

    // ---- 盤 ----
    m_Grid.Draw(sprite, *bp);

    // ---- 名前（候補か、盤の上の魔法に乗っている時）----
    const ItemID* named = nullptr;
    if (m_HoverPalette >= 0 && !m_Drag.IsActive()) named = &m_Palette[m_HoverPalette];
    const int hi = m_Grid.GetHoverItemIndex();
    if (!named && hi >= 0 && hi < (int)bp->items.size()) named = &bp->items[hi].id;
    if (m_Drag.IsActive()) named = &m_Drag.id;
    if (named)
        if (const ItemCommon* c = ItemDatabase::GetCommon(*named))
        {
            const std::wstring name = c->displayName;
            const float ns = 0.36f * k;
            const Vector2 sz = text.Measure(name, ns);
            const DirectX::XMFLOAT2 mp = InputManager::Get().GetMousePos();
            Vector2 pos = { mp.x - sz.x * 0.5f, mp.y - sz.y - 18.0f * k };
            pos.x = (std::min)(pos.x, m_PanelMax.x - sz.x);
            const float o = 1.5f * k;
            for (const Vector2 d : { Vector2(o, 0.0f), Vector2(-o, 0.0f), Vector2(0.0f, o), Vector2(0.0f, -o) })
                text.Draw(name, pos + d, { 0.0f, 0.0f, 0.0f, 0.8f * fade }, ns);
            text.Draw(name, pos, { 1.0f, 0.92f, 0.70f, fade }, ns);
        }
}
