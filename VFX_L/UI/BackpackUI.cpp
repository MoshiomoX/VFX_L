// ============================================================
// BackpackUI.cpp
// ============================================================
#include "UI/BackpackUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Component/SpellbookComponent.h"
#include "Graphics/Material/Texture.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX::SimpleMath;

void BackpackUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

// ============================================================
// アイコンの読み込み
// iconPath が nullptr のものは color で塗ったブロックで代用する
// ============================================================
void BackpackUI::LoadIcons()
{
    m_Icons.clear();

    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c || !c->iconPath) continue;

        auto tex = ResourceManager::Get().LoadTexture(c->iconPath);
        if (tex) m_Icons.push_back({ id, tex });
    }
}

std::shared_ptr<Texture> BackpackUI::GetIcon(ItemID id) const
{
    for (const auto& p : m_Icons)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// グリッド全体の一辺の長さ
// ============================================================
float BackpackUI::GridExtent() const
{
    return m_CellSize * GRID_SIZE + m_CellGap * (GRID_SIZE - 1);
}

// ============================================================
// レイアウトの確定
// 比率からピクセル値を計算する。
//   画面サイズが変わった時も同じ手順で組み直す。
// ============================================================
void BackpackUI::Layout(float screenW, float screenH)
{
    m_ScreenSize = { screenW, screenH };

    float shortSide = (screenW < screenH) ? screenW : screenH;

    // グリッド全体の目標サイズから 1 マスの大きさを逆算する
    //   extent = cell * GRID + gap * (GRID - 1)、gap = cell * cellGapRatio
    //   → extent = cell * (GRID + cellGapRatio * (GRID - 1))
    float targetExtent = shortSide * gridScreenRatio;
    float denom = (float)GRID_SIZE + cellGapRatio * (float)(GRID_SIZE - 1);

    m_CellSize = targetExtent / denom;
    m_CellGap = m_CellSize * cellGapRatio;
    m_FramePad = m_CellSize * framePadRatio;

    float extent = GridExtent();
    float margin = shortSide * marginRatio;

    // 外枠は m_Origin - m_FramePad から始まるので、その分を含めて位置を決める
    switch (anchor)
    {
    case Anchor::Center:
        m_Origin.x = (screenW - extent) * 0.5f;
        m_Origin.y = (screenH - extent) * 0.5f;
        break;

    case Anchor::TopRight:
        m_Origin.x = screenW - margin - m_FramePad - extent;
        m_Origin.y = margin + m_FramePad;
        break;

    case Anchor::BottomLeft:
        m_Origin.x = margin + m_FramePad;
        m_Origin.y = screenH - margin - m_FramePad - extent;
        break;

    case Anchor::BottomRight:
        m_Origin.x = screenW - margin - m_FramePad - extent;
        m_Origin.y = screenH - margin - m_FramePad - extent;
        break;

    case Anchor::TopLeft:
    default:
        m_Origin.x = margin + m_FramePad;
        m_Origin.y = margin + m_FramePad;
        break;
    }
}

// ============================================================
// マスの左上のスクリーン座標
// ============================================================
Vector2 BackpackUI::CellPosition(int row, int col) const
{
    return {
        m_Origin.x + col * CellPitch(),
        m_Origin.y + row * CellPitch()
    };
}

// ============================================================
// スクリーン座標 → マス座標
// ============================================================
bool BackpackUI::ScreenToCell(const Vector2& screenPos, int& outRow, int& outCol) const
{
    float pitch = CellPitch();
    if (pitch <= 0.0f) return false;

    float localX = screenPos.x - m_Origin.x;
    float localY = screenPos.y - m_Origin.y;
    if (localX < 0.0f || localY < 0.0f) return false;

    int col = (int)(localX / pitch);
    int row = (int)(localY / pitch);
    if (row < 0 || row >= GRID_SIZE || col < 0 || col >= GRID_SIZE) return false;

    // マスとマスの隙間に乗っている場合は外れ扱い
    float inCellX = localX - col * pitch;
    float inCellY = localY - row * pitch;
    if (inCellX > m_CellSize || inCellY > m_CellSize) return false;

    outRow = row;
    outCol = col;
    return true;
}

// ============================================================
// ドラッグ開始
// 掴む優先順位: 魔法（上）→ 枠（下）→ パレットの新規。
// 何が掴まれたかは以後 IsFrame(id) で判別する
// ============================================================
void BackpackUI::BeginDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    int row = 0, col = 0;
    if (!ScreenToCell(mousePos, row, col)) return;

    const int itemIdx = BackpackLogic::GetItemAt(bp, row, col);
    const int frameIdx = BackpackLogic::GetFrameAt(bp, row, col);

    // 空マスからは何も掴めない。新規の取り出し口は魔法書の箱だけ
    if (itemIdx < 0 && frameIdx < 0) return;

    // ---- グリッド上の既存を掴む（魔法優先）----
    const bool grabItem = (itemIdx >= 0);
    const auto& src = grabItem ? bp.items[itemIdx] : bp.frames[frameIdx];

    m_Drag->source = DragSource::BackpackGrid;
    m_Drag->id = src.id;
    m_Drag->rotation = src.rotation;
    m_Drag->originalIndex = grabItem ? itemIdx : frameIdx;

    const Vector2 origin = CellPosition(src.row, src.col);
    m_Drag->grabOffset = { mousePos.x - origin.x, mousePos.y - origin.y };

    UpdateDrag(bp, mousePos);
}
// ============================================================
// ドラッグ中
// 置き先の判定だけ。データは書き換えない。
// 枠か魔法かは掴んでいる id が決める
// ============================================================
void BackpackUI::UpdateDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    if (!m_Drag->IsActive()) return;

    const float pitch = CellPitch();
    if (pitch <= 0.0f) return;

    const float ox = mousePos.x - m_Drag->grabOffset.x;
    const float oy = mousePos.y - m_Drag->grabOffset.y;

    m_Drag->dropCol = (int)std::lround((ox - m_Origin.x) / pitch);
    m_Drag->dropRow = (int)std::lround((oy - m_Origin.y) / pitch);

    const int ignore = (m_Drag->source == DragSource::BackpackGrid)
        ? m_Drag->originalIndex : -1;

    m_Drag->canDrop = ItemDatabase::IsFrame(m_Drag->id)
        ? BackpackLogic::CanPlaceFrame(bp, m_Drag->id,
            m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation, ignore)
        : BackpackLogic::CanPlace(bp, m_Drag->id,
            m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation, ignore);
}

