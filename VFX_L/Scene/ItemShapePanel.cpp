// ============================================================
// ItemShapePanel.cpp
// ============================================================
#include "Scene/ItemShapePanel.h"
#include "Component/BackpackComponent.h"
#include "Component/ManaComponent.h"
#include "Component/TransformComponent.h"
#include "Component/WandComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemDataFile.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <algorithm>
#include <cstdlib>

using DirectX::SimpleMath::Vector3;
using DirectX::SimpleMath::Vector4;

namespace
{
    constexpr int   kEditHalf = 4;           // 形のマス目は -4..4（9x9。バックパックと同じ広さ）
    constexpr int   kEditSize = kEditHalf * 2 + 1;
    constexpr float kEditCell = 22.0f;
    constexpr float kBenchCell = 26.0f;
    constexpr float kGap = 3.0f;

    const ImU32 kColEmpty = IM_COL32(40, 40, 46, 255);
    const ImU32 kColInfluence = IM_COL32(80, 200, 255, 110);
    const ImU32 kColAnchor = IM_COL32(255, 255, 255, 255);
    const ImU32 kColHover = IM_COL32(255, 230, 120, 255);

    // 試し置きのマス（BackpackUI と同じ色）
    const ImU32 kColPlaceable = IM_COL32(115, 82, 56, 255);
    const ImU32 kColLocked = IM_COL32(36, 28, 23, 217);

    // 占有マスの雛形（Apply で今の占有マスを差し替える。影響マスはそのまま）
    //   アンカーは回転の中心になるので、なるべく真ん中寄り
    struct Preset { const char* name; std::vector<CellOffset> cells; };
    const std::vector<Preset>& Presets()
    {
        static const std::vector<Preset> list = {
            { "1x1",    ItemShape::Single() },
            { "I2",     { { 0, 0 }, { 0, 1 } } },
            { "I3",     { { 0, -1 }, { 0, 0 }, { 0, 1 } } },
            { "L3",     { { -1, 0 }, { 0, 0 }, { 0, 1 } } },
            { "L4",     { { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } } },
            { "T4",     { { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 0 } } },
            { "S4",     { { 0, 0 }, { 0, 1 }, { 1, -1 }, { 1, 0 } } },
            { "O4",     ItemShape::Rect(2, 2) },
            { "Rect3x3", ItemShape::Rect(3, 3) },
        };
        return list;
    }

