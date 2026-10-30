// ============================================================
// BackpackPadControl.cpp
// ============================================================
#include "UI/BackpackPadControl.h"
#include "UI/BackpackUI.h"
#include "UI/SpellbookUI.h"
#include "UI/ShapeSprite.h"
#include "UI/UIDeco.h"
#include "UI/InputGlyph.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/InputManager.h"
#include "Manager/ResourceManager.h"
#include "Graphics/Material/Texture.h"
#include "Audio/AudioSystem.h"

using namespace DirectX::SimpleMath;

namespace
{
    // カーソルの矢印の絵（コードで作る。白い塗り + 暗い縁、透明度は縁の外へ滑らかに）。
    // 先端（ホットスポット）は kPointerTip。最初に使う時に一度だけ作る
    constexpr int kPointerW = 48, kPointerH = 64;
    const Vector2 kPointerTip = { 5.0f, 4.0f };

    std::shared_ptr<Texture> PointerTexture()
    {
        static std::shared_ptr<Texture> tex;
        static bool made = false;
        if (made) return tex;
        made = true;
        ID3D11Device* device = ResourceManager::Get().GetDevice();
        if (!device) return nullptr;

        // よくある矢印カーソルの形（12 × 20 の升目で描いて 2.6 倍）
        const Vector2 shape[] = { { 0, 0 }, { 0, 17 }, { 4, 13 }, { 7, 20 }, { 10, 19 }, { 7, 12 }, { 12, 12 } };
        constexpr int n = (int)(sizeof(shape) / sizeof(shape[0]));
        Vector2 poly[n];
        for (int i = 0; i < n; ++i) poly[i] = kPointerTip + shape[i] * 2.6f;
        constexpr float outline = 3.5f;   // 暗い縁の太さ（絵の px）

        std::vector<uint8_t> px(kPointerW * kPointerH * 4, 0);
        for (int y = 0; y < kPointerH; ++y)
            for (int x = 0; x < kPointerW; ++x)
            {
                const Vector2 p = { x + 0.5f, y + 0.5f };
                bool inside = false;
                float dist = 1.0e9f;
                for (int i = 0, j = n - 1; i < n; j = i++)
                {
                    const Vector2 a = poly[j], b = poly[i];
                    if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
                        inside = !inside;
                    const Vector2 ab = b - a;
                    const float t = std::clamp((p - a).Dot(ab) / (std::max)(ab.LengthSquared(), 1.0e-6f), 0.0f, 1.0f);
                    dist = (std::min)(dist, (p - (a + ab * t)).Length());
                }
                const float s = inside ? dist : -dist;                                  // 内側が正
                const float alpha = std::clamp(s + outline + 0.5f, 0.0f, 1.0f);         // 縁の外側
                const float fill = std::clamp(s - outline * 0.35f + 0.5f, 0.0f, 1.0f);  // 塗り（縁の内側）
                const float v = 0.06f + 0.94f * fill;
                uint8_t* o = &px[(y * kPointerW + x) * 4];
                o[0] = o[1] = o[2] = (uint8_t)(v * 255.0f + 0.5f);
                o[3] = (uint8_t)(alpha * 255.0f + 0.5f);
            }
        tex = std::make_shared<Texture>();
        if (!tex->CreateFromMemory(device, px.data(), kPointerW, kPointerH)) tex.reset();
        return tex;
    }
}

// ============================================================
// 最後に触った機器
// ============================================================
bool BackpackPadControl::DetectDevice()
{
    if (m_HasTest || m_TestMode) { m_Active = true; return true; }   // TEMP-TEST

    // 判定は InputManager にまとめた（2026-10-09。操作案内の絵の出し分けと同じ機器を見る）
    m_Active = InputManager::Get().IsUsingPad();
    return m_Active;
}