// ============================================================
// ドラッグ終了
// Spellbook / Palette からの新規は同じ「置くだけ」。
// 置けなかった時に何もしなくて良いのも同じ:
//   Spellbook 由来なら次の同期で勝手に箱へ降って戻る
// ============================================================
void BackpackUI::EndDrag(BackpackComponent& bp)
{
    if (!m_Drag->IsActive()) return;

    const bool isFrame = ItemDatabase::IsFrame(m_Drag->id);

    if (m_Drag->canDrop)
    {
        if (m_Drag->source == DragSource::BackpackGrid)
        {
            if (isFrame)
            {
                int evicted = 0;
                BackpackLogic::MoveFrame(bp, m_Drag->originalIndex,
                    m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation, &evicted);
                if (evicted > 0) m_LastEvicted = evicted;
            }
            else
            {
                BackpackLogic::Remove(bp, m_Drag->originalIndex);
                BackpackLogic::Place(bp, m_Drag->id,
                    m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation);
            }
        }
        else
        {
            if (isFrame)
                BackpackLogic::PlaceFrame(bp, m_Drag->id,
                    m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation);
            else
                BackpackLogic::Place(bp, m_Drag->id,
                    m_Drag->dropRow, m_Drag->dropCol, m_Drag->rotation);
        }
    }
    else if (m_Drag->source == DragSource::BackpackGrid)
    {
        // グリッド由来を置けない場所（グリッド外含む）で離した = 手元へ戻す。
        // 魔法書の箱に落ちる動きは同期が勝手にやってくれる
        if (isFrame)
            m_LastEvicted = BackpackLogic::RemoveFrame(bp, m_Drag->originalIndex);
        else
            BackpackLogic::Remove(bp, m_Drag->originalIndex);
    }

    CancelDrag();
    m_HoverItemIndex = m_HoverFrameIndex = -1;
}