    ImU32 ToU32(const Vector4& c, float a)
    {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, a));
    }

    bool Has(const std::vector<CellOffset>& cells, int r, int c)
    {
        return std::any_of(cells.begin(), cells.end(),
            [&](const CellOffset& o) { return o.row == r && o.col == c; });
    }

    void SetCell(std::vector<CellOffset>& cells, int r, int c, bool on)
    {
        const bool has = Has(cells, r, c);
        if (on && !has) cells.push_back({ r, c });
        if (!on && has)
            cells.erase(std::remove_if(cells.begin(), cells.end(),
                [&](const CellOffset& o) { return o.row == r && o.col == c; }), cells.end());
    }

    CellOffset RotateOne(CellOffset o, int rotation)
    {
        return BackpackLogic::RotateShape({ o }, rotation)[0];
    }

    // 占有マスが 4 近傍でつながっているか
    bool IsConnected(const std::vector<CellOffset>& cells)
    {
        if (cells.size() <= 1) return true;
        std::vector<bool> seen(cells.size(), false);
        std::vector<size_t> stack = { 0 };
        seen[0] = true;
        size_t count = 1;
        while (!stack.empty())
        {
            const CellOffset o = cells[stack.back()];
            stack.pop_back();
            for (size_t i = 0; i < cells.size(); ++i)
            {
                if (seen[i]) continue;
                if (std::abs(cells[i].row - o.row) + std::abs(cells[i].col - o.col) != 1) continue;
                seen[i] = true;
                stack.push_back(i);
                ++count;
            }
        }
        return count == cells.size();
    }

    // マス集合を DrawList に塗る。connect = 隣り合うマスの隙間も塗って 1 枚に（ShapeSprite と同じ）
    //   マス (baseR + o.row, baseC + o.col) の左上 = origin + (col, row) * pitch
    void FillCells(ImDrawList* dl, const ImVec2& origin, float cell, float gap,
        int baseR, int baseC, const std::vector<CellOffset>& cells, ImU32 col, bool connect)
    {
        const float pitch = cell + gap;
        for (const auto& o : cells)
        {
            const float x = origin.x + (float)(baseC + o.col) * pitch;
            const float y = origin.y + (float)(baseR + o.row) * pitch;
            dl->AddRectFilled({ x, y }, { x + cell, y + cell }, col);
            if (!connect) continue;

            const bool right = Has(cells, o.row, o.col + 1);
            const bool down = Has(cells, o.row + 1, o.col);
            if (right) dl->AddRectFilled({ x + cell, y }, { x + pitch, y + cell }, col);
            if (down)  dl->AddRectFilled({ x, y + cell }, { x + cell, y + pitch }, col);
            if (right && down && Has(cells, o.row + 1, o.col + 1))
                dl->AddRectFilled({ x + cell, y + cell }, { x + pitch, y + pitch }, col);
        }
    }

    // マス目の外に出るマスを落とす（base + o が 0..size-1 に入る物だけ残す）
    std::vector<CellOffset> Clip(std::vector<CellOffset> cells, int baseR, int baseC, int size)
    {
        cells.erase(std::remove_if(cells.begin(), cells.end(), [&](const CellOffset& o)
            {
                const int r = baseR + o.row, c = baseC + o.col;
                return r < 0 || r >= size || c < 0 || c >= size;
            }), cells.end());
        return cells;
    }

    // マウスの下のマス（隙間も含めて一番近いマスに丸める）。外なら false
    bool MouseCell(const ImVec2& origin, float cell, float gap, int size, int& outR, int& outC)
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float pitch = cell + gap;
        const float lx = m.x - origin.x, ly = m.y - origin.y;
        if (lx < 0.0f || ly < 0.0f) return false;
        outC = (int)(lx / pitch);
        outR = (int)(ly / pitch);
        return outR >= 0 && outR < size && outC >= 0 && outC < size;
    }

    const char* CategoryName(ItemCategory c)
    {
        switch (c)
        {
        case ItemCategory::Projectile: return "Projectile";
        case ItemCategory::Function:   return "Function";
        case ItemCategory::Area:       return "Area";
        case ItemCategory::Frame:      return "Frame";
        case ItemCategory::Stat:       return "Stat";
        case ItemCategory::Summon:     return "Summon";
        default:                       return "?";
        }
    }
}

// ============================================================
// 初期化：試し置きの術者を 1 体作る
// ============================================================
void ItemShapePanel::Init(SwarmSystem* swarm, AreaVFXPlayer* areaVFX, const VFXContext* vfxCtx)
{
    m_Weapon.SetSwarm(swarm);
    m_Weapon.SetAreaVFX(areaVFX, vfxCtx);

    m_Caster = m_Reg.Create();
    m_Reg.Add<TransformComponent>(m_Caster, {});

    WandComponent wand;
    wand.range = 80.0f;   // 標的までの距離を変えても届くように
    m_Reg.Add<WandComponent>(m_Caster, wand);

    // 魔力は無限扱い（毎フレーム満タンに戻す）。形と効き方を見るための台なので
    ManaComponent mana;
    mana.max = mana.current = 1.0e6f;
    m_Reg.Add<ManaComponent>(m_Caster, mana);

    m_Reg.Add<BackpackComponent>(m_Caster, {});

    // 全面に枠 + 真ん中に火球（2x2）、右隣に分裂ルーン（開いた瞬間に「影響で 2 発になる」が見える）
    FillFrames();
    BackpackComponent& bp = Bench();
    const int mid = BackpackComponent::GRID / 2;
    BackpackLogic::Place(bp, ItemID::Fireball, mid, mid, 0);
    BackpackLogic::Place(bp, ItemID::SplitRune, mid, mid + 2, 0);

    if (!ItemDatabase::GetAllIDs().empty())
        m_Item = m_BenchItem = ItemDatabase::GetAllIDs().front();
}

BackpackComponent& ItemShapePanel::Bench()
{
    return m_Reg.Get<BackpackComponent>(m_Caster);
}

void ItemShapePanel::OnActivate()
{
    if (m_Reg.IsValid(m_Caster)) Bench().dirty = true;
}