// ============================================================
// 入力の読み取り
// 十字は押した瞬間に 1 マス、押し続けると repeatDelay の後 repeatInterval ごとに 1 マス。
// スティックはそのまま（カーソルの速さは MoveCursor が決める）
// ============================================================
BackpackPadControl::Input BackpackPadControl::Read(float dt)
{
    Input in;
    if (m_HasTest)   // TEMP-TEST
    {
        in = m_Test;
        in.boost = (in.buttons & kX) != 0;   // X = 押している間の加速
        m_HasTest = false;
        return in;
    }

    auto& input = InputManager::Get();

    int x = 0, y = 0;
    if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_LEFT))       x = -1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_RIGHT)) x = 1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_UP))    y = -1;
    else if (input.GetPadPress(XINPUT_GAMEPAD_DPAD_DOWN))  y = 1;

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

    in.stick = input.GetPadLeftStick();
    in.boost = input.GetPadPress(XINPUT_GAMEPAD_X);

    if (input.GetPadTrigger(XINPUT_GAMEPAD_A)) in.buttons |= kA;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_B)) in.buttons |= kB;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_Y)) in.buttons |= kY;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_LEFT_SHOULDER))  in.buttons |= kLB;
    if (input.GetPadTrigger(XINPUT_GAMEPAD_RIGHT_SHOULDER)) in.buttons |= kRB;
    return in;
}

// グリッドの外枠の内側（マスの隙間・縁も含む）にカーソルがあるか
bool BackpackPadControl::InGridRect(const BackpackUI& grid) const
{
    const float pad = grid.m_FramePad;
    const float e = grid.GridExtent();
    return m_Cursor.x >= grid.m_Origin.x - pad && m_Cursor.x <= grid.m_Origin.x + e + pad
        && m_Cursor.y >= grid.m_Origin.y - pad && m_Cursor.y <= grid.m_Origin.y + e + pad;
}

// ============================================================
// カーソルを動かす
// スティック：速さ = cursorSpeed（マス/秒）× 倒した量 ^ cursorCurve（B で × boostMul）。
// 十字：グリッドの上なら今のマスから 1 マス隣の真ん中へ（細かく合わせたい時）、外なら 1 マス分
// ============================================================
void BackpackPadControl::MoveCursor(const Input& in, const BackpackUI& grid, float dt, const Vector2& screen)
{
    const float pitch = grid.CellPitch();
    const float half = grid.m_CellSize * 0.5f;

    if (in.dx != 0 || in.dy != 0)
    {
        if (InGridRect(grid) && pitch > 0.0f)
        {
            const int last = BackpackUI::GRID_SIZE - 1;
            int col = (int)std::floor((m_Cursor.x - grid.m_Origin.x) / pitch);
            int row = (int)std::floor((m_Cursor.y - grid.m_Origin.y) / pitch);
            col = (std::clamp)(col + in.dx, 0, last);
            row = (std::clamp)(row + in.dy, 0, last);
            m_Cursor = grid.CellPosition(row, col) + Vector2(half, half);
        }
        else
        {
            m_Cursor.x += (float)in.dx * pitch;
            m_Cursor.y += (float)in.dy * pitch;
        }
    }

    const float mag = in.stick.Length();
    if (mag > 1.0e-4f)
    {
        float speed = cursorSpeed * pitch * std::pow((std::min)(mag, 1.0f), cursorCurve);
        if (in.boost) speed *= boostMul;
        m_Cursor.x += in.stick.x / mag * speed * dt;
        m_Cursor.y -= in.stick.y / mag * speed * dt;   // スティックの上 = 画面の上
    }

    m_Cursor.x = (std::clamp)(m_Cursor.x, 0.0f, (std::max)(0.0f, screen.x - 1.0f));
    m_Cursor.y = (std::clamp)(m_Cursor.y, 0.0f, (std::max)(0.0f, screen.y - 1.0f));
}

