// ============================================================
// BackpackUI.cpp
// ============================================================
#include "BackpackUI.h"
#include "SpriteRenderer.h"
#include "SpellbookComponent.h"
#include "Texture.h"
#include "BackpackLogic.h"
#include "ItemDatabase.h"
#include "ResourceManager.h"
#include "InputManager.h"
#include "imgui.h"
#include <cmath>

using namespace DirectX::SimpleMath;

void BackpackUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

// ============================================================
// アイコン読み込み
// iconPath が nullptr のものは color での染色表示にフォールバックする
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
// グリッド全体の辺の長さ
// ============================================================
float BackpackUI::GridExtent() const
{
    return m_CellSize * GRID_SIZE + m_CellGap * (GRID_SIZE - 1);
}

// ============================================================
// レイアウト計算
// 画面短辺を基準に比率でサイズを決める。
//   縦長・横長どちらでも UI が画面外に出ない。
// ============================================================
void BackpackUI::Layout(float screenW, float screenH)
{
    m_ScreenSize = { screenW, screenH };

    float shortSide = (screenW < screenH) ? screenW : screenH;

    // グリッド全体の目標サイズから 1 マスを逆算する
    //   extent = cell * GRID + gap * (GRID - 1)、gap = cell * cellGapRatio
    //   → extent = cell * (GRID + cellGapRatio * (GRID - 1))
    float targetExtent = shortSide * gridScreenRatio;
    float denom = (float)GRID_SIZE + cellGapRatio * (float)(GRID_SIZE - 1);

    m_CellSize = targetExtent / denom;
    m_CellGap = m_CellSize * cellGapRatio;
    m_FramePad = m_CellSize * framePadRatio;

    float extent = GridExtent();
    float margin = shortSide * marginRatio;

    // 背景板は m_Origin - m_FramePad から描くので、その分を足しておく
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
// 指定マスの左上スクリーン座標
// ============================================================
Vector2 BackpackUI::CellPosition(int row, int col) const
{
    return {
        m_Origin.x + col * CellPitch(),
        m_Origin.y + row * CellPitch()
    };
}

// ============================================================
// スクリーン座標 → グリッド座標
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

    // 隙間の部分をクリックした場合は無効にする
    float inCellX = localX - col * pitch;
    float inCellY = localY - row * pitch;
    if (inCellX > m_CellSize || inCellY > m_CellSize) return false;

    outRow = row;
    outCol = col;
    return true;
}

// ============================================================
// 掴む
//
// グリッド上のブロックを押した場合はそれを持ち上げる。
// 何も無いマスを押した場合はパレットの選択から新規に作る。
// ============================================================
void BackpackUI::BeginDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    const bool frameMode = (m_EditMode == EditMode::Frame);

    int row = 0, col = 0;
    if (!ScreenToCell(mousePos, row, col)) return;

    const int existing = frameMode
        ? BackpackLogic::GetFrameAt(bp, row, col)
        : BackpackLogic::GetItemAt(bp, row, col);

    if (existing >= 0)
    {
        // ---- 既存ブロックを持ち上げる ----
        const auto& src = frameMode ? bp.frames[existing] : bp.items[existing];

        m_Drag.source = DragSource::Grid;
        m_Drag.id = src.id;
        m_Drag.rotation = src.rotation;
        m_Drag.originalIndex = existing;

        // 掴んだ場所を保つために、ブロック原点との差を覚える
        const Vector2 origin = CellPosition(src.row, src.col);
        m_Drag.grabOffset = { mousePos.x - origin.x, mousePos.y - origin.y };
    }
    else
    {
        // ---- パレットから新規 ----
        if (!m_HasSelection) return;
        if (ItemDatabase::IsFrame(m_SelectedItem) != frameMode) return;

        if (GetAvailableCount(bp, m_SelectedItem) <= 0) return;

        m_Drag.source = DragSource::Palette;
        m_Drag.id = m_SelectedItem;
        m_Drag.rotation = m_Rotation;
        m_Drag.originalIndex = -1;

        // 新規は掴んだマスがそのままアンカーになるようにする
        const Vector2 origin = CellPosition(row, col);
        m_Drag.grabOffset = { mousePos.x - origin.x, mousePos.y - origin.y };
    }

    UpdateDrag(bp, mousePos);
}