// ============================================================
// 更新：集約 → 撃つ
// ============================================================
void ItemShapePanel::Update(float dt, const Vector3& muzzle)
{
    if (!m_Reg.IsValid(m_Caster)) return;

    // R で置く向きを回す（文字入力中は無効）
    if (!ImGui::GetIO().WantTextInput && InputManager::Get().GetKeyTrigger('R'))
        m_BenchRot = (m_BenchRot + 1) % 4;

    auto& wand = m_Reg.Get<WandComponent>(m_Caster);
    m_Reg.Get<TransformComponent>(m_Caster).position = muzzle - wand.muzzleOffset;

    m_Aggregate.Update(m_Reg);   // dirty の時だけ組み直す

    auto& mana = m_Reg.Get<ManaComponent>(m_Caster);
    mana.current = mana.max;
    mana.pendingSpend = 0.0f;

    if (m_Fire)
        m_Weapon.Update(m_Reg, dt, m_NoCollision);
}

// ============================================================
// 形の差し替え・未保存の印・保存
// ============================================================
void ItemShapePanel::ApplyEdit(const std::vector<CellOffset>& occupy,
    const std::vector<CellOffset>& influence)
{
    ItemDatabase::SetShape(m_Item, occupy, influence);
    SetUnsaved(m_Item, true);
    if (m_Reg.IsValid(m_Caster))
        m_BenchEvicted += BackpackLogic::Refit(Bench());
}

bool ItemShapePanel::IsUnsaved(ItemID id) const
{
    return std::find(m_Unsaved.begin(), m_Unsaved.end(), id) != m_Unsaved.end();
}

void ItemShapePanel::SetUnsaved(ItemID id, bool unsaved)
{
    m_Unsaved.erase(std::remove(m_Unsaved.begin(), m_Unsaved.end(), id), m_Unsaved.end());
    if (unsaved) m_Unsaved.push_back(id);
}

bool ItemShapePanel::SaveItem(ItemID id)
{
    const ItemCommon* c = ItemDatabase::GetCommon(id);
    if (!c) return false;
    if (!ItemDataFile::SaveShape(c->name, c->occupyCells, c->influenceCells)) return false;
    SetUnsaved(id, false);
    return true;
}

// ============================================================
// 頁の中身
// ============================================================
void ItemShapePanel::DrawTab()
{
    ImGui::TextDisabled("Pick an item, paint its occupy / influence cells, Save.");
    ImGui::TextDisabled("Test placement / aggregate / firing: \"Item Test Bench\" window.");

    DrawItemList();
    ImGui::Separator();
    DrawShapeGrid();
    DrawShapeTools();

    // ---------- 保存 ----------
    ImGui::Separator();
    const ItemCommon* c = ItemDatabase::GetCommon(m_Item);
    if (c)
    {
        char label[128];
        snprintf(label, sizeof(label), "Save \"%s\"", c->name);
        if (ImGui::Button(label))
            m_Status = SaveItem(m_Item) ? std::string("saved ") + ItemDataFile::PathOf(c->name)
                                        : std::string("save failed");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_Unsaved.empty());
    char all[64];
    snprintf(all, sizeof(all), "Save all unsaved (%zu)", m_Unsaved.size());
    if (ImGui::Button(all))
    {
        int ok = 0;
        const std::vector<ItemID> list = m_Unsaved;   // SaveItem が m_Unsaved を書き換えるので写しを回す
        for (ItemID id : list) if (SaveItem(id)) ++ok;
        m_Status = "saved " + std::to_string(ok) + " item(s)";
    }
    ImGui::EndDisabled();
    if (!m_Status.empty()) ImGui::TextDisabled("%s", m_Status.c_str());
    ImGui::TextDisabled("-> %s<Name>.json (overrides Items/*.h on next start)", ItemDataFile::kDir);
}

