// ============================================================
// BackpackPadControl.cpp
// ============================================================
#include "UI/BackpackPadControl.h"
#include "UI/BackpackUI.h"
#include "UI/SpellbookUI.h"
#include "UI/ShapeSprite.h"
#include "UI/UIDeco.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kStickThreshold = 0.5f;
    constexpr float kMouseWakePx = 2.0f;   // これ以上動いたらマウスへ戻す（捕獲を放した時の微小な動きでは戻さない）

    const WORD kPadButtons[] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT,
        XINPUT_GAMEPAD_START,
    };
}

// ============================================================
// 最後に触った機器
// ============================================================
bool BackpackPadControl::DetectDevice()
{
    if (m_HasTest) { m_Active = true; return true; }   // TEMP-TEST

    auto& input = InputManager::Get();

    bool pad = input.GetPadLeftStick().Length() > kStickThreshold;
    for (WORD b : kPadButtons)
        pad = pad || input.GetPadTrigger(b);

    const auto md = input.GetMouseDelta();
    const bool mouse = std::fabs(md.x) + std::fabs(md.y) > kMouseWakePx
        || input.GetMouseTrigger(0) || input.GetMouseTrigger(1) || input.GetMouseWheel() != 0.0f;

    if (pad)        m_Active = true;
    else if (mouse) m_Active = false;
    return m_Active;
}

// ============================================================
// 入力の読み取り
// 向きは十字優先、無ければスティックの強い方の軸だけ（斜めに滑らない）。
// 押した瞬間に 1 マス、押し続けると repeatDelay の後 repeatInterval ごとに 1 マス
// ============================================================
BackpackPadControl::Input BackpackPadControl::Read(float dt)
{
    Input in;
    if (m_HasTest)   // TEMP-TEST
    {
        in = m_Test;
        m_HasTest = false;
        return in;
    }

    auto& input = InputManager::Get();

    int x = 0, y = 0;
    if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_LEFT))       x = -1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_RIGHT)) x = 1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_UP))    y = -1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_DOWN))  y = 1;
    else
    {
        const Vector2 s = input.GetPadLeftStick();
        if (std::fabs(s.x) >= std::fabs(s.y)) { if (std::fabs(s.x) > kStickThreshold) x = (s.x > 0.0f) ? 1 : -1; }
        else if (std::fabs(s.y) > kStickThreshold) y = (s.y > 0.0f) ? -1 : 1;   // スティックの上 = 行は減る
    }

    if (x != m_HeldX || y != m_HeldY)
    {
        m_HeldX = x;
        m_HeldY = y;
        m_RepeatTimer = repeatDelay;
        in.dx = x;
        in.dy = y;
    }
    else if (x != 0 || y != 0)
    {
        m_RepeatTimer -= dt;
        if (m_RepeatTimer <= 0.0f)
        {
            m_RepeatTimer = repeatInterval;
            in.dx = x;
            in.dy = y;
        }
    }

    if (input.GetPadTrigger(XINPUT_GAMEPAD_A)) in.buttons |= kA;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_B)) in.buttons |= kB;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_X)) in.buttons |= kX;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_Y)) in.buttons |= kY;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_LEFT_SHOULDER))  in.buttons |= kLB;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER)) in.buttons |= kRB;
    return in;
}

// ============================================================
// Update
// ============================================================
bool BackpackPadControl::Update(BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag, float dt)
{
    const Input in = Read(dt);
    m_Rest += dt;

    // ---- Y：グリッド ⇔ 魔法書（掴んでいる間は替えない。箱が空なら移らない）----
    if ((in.buttons & kY) && !drag.IsActive())
    {
        if (m_Focus == Focus::Book)
            m_Focus = Focus::Grid;
        else if (book.GetBodyCount() > 0)
            m_Focus = Focus::Book;
        AudioSystem::Get().Play("ui_hover");
        m_Rest = 0.0f;
    }

    // 掴んだ物は必ずグリッドで運ぶ
    if (drag.IsActive()) m_Focus = Focus::Grid;

    if (m_Focus == Focus::Book)
        return UpdateBook(in, bp, grid, book, drag);

    book.SetPadSelection(0);
    return UpdateGrid(in, bp, grid, drag);
}