// ============================================================
// 運ぶ
//
// ブロック自体はピクセル単位で自由に動く。
// 落とし先はそのずれ量を最寄りのマスへ丸めて求める。
//
// 「最も面積が重なるマス」と「ずれ量を四捨五入したマス」は
// 同じ結果になる。形は剛体なので全ての占位格が同じだけずれ、
// 重なり面積の分布も全格で等しくなるため。
// 計算が単純な後者で実装している。
// ============================================================
void BackpackUI::UpdateDrag(BackpackComponent& bp, const Vector2& mousePos)
{
    if (m_Drag.source == DragSource::None) return;

    const float pitch = CellPitch();
    if (pitch <= 0.0f) return;

    // ブロック原点のスクリーン座標（掴んだ位置を保つ）
    const float ox = mousePos.x - m_Drag.grabOffset.x;
    const float oy = mousePos.y - m_Drag.grabOffset.y;

    // グリッド原点からのずれをマス数へ丸める
    m_Drag.dropCol = (int)std::lround((ox - m_Origin.x) / pitch);
    m_Drag.dropRow = (int)std::lround((oy - m_Origin.y) / pitch);

    // 置けるかどうか。既存ブロックの移動中は自分自身の占有を無視する。
    const int ignore = (m_Drag.source == DragSource::Grid) ? m_Drag.originalIndex : -1;

    m_Drag.canDrop = (m_EditMode == EditMode::Frame)
        ? BackpackLogic::CanPlaceFrame(bp, m_Drag.id,
            m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, ignore)
        : BackpackLogic::CanPlace(bp, m_Drag.id,
            m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, ignore);
}