// ============================================================
// アイテムの一覧（塗るアイテムを選ぶ）
//   * = 未保存、[file] = 保存済みのアイテムデータがある（コードの形を上書きしている）
// ============================================================
void ItemShapePanel::DrawItemList()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Items");
    if (ImGui::BeginListBox("##items", ImVec2(-1, 150)))
    {
        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;

            ImGui::PushID((int)id);
            ImGui::ColorButton("##col", ImVec4(c->color.x, c->color.y, c->color.z, 1),
                ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();

            char label[160];
            snprintf(label, sizeof(label), "%s%s   [%s]  occupy %zu / influence %zu%s",
                IsUnsaved(id) ? "* " : "", c->name, CategoryName(c->category),
                c->occupyCells.size(), c->influenceCells.size(),
                ItemDataFile::Exists(c->name) ? "  [file]" : "");
            if (ImGui::Selectable(label, id == m_Item))
            {
                m_Item = id;
                m_BenchItem = id;   // 試し置きでもすぐ置けるように揃える
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
}

// ============================================================
// 形のマス目（選んでいるアイテムの形）
//   左 = 占有マス、右 = 影響マス（押したマスの反転値でなぞり塗り）
//   Ctrl + 左 = そのマスがアンカー (0,0) になるよう全体をずらす
//   表示だけ回せる。塗った位置は回転を戻して形へ書く
// ============================================================
void ItemShapePanel::DrawShapeGrid()
{
    const ItemCommon* c = ItemDatabase::GetCommon(m_Item);
    if (!c) return;

    // 枠は他を強化しないので影響マスを塗らせない
    const bool isFrame = (c->category == ItemCategory::Frame);
    std::vector<CellOffset> occ = c->occupyCells;
    std::vector<CellOffset> infl = c->influenceCells;

    ImGui::Text("Preview rotation:");
    for (int r = 0; r < 4; ++r)
    {
        ImGui::SameLine();
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%d##prot", r * 90);
        ImGui::RadioButton(lbl, &m_PreviewRot, r);
    }

    const float pitch = kEditCell + kGap;
    const ImVec2 size = { pitch * kEditSize - kGap, pitch * kEditSize - kGap };
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##shape_grid", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool gridHovered = ImGui::IsItemHovered();

    // マウスの下のマス（表示座標 → 形の座標）。なぞり塗り中は窓の外に出ても追う
    int gr = -1, gc = -1;
    const bool onCell = MouseCell(origin, kEditCell, kGap, kEditSize, gr, gc)
        && (gridHovered || ImGui::IsItemActive());
    const int inv = (4 - m_PreviewRot) % 4;
    const CellOffset sc = RotateOne({ gr - kEditHalf, gc - kEditHalf }, inv);

    bool changed = false;
    if (ImGui::IsItemActivated() && onCell)
    {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            // ここをアンカーに。全体を (-sc) ずらす（はみ出した分はマス目の外に出るだけで消さない）
            for (auto& o : occ)  { o.row -= sc.row; o.col -= sc.col; }
            for (auto& o : infl) { o.row -= sc.row; o.col -= sc.col; }
            changed = true;
            m_PaintLayer = -1;
        }
        else
        {
            m_PaintLayer = ImGui::IsMouseDown(ImGuiMouseButton_Right) ? 1 : 0;
            if (m_PaintLayer == 1 && isFrame) m_PaintLayer = -1;
            if (m_PaintLayer >= 0)
                m_PaintValue = !Has(m_PaintLayer == 0 ? occ : infl, sc.row, sc.col);
            m_LastPaintR = m_LastPaintC = 999;
        }
    }
    if (!ImGui::IsItemActive()) m_PaintLayer = -1;

    if (m_PaintLayer >= 0 && onCell && (gr != m_LastPaintR || gc != m_LastPaintC))
    {
        m_LastPaintR = gr;
        m_LastPaintC = gc;
        if (m_PaintLayer == 0)
        {
            // 最後の 1 マスは消させない（占有マスが空だと「どこにでも置けて何も塞がない」物になる）
            const bool lastOne = !m_PaintValue && occ.size() == 1 && Has(occ, sc.row, sc.col);
            if (!lastOne)
            {
                SetCell(occ, sc.row, sc.col, m_PaintValue);
                // 占有マスにしたマスは影響マスから外す（自分自身には効かないので意味が無い）
                if (m_PaintValue) SetCell(infl, sc.row, sc.col, false);
                changed = true;
            }
        }
        else if (!Has(occ, sc.row, sc.col))
        {
            SetCell(infl, sc.row, sc.col, m_PaintValue);
            changed = true;
        }
    }

    if (changed)
        ApplyEdit(occ, infl);

    // ---- 描画（表示は回転後）----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int r = 0; r < kEditSize; ++r)
        for (int cc = 0; cc < kEditSize; ++cc)
        {
            const float x = origin.x + cc * pitch, y = origin.y + r * pitch;
            dl->AddRectFilled({ x, y }, { x + kEditCell, y + kEditCell }, kColEmpty);
        }

    FillCells(dl, origin, kEditCell, kGap, kEditHalf, kEditHalf,
        Clip(BackpackLogic::RotateShape(infl, m_PreviewRot), kEditHalf, kEditHalf, kEditSize),
        kColInfluence, false);
    FillCells(dl, origin, kEditCell, kGap, kEditHalf, kEditHalf,
        Clip(BackpackLogic::RotateShape(occ, m_PreviewRot), kEditHalf, kEditHalf, kEditSize),
        ToU32(c->color, 1.0f), true);

    {
        const float x = origin.x + kEditHalf * pitch, y = origin.y + kEditHalf * pitch;
        dl->AddRect({ x - 1, y - 1 }, { x + kEditCell + 1, y + kEditCell + 1 }, kColAnchor, 0.0f, 0, 2.0f);
    }
    if (onCell)
    {
        const float x = origin.x + gc * pitch, y = origin.y + gr * pitch;
        dl->AddRect({ x, y }, { x + kEditCell, y + kEditCell }, kColHover, 0.0f, 0, 1.5f);
    }

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(c->color.x, c->color.y, c->color.z, 1), "%s", c->name);
    ImGui::TextDisabled("L-click: occupy");
    if (isFrame) ImGui::TextDisabled("(frame: no influence)");
    else         ImGui::TextColored(ImVec4(0.3f, 0.8f, 1.0f, 1), "R-click: influence");
    ImGui::TextDisabled("drag = paint");
    ImGui::TextDisabled("Ctrl+L = anchor here");
    ImGui::TextDisabled("white box = anchor");
    ImGui::TextDisabled("(rotation pivot,");
    ImGui::TextDisabled(" grab point)");
    if (onCell) ImGui::Text("cell (%d, %d)", sc.row, sc.col);
    else        ImGui::TextDisabled("cell -");
    ImGui::EndGroup();
}