// ============================================================
// グリッド
// カーソルのマスの中心を仮のマウス位置にして、BackpackUI のドラッグ処理へ渡す
// ============================================================
bool BackpackPadControl::UpdateGrid(const Input& in, BackpackComponent& bp, BackpackUI& grid, DragContext& drag)
{
    auto& audio = AudioSystem::Get();
    const int last = BackpackUI::GRID_SIZE - 1;

    const int r0 = m_Row, c0 = m_Col;
    m_Row = (std::clamp)(m_Row + in.dy, 0, last);
    m_Col = (std::clamp)(m_Col + in.dx, 0, last);
    if (m_Row != r0 || m_Col != c0)
    {
        m_Rest = 0.0f;
        audio.Play("ui_hover");
    }

    const float cell = grid.m_CellSize;
    const float pitch = grid.CellPitch();
    const Vector2 half = { cell * 0.5f, cell * 0.5f };
    const Vector2 cellPos = grid.CellPosition(m_Row, m_Col);
    const Vector2 vm = cellPos + half;

    grid.m_MousePos = vm;
    grid.m_HoverRow = m_Row;
    grid.m_HoverCol = m_Col;
    m_TooltipAnchor = { cellPos.x + cell * 0.7f, cellPos.y + cell * 0.7f };

    // ---- 掴んでいる間 ----
    if (drag.IsActive())
    {
        grid.m_HoverItemIndex = grid.m_HoverFrameIndex = -1;
        m_Rest = 0.0f;

        const int rot = (in.buttons & kRB) ? 1 : ((in.buttons & kLB) ? 3 : 0);
        if (rot != 0)
        {
            // 掴んでいるマスがカーソルの下に残るよう、掴んだずれ（アンカーからのマス数）も同じだけ回す
            CellOffset g;
            g.row = (int)std::lround((drag.grabOffset.y - half.y) / pitch);
            g.col = (int)std::lround((drag.grabOffset.x - half.x) / pitch);
            g = BackpackLogic::RotateShape({ g }, rot)[0];
            drag.grabOffset = { (float)g.col * pitch + half.x, (float)g.row * pitch + half.y };
            drag.rotation = (drag.rotation + rot) % 4;
            grid.m_Rotation = (grid.m_Rotation + rot) % 4;
            audio.Play("item_rotate");
        }

        grid.UpdateDrag(bp, vm);

        if (in.buttons & kA)
        {
            // 置けない所では置かない（持ったまま）。マウスの「離したら手元へ戻る」とは変えてある
            if (drag.canDrop) grid.EndDrag(bp);
            else              audio.Play("gold_deny");
        }
        else if (in.buttons & kX)
        {
            // 魔法書へ戻す：置けない所で離したのと同じ扱い
            drag.canDrop = false;
            grid.EndDrag(bp);
        }
        else if (in.buttons & kB)
        {
            grid.CancelDrag();   // データは書き換えていないので、元の位置に残る
            audio.Play("item_return");
        }
        return false;
    }

    // ---- 何も掴んでいない ----
    grid.m_HoverItemIndex = BackpackLogic::GetItemAt(bp, m_Row, m_Col);
    grid.m_HoverFrameIndex = BackpackLogic::GetFrameAt(bp, m_Row, m_Col);

    if (in.buttons & kA)
    {
        grid.BeginDrag(bp, vm);   // 魔法 → 枠の順。空のマスなら何も起きない
        if (drag.IsActive()) audio.Play("item_pick");
    }
    else if (in.buttons & kX)
    {
        if (grid.m_HoverItemIndex >= 0)
        {
            audio.Play("item_return");
            BackpackLogic::Remove(bp, grid.m_HoverItemIndex);
        }
        else if (grid.m_HoverFrameIndex >= 0)
        {
            audio.Play("item_return");
            grid.m_LastEvicted = BackpackLogic::RemoveFrame(bp, grid.m_HoverFrameIndex);
        }
        grid.m_HoverItemIndex = grid.m_HoverFrameIndex = -1;
    }
    else if (in.buttons & kB)
    {
        return true;
    }
    return false;
}

// ============================================================
// 魔法書の箱
// ============================================================
bool BackpackPadControl::UpdateBook(const Input& in, BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag)
{
    grid.m_HoverRow = grid.m_HoverCol = -1;
    grid.m_HoverItemIndex = grid.m_HoverFrameIndex = -1;

    // ---- 選択の確認：消えていたら（数が減った・取り出した）一番上にある物へ ----
    Vector2 selPos = { 0.0f, 0.0f };
    bool found = false;
    uint32_t top = 0;
    float topY = 1.0e9f;
    for (int i = 0; i < book.GetBodyCount(); ++i)
    {
        uint32_t uid = 0;
        Vector2 pos;
        if (!book.GetBodyInfo(i, uid, pos)) continue;
        if (uid == m_BookSel) { found = true; selPos = pos; }
        if (pos.y < topY) { topY = pos.y; top = uid; }
    }
    if (!found)
    {
        m_BookSel = top;
        m_Rest = 0.0f;
        if (m_BookSel == 0)   // 箱が空
        {
            m_Focus = Focus::Grid;
            book.SetPadSelection(0);
            return (in.buttons & kB) != 0;
        }
    }

    if (in.dx != 0 || in.dy != 0)
    {
        const uint32_t next = PickBookNeighbor(book, in.dx, in.dy);
        if (next != 0 && next != m_BookSel)
        {
            m_BookSel = next;
            m_Rest = 0.0f;
            AudioSystem::Get().Play("ui_hover");
        }
    }
    book.SetPadSelection(m_BookSel);

    for (int i = 0; i < book.GetBodyCount(); ++i)
    {
        uint32_t uid = 0;
        Vector2 pos;
        if (book.GetBodyInfo(i, uid, pos) && uid == m_BookSel) selPos = pos;
    }
    m_TooltipAnchor = { selPos.x + grid.m_CellSize * 0.3f, selPos.y + grid.m_CellSize * 0.3f };

    if (in.buttons & kA)
    {
        if (book.GrabByUid(m_BookSel))
        {
            // 形の真ん中のマスをカーソルの下に持つ（枠のようにアンカーが角にある物でも中心で運べる）
            const float cell = grid.m_CellSize;
            const float pitch = grid.CellPitch();
            if (const ItemCommon* c = ItemDatabase::GetCommon(drag.id); c && !c->occupyCells.empty())
            {
                const CellOffset cc = ShapeSprite::CenterCell(c->occupyCells);
                drag.grabOffset = { (float)cc.col * pitch + cell * 0.5f, (float)cc.row * pitch + cell * 0.5f };
            }

            m_Focus = Focus::Grid;
            m_BookSel = 0;
            book.SetPadSelection(0);

            const Vector2 vm = grid.CellPosition(m_Row, m_Col) + Vector2(cell * 0.5f, cell * 0.5f);
            grid.m_MousePos = vm;
            grid.UpdateDrag(bp, vm);
        }
    }
    else if (in.buttons & kB)
    {
        return true;
    }
    return false;
}