// ============================================================
// 落とす
//
// 置けない場所で離した場合は何もしない。
// 近くの置ける場所を探して勝手にずらすことはしない。
// 「置いたつもりの場所と違う所に入る」方が混乱するため。
// ============================================================
void BackpackUI::EndDrag(BackpackComponent& bp)
{
    if (m_Drag.source == DragSource::None) return;

    const bool frameMode = (m_EditMode == EditMode::Frame);

    if (m_Drag.canDrop)
    {
        if (m_Drag.source == DragSource::Grid)
        {
            if (frameMode)
            {
                int evicted = 0;
                BackpackLogic::MoveFrame(bp, m_Drag.originalIndex,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation, &evicted);
                if (evicted > 0) m_LastEvicted = evicted;
            }
            else
            {
                // 魔法の移動。中断されないよう、一度消してから置く。
                BackpackLogic::Remove(bp, m_Drag.originalIndex);
                BackpackLogic::Place(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
            }
        }
        else
        {
            if (frameMode)
                BackpackLogic::PlaceFrame(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
            else
                BackpackLogic::Place(bp, m_Drag.id,
                    m_Drag.dropRow, m_Drag.dropCol, m_Drag.rotation);
        }
    }

    CancelDrag();

    // インデックスがずれているので、次のフレームで取り直す
    m_HoverItemIndex = m_HoverFrameIndex = -1;
}

// ============================================================
// マウス操作
//   左ドラッグ：掴んで運んで置く
//   右クリック：そのマスのブロックを取り出す
//   ホイール / R：回転（運んでいる最中も効く）
// ============================================================
void BackpackUI::HandleInput(BackpackComponent& bp)
{
    auto& input = InputManager::Get();

    // ImGui がマウスを使っている時は操作しない
    if (ImGui::GetIO().WantCaptureMouse)
    {
        // 運んでいる途中なら取り消す（中途半端な状態を残さない）
        CancelDrag();
        m_HoverRow = m_HoverCol = -1;
        m_HoverItemIndex = m_HoverFrameIndex = -1;
        return;
    }

    // 描画と同じ座標系（クライアント座標）を使う
    auto mp = input.GetMousePos();
    m_MousePos = { mp.x, mp.y };

    // ---- 乗っているマスとブロック ----
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
    // 運んでいる最中に回せた方が置きやすい
    float wheel = input.GetMouseWheel();
    int rotDelta = 0;
    if (wheel > 0.0f)      rotDelta = 1;
    else if (wheel < 0.0f) rotDelta = 3;
    if (input.GetKeyTrigger('R')) rotDelta = 1;

    if (rotDelta != 0)
    {
        m_Rotation = (m_Rotation + rotDelta) % 4;
        if (m_Drag.source != DragSource::None)
            m_Drag.rotation = (m_Drag.rotation + rotDelta) % 4;
    }

    // ---- ドラッグ ----
    if (m_Drag.source != DragSource::None)
    {
        UpdateDrag(bp, m_MousePos);

        if (!input.GetMousePress(0))   // 離した
            EndDrag(bp);

        return;   // 運んでいる間は他の操作をしない
    }

    if (input.GetMouseTrigger(0))
    {
        BeginDrag(bp, m_MousePos);
        return;
    }

    if (m_HoverRow < 0) return;

    // ---- 右クリック：取り出す ----
    // 枠を外すと、足場を失った魔法も一緒に外れる。
    // 外れた魔法は手元に戻る（グリッドから消えた時点で戻っている）。
    if (input.GetMouseTrigger(1))
    {
        if (m_EditMode == EditMode::Frame)
        {
            if (m_HoverFrameIndex >= 0)
                m_LastEvicted = BackpackLogic::RemoveFrame(bp, m_HoverFrameIndex);
        }
        else
        {
            if (m_HoverItemIndex >= 0)
                BackpackLogic::Remove(bp, m_HoverItemIndex);
        }

        m_HoverItemIndex = m_HoverFrameIndex = -1;
    }
}

// ============================================================
// 触れているブロックの影響格を描く
//
// 本体より「後」に描く。
// 常時表示だった頃は本体の下に敷いていたが、1つだけ描く方式では
// 別のブロックの本体に隠れて見えなくなる場合がある。
// 薄い色で上に重ねる方が確実。
// ============================================================
void BackpackUI::DrawHoverInfluence(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!showInfluenceOnHover) return;
    if (m_Drag.source != DragSource::None) return;   // 運んでいる時は出さない
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

    // ---- 影響を受けている側を光らせる ----
    // 影響格が覆っているマスの占有者を調べるだけ。
    // 集約側と同じく「どのブロックか」で数えるので、
    // 接触面が何マスあっても1回しか光らない。
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
// 落とし先の投影
//
// マス目に吸い付いた状態で描く。
// 運んでいる本体は自由に動くので、これが無いと
// どこに入るのか分からない。
// ============================================================
void BackpackUI::DrawDropShadow(SpriteRenderer& sprite)
{
    if (m_Drag.source == DragSource::None) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag.id);
    if (!c) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };
    const Vector4 col = m_Drag.canDrop ? Vector4(0.4f, 1.0f, 0.5f, 0.45f)
        : Vector4(1.0f, 0.3f, 0.3f, 0.45f);

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag.rotation);
    for (const auto& off : cells)
    {
        int r = m_Drag.dropRow + off.row;
        int cc = m_Drag.dropCol + off.col;
        if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

        sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, col);
    }

    // 魔法なら影響範囲も見せる。置く前に効果が分かるように。
    if (m_EditMode == EditMode::Spell && !c->influenceCells.empty())
    {
        Vector4 ic = c->color;
        ic.w = 0.25f;

        auto infl = BackpackLogic::RotateShape(c->influenceCells, m_Drag.rotation);
        for (const auto& off : infl)
        {
            int r = m_Drag.dropRow + off.row;
            int cc = m_Drag.dropCol + off.col;
            if (r < 0 || r >= GRID_SIZE || cc < 0 || cc >= GRID_SIZE) continue;

            sprite.Draw(m_BlockTex, CellPosition(r, cc), cellSizeVec, ic);
        }
    }
}