// ============================================================
// 形のアイテム箱：雛形・ずらす・影響マスの自動生成・戻す + 警告
// ============================================================
void ItemShapePanel::DrawShapeTools()
{
    const ItemCommon* c = ItemDatabase::GetCommon(m_Item);
    if (!c) return;

    const bool isFrame = (c->category == ItemCategory::Frame);
    std::vector<CellOffset> occ = c->occupyCells;
    std::vector<CellOffset> infl = c->influenceCells;
    bool changed = false;

    // ---- 占有マスの雛形 ----
    const auto& presets = Presets();
    m_Preset = std::clamp(m_Preset, 0, (int)presets.size() - 1);
    ImGui::Text("Occupy preset");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    if (ImGui::BeginCombo("##preset", presets[m_Preset].name))
    {
        for (int i = 0; i < (int)presets.size(); ++i)
            if (ImGui::Selectable(presets[i].name, i == m_Preset)) m_Preset = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Apply##preset"))
    {
        occ = presets[m_Preset].cells;
        infl.erase(std::remove_if(infl.begin(), infl.end(),
            [&](const CellOffset& o) { return Has(occ, o.row, o.col); }), infl.end());
        changed = true;
    }

    // ---- ずらす（表示の向きで上下左右。形の座標へは回転を戻して足す）----
    auto shift = [&](int dr, int dc)
        {
            const CellOffset d = RotateOne({ dr, dc }, (4 - m_PreviewRot) % 4);
            for (const auto* v : { &occ, &infl })
                for (const auto& o : *v)
                    if (std::abs(o.row + d.row) > kEditHalf || std::abs(o.col + d.col) > kEditHalf)
                        return;   // マス目の外に出るならずらさない
            for (auto* v : { &occ, &infl })
                for (auto& o : *v) { o.row += d.row; o.col += d.col; }
            changed = true;
        };

    ImGui::Text("Shift");
    ImGui::SameLine(); if (ImGui::ArrowButton("##sl", ImGuiDir_Left))  shift(0, -1);
    ImGui::SameLine(); if (ImGui::ArrowButton("##su", ImGuiDir_Up))    shift(-1, 0);
    ImGui::SameLine(); if (ImGui::ArrowButton("##sd", ImGuiDir_Down))  shift(1, 0);
    ImGui::SameLine(); if (ImGui::ArrowButton("##sr", ImGuiDir_Right)) shift(0, 1);

    // ---- 影響マスの自動生成：占有マスのまわりを囲む ----
    auto autoInfluence = [&](bool diagonal)
        {
            infl.clear();
            for (const auto& o : occ)
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                    {
                        if (dr == 0 && dc == 0) continue;
                        if (!diagonal && dr != 0 && dc != 0) continue;
                        const int r = o.row + dr, cc = o.col + dc;
                        if (std::abs(r) > kEditHalf || std::abs(cc) > kEditHalf) continue;
                        if (Has(occ, r, cc) || Has(infl, r, cc)) continue;
                        infl.push_back({ r, cc });
                    }
            changed = true;
        };

    if (!isFrame)
    {
        ImGui::Text("Influence");
        ImGui::SameLine(); if (ImGui::Button("Around (4)")) autoInfluence(false);
        ImGui::SameLine(); if (ImGui::Button("Around (8)")) autoInfluence(true);
        ImGui::SameLine(); if (ImGui::Button("Clear##infl")) { infl.clear(); changed = true; }
    }

    // ---- 戻す ----
    if (ImGui::Button("Revert to code (Items/*.h)"))
    {
        if (ItemDatabase::GetCodeShape(m_Item, occ, infl)) changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!ItemDataFile::Exists(c->name));
    if (ImGui::Button("Reload saved file"))
    {
        if (ItemDataFile::LoadShape(c->name, occ, infl))
        {
            ApplyEdit(occ, infl);
            SetUnsaved(m_Item, false);   // ファイルと同じ中身に戻ったので未保存ではない
            m_Status = "reloaded " + ItemDataFile::PathOf(c->name);
        }
    }
    ImGui::EndDisabled();

    if (changed)
        ApplyEdit(occ, infl);

    // ---- 警告（差し替え後の形で見る）----
    c = ItemDatabase::GetCommon(m_Item);
    if (!Has(c->occupyCells, 0, 0))
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "Anchor is not occupied (spellbook grabs the anchor cell)");
    if (!IsConnected(c->occupyCells))
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Occupy cells are not connected");
}