// ============================================================
// 入力処理
//   左ドラッグ: 掴む / 置く
//   右クリック: 手元へ戻す（削除）
//   ホイール / R: 回転（ドラッグ中はそのブロックも回す）
// ============================================================
void BackpackUI::HandleInput(BackpackComponent& bp)
{
    if (!m_Drag) return;   // SetDragContext 前は何もしない

    auto& input = InputManager::Get();

    // ImGui がマウスを掴んでいる間は何もしない
    if (ImGui::GetIO().WantCaptureMouse)
    {
        // ドラッグ中なら取り消す（ImGui の上で離されると迷子になる）
        CancelDrag();
        m_HoverRow = m_HoverCol = -1;
        m_HoverItemIndex = m_HoverFrameIndex = -1;
        return;
    }

    // マウス座標（スクリーン座標）を取る
    auto mp = input.GetMousePos();
    m_MousePos = { mp.x, mp.y };

    // ---- マウスが乗っているマスを更新 ----
    if (!ScreenToCell(m_MousePos, m_HoverRow, m_HoverCol))
    {
        m_HoverRow = m_HoverCol = -1;
        m_HoverItemIndex = m_HoverFrameIndex = -1;
    }
    else
    {
        m_HoverItemIndex = BackpackLogic::GetItemAt(bp, m_HoverRow, m_HoverCol);
        m_HoverFrameIndex = BackpackLogic::GetFrameAt(bp, m_HoverRow, m_HoverCol);
    }

    // ---- 回転（ホイール or R キー）----
    // ドラッグ中なら掴んでいるものも一緒に回す
    float wheel = input.GetMouseWheel();
    int rotDelta = 0;
    if (wheel > 0.0f)      rotDelta = 1;
    else if (wheel < 0.0f) rotDelta = 3;
    if (input.GetKeyTrigger('R')) rotDelta = 1;

    if (rotDelta != 0)
    {
        m_Rotation = (m_Rotation + rotDelta) % 4;
        if (m_Drag->IsActive())
            m_Drag->rotation = (m_Drag->rotation + rotDelta) % 4;
    }

    // ---- ドラッグ中 ----
    if (m_Drag->IsActive())
    {
        UpdateDrag(bp, m_MousePos);

        if (!input.GetMousePress(0))   // 離した
            EndDrag(bp);

        return;   // ドラッグ中は他の操作を受けない
    }

    if (input.GetMouseTrigger(0))
    {
        BeginDrag(bp, m_MousePos);
        return;
    }

    if (m_HoverRow < 0) return;

    if (input.GetMouseTrigger(1))
    {
        if (m_HoverItemIndex >= 0)
            BackpackLogic::Remove(bp, m_HoverItemIndex);
        else if (m_HoverFrameIndex >= 0)
            m_LastEvicted = BackpackLogic::RemoveFrame(bp, m_HoverFrameIndex);

        m_HoverItemIndex = m_HoverFrameIndex = -1;
    }
}
// ============================================================
// 置き先の影
//
// 置けるなら緑、置けないなら赤。
// 魔法なら影響格も薄く重ねて、置いた後の効き方が分かるようにする
// （枠には influenceCells が無いので自然に何も出ない）
// ============================================================
void BackpackUI::DrawDropShadow(SpriteRenderer& sprite)
{
    if (!m_Drag || !m_Drag->IsActive()) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag->id);
    if (!c) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };
    const Vector4 col = m_Drag->canDrop ? Vector4(0.4f, 1.0f, 0.5f, 0.45f)
        : Vector4(1.0f, 0.3f, 0.3f, 0.45f);

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag->rotation);
    for (const auto& off : cells)
    {
        int r = m_Drag->dropRow + off.row;
        int cc = m_Drag->dropCol + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
    }

    // 魔法なら影響格も薄く重ねる
    if (!ItemDatabase::IsFrame(m_Drag->id) && !c->influenceCells.empty())
    {
        Vector4 ic = c->color;
        ic.w = 0.25f;

        auto infl = BackpackLogic::RotateShape(c->influenceCells, m_Drag->rotation);
        for (const auto& off : infl)
        {
            int r = m_Drag->dropRow + off.row;
            int cc = m_Drag->dropCol + off.col;
            if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

            sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, ic);
        }
    }
}
// ============================================================
// マウスが乗っているブロックの影響格を描く
//
// 全部同時に出さない。
// 触れている1つ分だけ出せば、どこに効いているかが一目で分かる。
// 影響を受けている側のブロックも光らせて、
// 関係を線で結ばなくても追えるようにする。
// ============================================================
void BackpackUI::DrawHoverInfluence(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!showInfluenceOnHover) return;
    if (m_Drag && m_Drag->IsActive()) return;   // ドラッグ中は影を優先
    if (m_HoverItemIndex < 0 || m_HoverItemIndex >= (int)bp.items.size()) return;

    const auto& src = bp.items[m_HoverItemIndex];
    const ItemCommon* c = ItemDatabase::GetCommon(src.id);
    if (!c || c->influenceCells.empty()) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };
    auto cells = BackpackLogic::RotateShape(c->influenceCells, src.rotation);

    Vector4 col = c->color;
    col.w = 0.45f;

    for (const auto& off : cells)
    {
        int r = src.row + off.row;
        int cc = src.col + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
    }

    if (!highlightInfluenced) return;

    // ---- 影響を受けているブロックを光らせる ----
    // 影響格に占位格が1つでも重なっていれば対象。
    // ブロック全体を光らせるので、異形でも見落とさない。
    // 同じブロックを複数回塗っても1回分より濃くなるだけで問題ない。
    Vector4 glow = { 1.0f, 1.0f, 0.75f, 0.35f };

    for (const auto& off : cells)
    {
        int r = src.row + off.row;
        int cc = src.col + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        int target = bp.GetOccupant(r, cc);
        if (target < 0 || target == m_HoverItemIndex) continue;

        const auto& t = bp.items[target];
        const ItemCommon* tc = ItemDatabase::GetCommon(t.id);
        if (!tc) continue;

        auto tcells = BackpackLogic::RotateShape(tc->occupyCells, t.rotation);
        for (const auto& toff : tcells)
            sprite.Draw(m_BlockTex,
                CellPosition(t.row + toff.row, t.col + toff.col),
                cellSizeVec, glow);
    }
}