// ============================================================
// 箱の中で、今の物から (dx, dy) の向きに一番近い物
// 向きに沿った距離 + 横ずれ × 2 が最小の物。その向きに何も無ければ 0
// ============================================================
uint32_t BackpackPadControl::PickBookNeighbor(const SpellbookUI& book, int dx, int dy) const
{
    Vector2 from = { 0.0f, 0.0f };
    bool found = false;
    for (int i = 0; i < book.GetBodyCount(); ++i)
    {
        uint32_t uid = 0;
        Vector2 pos;
        if (book.GetBodyInfo(i, uid, pos) && uid == m_BookSel) { from = pos; found = true; }
    }
    if (!found) return 0;

    const Vector2 dir = { (float)dx, (float)dy };
    uint32_t best = 0;
    float bestScore = 1.0e9f;
    for (int i = 0; i < book.GetBodyCount(); ++i)
    {
        uint32_t uid = 0;
        Vector2 pos;
        if (!book.GetBodyInfo(i, uid, pos) || uid == m_BookSel) continue;

        const Vector2 d = pos - from;
        const float along = d.Dot(dir);
        if (along <= 4.0f) continue;
        const float side = (d - dir * along).Length();
        const float score = along + side * 2.0f;
        if (score < bestScore) { bestScore = score; best = uid; }
    }
    return best;
}

// ============================================================
// 描画：グリッドのカーソルと操作の案内
// 箱の中の選択は SpellbookUI が描く（明るい縁で少し浮かせる）
// ============================================================
void BackpackPadControl::Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid,
    const DragContext& drag, const Vector2& screen) const
{
    const float k = (std::min)(screen.x, screen.y) / 900.0f * UIDeco::UIScale();
    const bool carrying = drag.IsActive();

    // ---- カーソル（明滅する金の枠）----
    if (m_Focus == Focus::Grid)
    {
        const float cell = grid.m_CellSize;
        const float t = (std::max)(2.0f, cell * 0.07f);
        const float pulse = 0.70f + 0.30f * std::sin(UIDeco::Clock() * 6.0f);

        Vector4 col = UIDeco::TintColor(UIDeco::Tint::Gold);
        col.x *= 1.6f; col.y *= 1.6f; col.z *= 1.6f;
        col.w = (carrying ? 0.75f : 1.0f) * pulse;

        const Vector2 p = grid.CellPosition(m_Row, m_Col);
        UIDeco::DrawFrameLines(sprite, { p.x - t, p.y - t }, { cell + t * 2.0f, cell + t * 2.0f }, col, t);
    }

    // ---- 操作の案内（グリッドの外枠の下）----
    const wchar_t* hint = L"A  つかむ　　X  魔法書へ戻す　　Y  魔法書へ移る　　B  閉じる";
    if (carrying)
        hint = L"A  置く　　LB / RB  回転　　X  魔法書へ戻す　　B  やめる";
    else if (m_Focus == Focus::Book)
        hint = L"A  取り出す　　Y  バックパックへ移る　　B  閉じる";

    const float scale = 0.5f * k;
    const float pad = 8.0f * k;
    const Vector2 size = text.Measure(hint, scale);
    const Vector2 pos = {
        grid.m_Origin.x - grid.m_FramePad + pad,
        grid.m_Origin.y + grid.GridExtent() + grid.m_FramePad + 10.0f * k + pad
    };
    // 草地の上でも読めるよう暗い帯を敷く（線形の値。HDR バッファでは薄い黒はほとんど効かない）
    if (const auto& white = UIDeco::Tex().white)
        sprite.Draw(white, { pos.x - pad, pos.y - pad }, { size.x + pad * 2.0f, size.y + pad * 2.0f },
            { 0.0f, 0.0f, 0.0f, 0.9f });
    text.Draw(hint, { pos.x + 2.0f, pos.y + 2.0f }, { 0.0f, 0.0f, 0.0f, 0.8f }, scale);
    text.Draw(hint, pos, { 0.95f, 0.88f, 0.70f, 1.0f }, scale);
}