// ============================================================
// 試し置き：枠
// ============================================================
void ItemShapePanel::FillFrames()
{
    // 枠の形が何であっても、左上から順に置けるだけ置く（3x3 なら 9 枚で全面）
    BackpackComponent& bp = Bench();
    bp.frames.clear();
    BackpackLogic::RebuildFrameOccupancy(bp);
    for (int r = 0; r < BackpackComponent::GRID; ++r)
        for (int c = 0; c < BackpackComponent::GRID; ++c)
            BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, r, c, 0);
    BackpackLogic::Refit(bp);   // 枠の上に乗っていた魔法を置き直す
}

void ItemShapePanel::ResetToGameStart()
{
    BackpackComponent& bp = Bench();
    bp.items.clear();
    bp.frames.clear();
    BackpackLogic::RebuildOccupancy(bp);
    BackpackLogic::RebuildFrameOccupancy(bp);
    const int start = BackpackComponent::GRID / 2 - 1;
    BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, start, start, 0);
    bp.dirty = true;
}

// ============================================================
// 試し置きの窓
// ============================================================
void ItemShapePanel::DrawBench()
{
    if (!m_Reg.IsValid(m_Caster)) return;

    ImGui::SetNextWindowSize(ImVec2(520, 760), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Item Test Bench"))
    {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Same path as the game: BackpackLogic -> Aggregate -> WeaponSystem");

    // ---------- アイテムを選ぶ ----------
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Palette   (rotation %d deg, R = rotate)", m_BenchRot * 90);
    if (ImGui::BeginListBox("##palette", ImVec2(-1, 120)))
    {
        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;
            ImGui::PushID((int)id);
            ImGui::ColorButton("##col", ImVec4(c->color.x, c->color.y, c->color.z, 1),
                ImGuiColorEditFlags_NoTooltip, ImVec2(12, 12));
            ImGui::SameLine();
            char label[128];
            snprintf(label, sizeof(label), "%s   [%s]  %zu cell(s)", c->name, CategoryName(c->category),
                c->occupyCells.size());
            if (ImGui::Selectable(label, id == m_BenchItem))
                m_BenchItem = id;
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }
    if (ImGui::Button("Rot -")) m_BenchRot = (m_BenchRot + 3) % 4;
    ImGui::SameLine();
    if (ImGui::Button("Rot +")) m_BenchRot = (m_BenchRot + 1) % 4;

    // ---------- バックパック ----------
    DrawBenchGrid();

    BackpackComponent& bp = Bench();
    if (ImGui::Button("Fill frames")) FillFrames();
    ImGui::SameLine();
    if (ImGui::Button("Game start (3x3)")) ResetToGameStart();
    ImGui::SameLine();
    if (ImGui::Button("Clear spells"))
    {
        bp.items.clear();
        BackpackLogic::RebuildOccupancy(bp);
        bp.dirty = true;
    }
    if (m_BenchEvicted > 0)
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "%d item(s) returned (shape changed, no longer fit)", m_BenchEvicted);
        ImGui::SameLine();
        if (ImGui::SmallButton("OK")) m_BenchEvicted = 0;
    }

    ImGui::Checkbox("Fire at targets (infinite mana)", &m_Fire);

    // ---------- 集約結果 ----------
    ImGui::Separator();
    DrawBenchResult();

    ImGui::End();
}