// ============================================================
// ドラッグ中のブロック
//
// マスには吸い付けず、マウスに追従させる。
// CellPosition を使わずにピクセルで直接置く。
// ============================================================
void BackpackUI::DrawDragged(SpriteRenderer& sprite, const Vector2& mousePos)
{
    if (!m_Drag || !m_Drag->IsActive()) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag->id);
    if (!c) return;

    const float pitch = CellPitch();
    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // アンカーの現在位置（掴んだずれを戻す）
    const float ox = mousePos.x - m_Drag->grabOffset.x;
    const float oy = mousePos.y - m_Drag->grabOffset.y;

    auto icon = GetIcon(m_Drag->id);
    auto tex = icon ? icon : m_BlockTex;

    Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;
    col.w *= dragAlpha;

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag->rotation);
    for (const auto& off : cells)
    {
        Vector2 pos = { ox + off.col * pitch, oy + off.row * pitch };
        sprite.Draw(tex, pos, cellSizeVec, col);
    }
}

// ============================================================
// 描画
// 重ね順 = 呼ぶ順
//   外枠 → マス（枠の有無で色分け）→ 枠のハイライト → 置かれた魔法
//   → 影響格 → 置き先の影 → ドラッグ中のブロック
// ============================================================
void BackpackUI::Draw(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!m_BlockTex) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // ---- 外枠 ----
    float extent = GridExtent();
    Vector2 framePos = { m_Origin.x - m_FramePad, m_Origin.y - m_FramePad };
    Vector2 frameSize = { extent + m_FramePad * 2.0f, extent + m_FramePad * 2.0f };
    sprite.Draw(m_BlockTex, framePos, frameSize, frameColor);

    // ---- マス ----
    // 枠が敷かれていないマスは暗くする。
    // GRID は画布の上限であって、置ける場所ではないことを見せるため。
    for (int row = 0; row < GRID_SIZE; ++row)
    {
        for (int col = 0; col < GRID_SIZE; ++col)
        {
            const bool placeable = bp.IsPlaceable(row, col);
            sprite.Draw(m_BlockTex, CellPosition(row, col), cellSizeVec,
                placeable ? cellColor : lockedCellColor);
        }
    }

    // ---- マウスが乗っている枠をハイライト ----
    // 魔法が乗っているマスでは光らせない
    // （掴むと魔法の方が取れるので、枠が取れると誤解させない）
    if (!(m_Drag && m_Drag->IsActive()) && m_HoverItemIndex < 0 &&
        m_HoverFrameIndex >= 0 && m_HoverFrameIndex < (int)bp.frames.size())
    {
        const auto& f = bp.frames[m_HoverFrameIndex];
        const ItemCommon* fc = ItemDatabase::GetCommon(f.id);
        if (fc)
        {
            Vector4 mark = { 1.0f, 0.9f, 0.5f, 0.30f };
            auto cells = BackpackLogic::RotateShape(fc->occupyCells, f.rotation);
            for (const auto& off : cells)
                sprite.Draw(m_BlockTex,
                    CellPosition(f.row + off.row, f.col + off.col),
                    cellSizeVec, mark);
        }
    }

    // ---- 置かれた魔法（本体）----
    // グリッドから魔法を掴んでいる間は元の位置に描かない。
    // 枠を掴んでいる間は乗っている魔法をそのまま描く。
    const bool hideOriginalItem =
        m_Drag && m_Drag->source == DragSource::BackpackGrid
        && !ItemDatabase::IsFrame(m_Drag->id);

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        if (hideOriginalItem && (int)i == m_Drag->originalIndex)
            continue;

        const auto& item = bp.items[i];
        const ItemCommon* c = ItemDatabase::GetCommon(item.id);
        if (!c) continue;

        auto icon = GetIcon(item.id);
        auto tex = icon ? icon : m_BlockTex;
        Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;

        auto cells = BackpackLogic::RotateShape(c->occupyCells, item.rotation);
        for (const auto& off : cells)
        {
            sprite.Draw(tex, CellPosition(item.row + off.row, item.col + off.col),
                cellSizeVec, col);
        }
    }

    // ---- 影響格 ----
    // 枠には influenceCells が無いので、関数内の空チェックで自然に弾かれる
    DrawHoverInfluence(sprite, bp);

    // ---- 置き先の影 → ドラッグ中のブロック ----
    DrawDropShadow(sprite);
    DrawDragged(sprite, m_MousePos);
}
// ============================================================
// 取り出せる数
//
// 魔法書が未設定なら制限しない（テストシーン用の逃げ道）。
// 所持数からグリッド上の数を引いた残りが取り出せる数。
// ============================================================
int BackpackUI::GetAvailableCount(const BackpackComponent& bp, ItemID id) const
{
    if (!m_Spellbook) return 999;   // 魔法書が無ければ無制限

    const int owned = m_Spellbook->GetCount(id);
    const int placed = ItemDatabase::IsFrame(id)
        ? BackpackLogic::CountPlacedFrames(bp, id)
        : BackpackLogic::CountPlaced(bp, id);

    return owned - placed;
}