// ============================================================
// 運んでいるブロック本体
//
// マス目ではなくピクセル位置に描く。
// CellPosition は使わず、マウス位置から直接求める。
// ============================================================
void BackpackUI::DrawDragged(SpriteRenderer& sprite, const Vector2& mousePos)
{
    if (m_Drag.source == DragSource::None) return;

    const ItemCommon* c = ItemDatabase::GetCommon(m_Drag.id);
    if (!c) return;

    const float pitch = CellPitch();
    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // 掴んだ位置を保ったブロック原点
    const float ox = mousePos.x - m_Drag.grabOffset.x;
    const float oy = mousePos.y - m_Drag.grabOffset.y;

    auto icon = GetIcon(m_Drag.id);
    auto tex = icon ? icon : m_BlockTex;

    Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;
    col.w *= dragAlpha;

    auto cells = BackpackLogic::RotateShape(c->occupyCells, m_Drag.rotation);
    for (const auto& off : cells)
    {
        Vector2 pos = { ox + off.col * pitch, oy + off.row * pitch };
        sprite.Draw(tex, pos, cellSizeVec, col);
    }
}

// ============================================================
// 描画
// 描画順 = 重なり順
//   背景板 → マス（枠の有無で明暗）→ 枠の強調 → ブロック本体
//   → 影響 → 落とし先の投影 → 運んでいる本体
// ============================================================
void BackpackUI::Draw(SpriteRenderer& sprite, const BackpackComponent& bp)
{
    if (!m_BlockTex) return;

    const Vector2 cellSizeVec = { m_CellSize, m_CellSize };

    // ---- 背景板 ----
    float extent = GridExtent();
    Vector2 framePos = { m_Origin.x - m_FramePad, m_Origin.y - m_FramePad };
    Vector2 frameSize = { extent + m_FramePad * 2.0f, extent + m_FramePad * 2.0f };
    sprite.Draw(m_BlockTex, framePos, frameSize, frameColor);

    // ---- マス ----
    // 枠が敷かれているマスだけ明るく描く。
    // GRID は画布の上限であって、置ける場所ではないことを見た目で示す。
    for (int row = 0; row < GRID_SIZE; ++row)
    {
        for (int col = 0; col < GRID_SIZE; ++col)
        {
            const bool placeable = bp.IsPlaceable(row, col);
            sprite.Draw(m_BlockTex, CellPosition(row, col), cellSizeVec,
                placeable ? cellColor : lockedCellColor);
        }
    }

    // ---- 枠モードのとき、触れている枠の範囲を示す ----
    // どの枠を掴もうとしているのかが分からないと操作できない。
    if (m_EditMode == EditMode::Frame && m_Drag.source == DragSource::None &&
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

    // ---- ブロック本体（占位格）----
    // アイコンがあれば白で描く（画像そのままの色）、無ければ色染め
    // 運んでいるブロックはここでは描かない（マウス位置に描くため）
    const bool draggingFromGrid =
        (m_Drag.source == DragSource::Grid);
    const bool dragIsFrame = (m_EditMode == EditMode::Frame);

    for (size_t i = 0; i < bp.items.size(); ++i)
    {
        if (draggingFromGrid && !dragIsFrame && (int)i == m_Drag.originalIndex)
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

    // ---- 触れているブロックの影響（本体の上に重ねる）----
    if (m_EditMode == EditMode::Spell)
        DrawHoverInfluence(sprite, bp);

    // ---- 落とし先の投影 → 運んでいる本体 ----
    DrawDropShadow(sprite);
    DrawDragged(sprite, m_MousePos);
}
// ============================================================
// あと何個置けるか
//
// 所持数から、グリッド上に既に置いてある数を引く。
// 使用中の数を保持しないので、外した瞬間に自動で戻る。
// ============================================================
int BackpackUI::GetAvailableCount(const BackpackComponent& bp, ItemID id) const
{
    if (!m_Spellbook) return 999;   // 未設定ならデバッグ扱いで制限しない

    const int owned = m_Spellbook->GetCount(id);
    const int placed = ItemDatabase::IsFrame(id)
        ? BackpackLogic::CountPlacedFrames(bp, id)
        : BackpackLogic::CountPlaced(bp, id);

    return owned - placed;
}