// ============================================================
// 試し置きのマス目
//   左クリック = 選んだアイテムを置く（枠なら枠として）
//   右クリック = そのマスの魔法を外す（無ければ枠を外す）
//   アイテムの上にマウス = その影響マスと、影響を受けているアイテムを光らせる
//   空きマスの上 = 置いた時の影（緑 = 置ける / 赤 = 置けない）
// ============================================================
void ItemShapePanel::DrawBenchGrid()
{
    BackpackComponent& bp = Bench();
    const int G = BackpackComponent::GRID;
    const float pitch = kBenchCell + kGap;
    const ImVec2 size = { pitch * G - kGap, pitch * G - kGap };
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##bench_grid", size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();

    int hr = -1, hc = -1;
    const bool onCell = hovered && MouseCell(origin, kBenchCell, kGap, G, hr, hc);
    const bool isFrame = ItemDatabase::IsFrame(m_BenchItem);

    // ---- 操作 ----
    if (onCell && ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        if (isFrame) BackpackLogic::PlaceFrame(bp, m_BenchItem, hr, hc, m_BenchRot);
        else         BackpackLogic::Place(bp, m_BenchItem, hr, hc, m_BenchRot);
    }
    if (onCell && ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        const int item = BackpackLogic::GetItemAt(bp, hr, hc);
        if (item >= 0) BackpackLogic::Remove(bp, item);
        else
        {
            const int frame = BackpackLogic::GetFrameAt(bp, hr, hc);
            if (frame >= 0) BackpackLogic::RemoveFrame(bp, frame);
        }
    }

    // ---- 描画 ----
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int r = 0; r < G; ++r)
        for (int c = 0; c < G; ++c)
        {
            const float x = origin.x + c * pitch, y = origin.y + r * pitch;
            dl->AddRectFilled({ x, y }, { x + kBenchCell, y + kBenchCell },
                bp.IsPlaceable(r, c) ? kColPlaceable : kColLocked);
        }

    // 置かれた魔法（異形は 1 枚に）+ アンカーに小さい点
    for (const auto& item : bp.items)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;
        FillCells(dl, origin, kBenchCell, kGap, item.row, item.col,
            BackpackLogic::RotateShape(c->occupyCells, item.rotation), ToU32(c->color, 1.0f), true);
        const float ax = origin.x + item.col * pitch + kBenchCell * 0.5f;
        const float ay = origin.y + item.row * pitch + kBenchCell * 0.5f;
        dl->AddCircleFilled({ ax, ay }, 2.5f, IM_COL32(0, 0, 0, 160));
    }

    const int hoverItem = onCell ? BackpackLogic::GetItemAt(bp, hr, hc) : -1;
    if (hoverItem >= 0)
    {
        // 影響マス + 影響を受けているアイテム
        const auto& src = bp.items[hoverItem];
        const ItemCommon* c = ItemDatabase::GetCommon(src.id);
        if (c && !c->influenceCells.empty())
        {
            const auto cells = Clip(BackpackLogic::RotateShape(c->influenceCells, src.rotation),
                src.row, src.col, G);
            FillCells(dl, origin, kBenchCell, kGap, src.row, src.col, cells, ToU32(c->color, 0.45f), false);

            for (const auto& off : cells)
            {
                const int t = bp.GetOccupant(src.row + off.row, src.col + off.col);
                if (t < 0 || t == hoverItem) continue;
                const auto& ti = bp.items[t];
                const ItemCommon* tc = ItemDatabase::GetCommon(ti.id);
                if (!tc) continue;
                FillCells(dl, origin, kBenchCell, kGap, ti.row, ti.col,
                    BackpackLogic::RotateShape(tc->occupyCells, ti.rotation),
                    IM_COL32(255, 255, 190, 90), true);
            }
        }

        ImGui::SetTooltip("%s\nrot: %d\nR-click: remove",
            c ? c->name : "?", src.rotation * 90);
    }
    else if (onCell)
    {
        // 置いた時の影
        const ItemCommon* c = ItemDatabase::GetCommon(m_BenchItem);
        if (c)
        {
            const bool ok = isFrame
                ? BackpackLogic::CanPlaceFrame(bp, m_BenchItem, hr, hc, m_BenchRot)
                : BackpackLogic::CanPlace(bp, m_BenchItem, hr, hc, m_BenchRot);

            if (!isFrame && !c->influenceCells.empty())
                FillCells(dl, origin, kBenchCell, kGap, hr, hc,
                    Clip(BackpackLogic::RotateShape(c->influenceCells, m_BenchRot), hr, hc, G),
                    ToU32(c->color, 0.25f), false);

            FillCells(dl, origin, kBenchCell, kGap, hr, hc,
                Clip(BackpackLogic::RotateShape(c->occupyCells, m_BenchRot), hr, hc, G),
                ok ? IM_COL32(100, 255, 130, 120) : IM_COL32(255, 80, 80, 120), true);
        }
    }
}