// ============================================================
// Update
// カーソルの位置を仮のマウス位置にして、BackpackUI のドラッグ処理と魔法書の掴みへ渡す
// ============================================================
void BackpackPadControl::Update(BackpackComponent& bp, BackpackUI& grid, SpellbookUI& book, DragContext& drag, float dt,
    const Vector2& screen)
{
    auto& audio = AudioSystem::Get();
    const Input in = Read(dt);

    const float cell = grid.m_CellSize;
    const float pitch = grid.CellPitch();
    const Vector2 half = { cell * 0.5f, cell * 0.5f };
    const Vector2 gridCenter = grid.CellPosition(BackpackUI::GRID_SIZE / 2, BackpackUI::GRID_SIZE / 2) + half;
    if (!m_HasCursor) { m_Cursor = gridCenter; m_HasCursor = true; }
    const Vector2 before = m_Cursor;

    // ---- Y：カーソルをグリッド ⇔ 魔法書の箱へ（掴んだままでも飛べる）----
    if (in.buttons & kY)
    {
        if (InGridRect(grid))
        {
            m_GridReturn = m_Cursor;
            m_HasGridReturn = true;
            // 箱の真ん中に一番近い物の上（箱が空なら箱の真ん中）
            Vector2 target = book.BoxCenter();
            float best = 1.0e18f;
            for (int i = 0; i < book.GetBodyCount(); ++i)
            {
                uint32_t uid = 0;
                Vector2 pos;
                if (!book.GetBodyInfo(i, uid, pos)) continue;
                const float d = (pos - book.BoxCenter()).LengthSquared();
                if (d < best) { best = d; target = pos; }
            }
            m_Cursor = target;
        }
        else
        {
            m_Cursor = m_HasGridReturn ? m_GridReturn : gridCenter;
        }
        audio.Play("ui_hover");
    }

    MoveCursor(in, grid, dt, screen);
    if ((m_Cursor - before).LengthSquared() > 0.25f) m_Rest = 0.0f;
    else m_Rest += dt;

    // ---- カーソルの下 ----
    grid.m_MousePos = m_Cursor;
    if (!grid.ScreenToCell(m_Cursor, m_HoverRow, m_HoverCol))
        m_HoverRow = m_HoverCol = -1;
    grid.m_HoverRow = m_HoverRow;
    grid.m_HoverCol = m_HoverCol;
    m_BookSel = (!drag.IsActive() && m_HoverRow < 0) ? book.BodyAt(m_Cursor) : 0;
    book.SetPadSelection(m_BookSel);
    book.SetPadPointer(m_Cursor);
    m_TooltipAnchor = { m_Cursor.x + cell * 0.5f, m_Cursor.y + cell * 0.68f };   // 矢印の絵（高さ 0.62 マス）に被らない所

    // ---- 掴んでいる間 ----
    if (drag.IsActive())
    {
        grid.m_HoverItemIndex = grid.m_HoverFrameIndex = -1;

        const int rot = (in.buttons & kRB) ? 1 : ((in.buttons & kLB) ? 3 : 0);
        if (rot != 0)
        {
            // 掴んでいる所がカーソルの下に残るよう、掴んだずれ（要のマスの真ん中から、マス単位）も同じだけ回す：
            // (行, 列) → (列, −行)（BackpackLogic::RotateShape と同じ）
            Vector2 g = (pitch > 0.0f) ? (drag.grabOffset - half) / pitch : Vector2(0.0f, 0.0f);   // x = 列, y = 行
            for (int i = 0; i < rot; ++i) g = Vector2(-g.y, g.x);
            drag.grabOffset = g * pitch + half;
            drag.rotation = (drag.rotation + rot) % 4;
            grid.m_Rotation = (grid.m_Rotation + rot) % 4;
            audio.Play("item_rotate");
        }

        grid.UpdateDrag(bp, m_Cursor);

        if (in.buttons & kA)
        {
            // 置ける → 置く。グリッドの上で置けない所 → 持ったまま（マウスの「離したら魔法書へ」とは変えてある）。
            // グリッドの外 → 魔法書へ戻して、その場から落とす（マウスで外へ離したのと同じ）
            if (drag.canDrop)
                grid.EndDrag(bp);
            else if (InGridRect(grid))
                audio.Play("gold_deny");
            else
                grid.EndDrag(bp);
        }
        else if (in.buttons & kB)
        {
            // 外す：魔法書へ戻す（置けない所で離したのと同じ扱い。カーソルの所から落ちる）
            drag.canDrop = false;
            grid.EndDrag(bp);
        }
        return;
    }

    // ---- 何も掴んでいない ----
    grid.m_HoverItemIndex = (m_HoverRow >= 0) ? BackpackLogic::GetItemAt(bp, m_HoverRow, m_HoverCol) : -1;
    grid.m_HoverFrameIndex = (m_HoverRow >= 0) ? BackpackLogic::GetFrameAt(bp, m_HoverRow, m_HoverCol) : -1;

    if (in.buttons & kA)
    {
        if (m_HoverRow >= 0)
        {
            grid.BeginDrag(bp, m_Cursor);   // 魔法 → 枠の順。空のマスなら何も起きない
            if (drag.IsActive()) audio.Play("item_pick");
        }
        else if (m_BookSel != 0 && book.GrabByUid(m_BookSel))   // 掴む音は SpellbookUI が鳴らす
        {
            // 形の真ん中のマスをカーソルの下に持つ（枠のようにアンカーが角にある物でも中心で運べる）
            if (const ItemCommon* c = ItemDatabase::GetCommon(drag.id); c && !c->occupyCells.empty())
            {
                const CellOffset cc = ShapeSprite::CenterCell(c->occupyCells);
                drag.grabOffset = { (float)cc.col * pitch + half.x, (float)cc.row * pitch + half.y };
            }
            m_BookSel = 0;
            book.SetPadSelection(0);
            grid.UpdateDrag(bp, m_Cursor);
        }
    }
    else if ((in.buttons & kB) && m_HoverRow >= 0)
    {
        // 素早く外す：カーソルの下の魔法（無ければ枠）を魔法書へ戻す
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
}

// ============================================================
// 描画：指しているマスの枠・カーソル・操作の案内
// 箱の中のカーソルの下の物は SpellbookUI が描く（明るい縁で少し浮かせる）
// ============================================================
void BackpackPadControl::Draw(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid,
    const DragContext& drag, const Vector2& screen) const
{
    const float k = (std::min)(screen.x, screen.y) / 900.0f * UIDeco::UIScale();
    const bool carrying = drag.IsActive();
    const float cell = grid.m_CellSize;
    const float pulse = 0.70f + 0.30f * std::sin(UIDeco::Clock() * 6.0f);

    Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    gold.x *= 1.6f; gold.y *= 1.6f; gold.z *= 1.6f;

    // ---- 指しているマス（明滅する金の枠。掴んでいない時だけ。掴んでいる時は置き先の影が出る）----
    if (!carrying && m_HoverRow >= 0)
    {
        const float t = (std::max)(2.0f, cell * 0.07f);
        Vector4 col = gold;
        col.w = pulse;
        const Vector2 p = grid.CellPosition(m_HoverRow, m_HoverCol);
        UIDeco::DrawFrameLines(sprite, { p.x - t, p.y - t }, { cell + t * 2.0f, cell + t * 2.0f }, col, t);
    }

    // カーソルの矢印は DrawPointer（GameUI が説明カードの後に描く）

    // ---- 操作の案内（グリッドの外枠の下）----
    // {…} = パッドのボタンの絵（InputGlyph。2026-10-09 から文字の「A」などを絵に）
    const bool inGrid = InGridRect(grid);
    const wchar_t* hint = inGrid
        ? L"{LS} 動かす（{X} で速く）　　{A} つかむ　　{B} 外す　　{Y} 魔法書へ移る　　{START} 閉じる"
        : L"{LS} 動かす（{X} で速く）　　{A} つかむ　　{Y} バックパックへ移る　　{START} 閉じる";
    if (carrying)
        hint = inGrid
            ? L"{LS} 動かす（{X} で速く）　　{A} 置く　　{B} 魔法書へ戻す　　{LB} / {RB} 回転"
            : L"{LS} 動かす（{X} で速く）　　{A} 魔法書へ戻す　　{LB} / {RB} 回転　　{Y} バックパックへ移る";

    DrawHintBar(sprite, text, grid, hint, k);
}

// ============================================================
// カーソル（金の矢印 + 暗い縁 + 落ち影。先端がカーソルの位置）
// ============================================================
void BackpackPadControl::DrawPointer(SpriteRenderer& sprite, const BackpackUI& grid, const Vector2& screen) const
{
    const auto ptr = PointerTexture();
    if (!ptr) return;
    const float k = (std::min)(screen.x, screen.y) / 900.0f * UIDeco::UIScale();
    const float h = grid.m_CellSize * 0.62f;
    const float scale = h / (float)kPointerH;
    const Vector2 size = { (float)kPointerW * scale, h };
    const Vector2 pos = m_Cursor - kPointerTip * scale;
    sprite.Draw(ptr, pos + Vector2(2.0f, 3.0f) * k, size, { 0.0f, 0.0f, 0.0f, 0.45f });   // 落ち影
    Vector4 col = UIDeco::TintColor(UIDeco::Tint::Gold);
    col.x = 0.35f + col.x * 1.3f; col.y = 0.35f + col.y * 1.3f; col.z = 0.35f + col.z * 1.3f;   // 明るい金（白寄り）
    col.w = 1.0f;
    sprite.Draw(ptr, pos, size, col);
}

// ============================================================
// マウスで操作している間の案内（2026-10-09。パッドの案内と同じ場所・同じ帯）
// ============================================================
void BackpackPadControl::DrawMouseHint(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid,
    const DragContext& drag, const Vector2& screen)
{
    const float k = (std::min)(screen.x, screen.y) / 900.0f * UIDeco::UIScale();
    const wchar_t* hint = drag.IsActive()
        ? L"{M:L} 離して置く（外なら魔法書へ）　　{M:WHEEL} / {K:R} 回転"
        : L"{M:L} ドラッグで動かす　　{M:WHEEL} / {K:R} 回転　　{M:R} 魔法書へ戻す　　{K:Tab} 閉じる";
    DrawHintBar(sprite, text, grid, hint, k);
}

// 操作の案内の帯（グリッドの外枠の下）
void BackpackPadControl::DrawHintBar(SpriteRenderer& sprite, TextRenderer& text, const BackpackUI& grid,
    const wchar_t* hint, float k)
{
    const float scale = 0.5f * k;
    const float pad = 8.0f * k;
    const Vector2 size = InputGlyph::Measure(text, hint, scale);
    const Vector2 pos = {
        grid.m_Origin.x - grid.m_FramePad + pad,
        grid.m_Origin.y + grid.GridExtent() + grid.m_FramePad + 10.0f * k + pad
    };
    // 草地の上でも読めるよう暗い帯を敷く（線形の値。HDR バッファでは薄い黒はほとんど効かない）
    if (const auto& white = UIDeco::Tex().white)
        sprite.Draw(white, { pos.x - pad, pos.y - pad }, { size.x + pad * 2.0f, size.y + pad * 2.0f },
            { 0.0f, 0.0f, 0.0f, 0.9f });
    InputGlyph::DrawShadowed(sprite, text, hint, pos, { 0.95f, 0.88f, 0.70f, 1.0f }, scale,
        { 2.0f, 2.0f }, { 0.0f, 0.0f, 0.0f, 0.8f });
}