// ============================================================
// 集約結果：どの攻撃ブロックが何に影響されたか + 最終の値
// ============================================================
void ItemShapePanel::DrawBenchResult()
{
    ImGui::TextColored(ImVec4(0.6f, 0.9f, 1, 1), "Aggregate (rebuilds: %d)", m_Aggregate.GetRebuildCount());

    for (const auto& log : m_Aggregate.GetLog())
    {
        std::string by;
        for (const auto& n : log.influencedBy)
        {
            if (!by.empty()) by += ", ";
            by += n;
        }
        ImGui::BulletText("%s @(%d,%d)  <-  %s", log.sourceName.c_str(), log.row, log.col,
            by.empty() ? "(none)" : by.c_str());
    }

    const auto& wand = m_Reg.Get<WandComponent>(m_Caster);
    if (!wand.spells.empty() &&
        ImGui::BeginTable("##spells", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        for (const char* h : { "Spell", "count", "spread", "casts", "damage", "speed", "interval", "mana" })
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto& s : wand.spells)
        {
            const ItemCommon* c = ItemDatabase::GetCommon(s.id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", c ? c->name : "?");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%d", s.projectileCount);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.0f", s.spreadAngle);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%d", s.castCount);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f", s.damage);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.1f", s.speed);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%.2f", s.castInterval);
            ImGui::TableSetColumnIndex(7); ImGui::Text("%.1f", s.manaCost);
        }
        ImGui::EndTable();
    }

    if (!wand.areas.empty() &&
        ImGui::BeginTable("##areas", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        for (const char* h : { "Area", "radius", "duration", "tick", "dmg/tick", "interval", "mana" })
            ImGui::TableSetupColumn(h);
        ImGui::TableHeadersRow();
        for (const auto& a : wand.areas)
        {
            const ItemCommon* c = ItemDatabase::GetCommon(a.id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", c ? c->name : "?");
            ImGui::TableSetColumnIndex(1); ImGui::Text("%.2f", a.radius);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%.2f", a.duration);
            ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", a.tickInterval);
            ImGui::TableSetColumnIndex(4); ImGui::Text("%.1f", a.damagePerTick);
            ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", a.castInterval);
            ImGui::TableSetColumnIndex(6); ImGui::Text("%.1f", a.manaCost);
        }
        ImGui::EndTable();
    }

    if (wand.spells.empty() && wand.areas.empty())
        ImGui::TextDisabled("No attack block placed");
}
