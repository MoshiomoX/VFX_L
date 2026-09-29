// ============================================================
// GameUI.cpp
// ============================================================
#include "UI/GameUI.h"
#include "ECS/Registry.h"
#include "ECS/System/BackpackAggregateSystem.h"
#include "UI/LevelUpSystem.h"
#include "Item/ItemDatabase.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemInfo.h"
#include "Component/BackpackComponent.h"
#include "Component/SpellbookComponent.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Player/LevelComponent.h"
#include "Player/PlayerStateComponent.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputMap.h"
#include "Manager/InputManager.h"
#include "Graphics/Material/Texture.h"
#include "ResourcePaths.h"
#include "imgui.h"
#include <algorithm>
#include <iostream>

// ============================================================
// 初期化
// ============================================================
bool GameUI::Initialize(ID3D11Device* device, ID3D11DeviceContext* context,
    float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;

    if (!m_Sprite.Initialize(device, context, 4096))
    {
        std::cout << "[Error] GameUI: SpriteRenderer init failed" << std::endl;
        return false;
    }
    m_Sprite.SetScreenSize(screenW, screenH);

    if (!m_Text.Initialize(device, context, Res::Fnt::JP))
    {
        std::cout << "[Error] GameUI: TextRenderer init failed" << std::endl;
        return false;
    }

    auto blockTex = ResourceManager::Get().LoadTexture(Res::Tex::BlockSolo);

    // ブロックの貼图は縁に陰影があるので、箱や線は無地の白で描く
    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
        m_WhiteTex = blockTex;

    m_Backpack.Initialize(blockTex);
    m_Backpack.LoadIcons();
    m_Backpack.Layout(screenW, screenH);
	m_Backpack.SetDragContext(&m_Drag);

    m_Spellbook.Initialize(blockTex);
    m_Spellbook.LoadIcons();
    m_Spellbook.Layout(screenW, screenH);
    m_Spellbook.SetDragContext(&m_Drag);
    m_Spellbook.SetCellSize(m_Backpack.GetCellSize());

    m_LevelUp.Initialize(blockTex);
    m_LevelUp.SetWhiteTexture(m_WhiteTex);
    m_LevelUp.LoadIcons();
    m_LevelUp.Layout(screenW, screenH);

    if (!m_HUD.Initialize(device))
    {
        std::cout << "[Error] GameUI: HUD init failed" << std::endl;
        return false;
    }
    m_HUD.Layout(screenW, screenH);
    m_HUD.SetIconLookup([this](ItemID id) { return m_Backpack.FindIcon(id); });

    m_Pause.Layout(screenW, screenH);

    return true;
}

void GameUI::Shutdown()
{
    m_Text.Shutdown();
    m_Sprite.Shutdown();
}

// ============================================================
// レイアウトの組み直し
// UI は全部ピクセル指定なので、サイズが変わった時だけ呼ばれる
// ============================================================
void GameUI::Layout(float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;

    m_Sprite.SetScreenSize(screenW, screenH);
    m_Backpack.Layout(screenW, screenH);

    m_Spellbook.Layout(screenW, screenH);
    m_Spellbook.SetCellSize(m_Backpack.GetCellSize());

    m_LevelUp.Layout(screenW, screenH);
    m_HUD.Layout(screenW, screenH);
    m_Pause.Layout(screenW, screenH);
}

// ============================================================
// 一時停止の判定
// 三択は必ず止める。
// グリッドだけは「開いたまま戦況を見る」を許すか選べる。
// ============================================================
bool GameUI::ShouldPauseGame() const
{
    return m_Stack.ShouldPauseGame()
        && !(m_Stack.Top() == UILayer::Backpack && !m_PauseOnBackpack);
}

// ============================================================
// Update
// ============================================================
void GameUI::Update(Registry& reg, Entity player, float dt)
{
    // ============================================================
    // プレイヤーが消えたら、開いている UI を全部下ろす。
    // モーダルはプレイヤーのデータを前提に描いている。
    // 持ち主が居ないまま残すと参照だけが宙に浮き、
    // ゲームが止まったまま操作不能になるのが最悪の形。
    // ============================================================
    if (!reg.IsValid(player))
    {
        m_Stack.Clear();
        return;
    }

    // 死んだら何も開かせない（「力尽きた」の幕 → リザルトへ進むだけ）
    if (reg.Has<PlayerStateComponent>(player) && reg.Get<PlayerStateComponent>(player).IsDead())
    {
        m_Stack.Clear();
        return;
    }

    // 魔法書の参照は毎フレーム取り直す。
    // ※Registry の再確保で古いポインタが無効になるため、
    //   Init で一度だけ渡す形にはしない。
    if (reg.Has<SpellbookComponent>(player))
        m_Backpack.SetSpellbook(&reg.Get<SpellbookComponent>(player));

    UpdateStack(reg, player, dt);
}

// ============================================================
// スタックの更新
// 1. 割り込みの受け付け
// 2. 開閉の切り替え
// 3. 一番上の UI にだけ入力を渡す
// 優先度の表はここには書かない。スタック順がそのまま優先度。
// ============================================================
void GameUI::UpdateStack(Registry& reg, Entity player, float dt)
{
    // ---- 1. 三択の割り込み ----
    // 二重に積まれないよう、Push 側が既存を弾いてくれる
    if (LevelUpSystem::IsAnyoneChoosing(reg))
        m_Stack.Push(UILayer::LevelUp, UIManager::CloseMode::Forced);

    // ---- 1b. 一時停止のメニュー（P / パッド Back）----
    // 三択の上には開かない（選ぶまで閉じられない物の上に積むと戻り方がややこしい）。
    // グリッドの上には開ける。閉じればグリッドへ戻る
    if (InputMap::GetPauseToggle())
    {
        if (m_Stack.Top() == UILayer::Pause)
            m_Stack.Pop(UILayer::Pause);
        else if (m_Stack.Top() != UILayer::LevelUp)
        {
            m_Stack.Push(UILayer::Pause);
            m_Pause.Open();
        }
    }

    // ---- 2. グリッドの開閉 ----
    // 何も開いていない時か、自分が一番上の時だけ Tab を受ける。
    // 三択が乗っている間はグリッドを閉じられない。
    const bool canToggleBackpack =
        m_Stack.CanReceiveInput(UILayer::Backpack) || m_Stack.IsEmpty();

    if (canToggleBackpack && InputMap::GetBackpackToggle())
        m_Stack.Toggle(UILayer::Backpack);

    // ---- 3. 一番上にだけ入力を渡す ----
    switch (m_Stack.Top())
    {
    case UILayer::LevelUp:
    {
        if (!reg.Has<LevelComponent>(player)) break;

        const auto& lv = reg.Get<LevelComponent>(player);

        // 画面の外で選ばれた（自測など）なら、三択の層だけ残らないよう下ろす
        if (!lv.IsChoosing())
        {
            m_Stack.Pop(UILayer::LevelUp);
            break;
        }

        ItemID picked;
        if (m_LevelUp.HandleInput(lv, picked))
        {
            LevelUpSystem::Choose(reg, player, picked);
            m_Stack.Pop(UILayer::LevelUp);
        }
        break;
    }

    case UILayer::Backpack:
    {
        if (!reg.Has<BackpackComponent>(player)) break;
        if (!reg.Has<SpellbookComponent>(player)) break;

        auto& bp = reg.Get<BackpackComponent>(player);

        // 箱が先（掴み開始）、グリッドが後（ドラッグの継続と着地）。
        // ドラッグの終了は常に BackpackUI 側が行う
        m_Spellbook.Update(reg.Get<SpellbookComponent>(player), bp, dt);
        m_Backpack.HandleInput(bp);
        break;
    }

    case UILayer::Pause:
    {
        const auto action = m_Pause.HandleInput();
        if (action == PauseMenuUI::Action::Resume)
            m_Stack.Pop(UILayer::Pause);
        else if (action != PauseMenuUI::Action::None)
            m_MenuAction = action;   // やり直す / タイトルへ：シーンが ConsumeMenuAction で拾う
        break;
    }

    default:
        break;
    }
}

// ============================================================
// 描画
// Begin / End は1回ずつ。全ての UI をまとめて 1 回の draw call にする
// ============================================================
void GameUI::Render(Registry& reg, Entity player)
{
    m_Sprite.Begin();
    m_Text.Begin();

    // ---- HUD（常時表示、モーダルが開いている間は隠す）----
    // 文字はスプライトの後に一括で描かれるため、隠さないと
    // 暗幕の上に HUD の文字だけが浮いてしまう
    if (m_Stack.IsEmpty()
        && reg.IsValid(player)
        && reg.Has<HealthComponent>(player)
        && reg.Has<ManaComponent>(player)
        && reg.Has<LevelComponent>(player))
    {
        const WandComponent* wand = reg.Has<WandComponent>(player)
            ? &reg.Get<WandComponent>(player) : nullptr;
        m_FrameInfo.wand = wand;
        m_HUD.Draw(m_Sprite, m_Text,
            reg.Get<HealthComponent>(player),
            reg.Get<ManaComponent>(player),
            reg.Get<LevelComponent>(player),
            wand && wand->castingPaused, m_FrameInfo);
        m_FrameInfo.wand = nullptr;   // 次のフレームまで指したままにしない
    }

    // ---- 操作案内（画面下の中央。影付き）----
    if (m_Stack.IsEmpty() && m_Prompt && *m_Prompt)
    {
        const std::wstring text = m_Prompt;
        const float scale = 0.6f;
        const DirectX::SimpleMath::Vector2 size = m_Text.Measure(text, scale);
        const DirectX::SimpleMath::Vector2 pos = { (m_ScreenW - size.x) * 0.5f, m_ScreenH * 0.80f };
        m_Text.Draw(text, { pos.x + 2.0f, pos.y + 2.0f }, { 0.0f, 0.0f, 0.0f, 0.8f }, scale);
        m_Text.Draw(text, pos, { 1.0f, 0.9f, 0.55f, 1.0f }, scale);
    }

    if (reg.IsValid(player))
        DrawModals(reg, player);

    m_Sprite.End();
    m_Text.End();

    // ---- 覆い層 ----
    // 文字はスプライトの後にまとめて描かれるので、同じ組の中だと
    // tooltip の箱の上に背包の文字が透けて出る。別の組で最後に描く
    if (reg.IsValid(player))
        DrawOverlay(reg, player);
}

// ============================================================
// 覆い層：背包を操作している間の tooltip
//   グリッドの魔法 → 置いてある状態の値（隣のルーンの強化込み）
//   グリッドの枠 / 魔法書の中の物 → 道具そのものの値
// ============================================================
void GameUI::DrawOverlay(Registry& reg, Entity player)
{
    // ---- tooltip の中身（背包を操作している間だけ）----
    ItemInfo::Sheet sheet;
    bool tooltip = false;
    if (m_Stack.CanReceiveInput(UILayer::Backpack) && !m_Drag.IsActive()
        && reg.Has<BackpackComponent>(player) && !ImGui::GetIO().WantCaptureMouse)
    {
        const auto& bp = reg.Get<BackpackComponent>(player);
        const int item = m_Backpack.GetHoverItemIndex();
        const int frame = m_Backpack.GetHoverFrameIndex();
        ItemID bookId = ItemID::Fireball;

        tooltip = true;
        if (item >= 0 && item < (int)bp.items.size())
            sheet = ItemInfo::DescribePlaced(bp, item);
        else if (frame >= 0 && frame < (int)bp.frames.size())
            sheet = ItemInfo::Describe(bp.frames[frame].id);
        else if (m_Spellbook.GetHoveredItem(bookId))
            sheet = ItemInfo::Describe(bookId);
        else
            tooltip = false;
    }

    // TEMP-TEST: 自測で置いた tooltip（鼠标の位置の代わりに m_TestTooltipPos）
    DirectX::SimpleMath::Vector2 anchor = { 0.0f, 0.0f };
    {
        const auto mp = InputManager::Get().GetMousePos();
        anchor = { mp.x, mp.y };
    }
    if (m_TestTooltipItem >= 0 && reg.Has<BackpackComponent>(player))
    {
        const auto& bp = reg.Get<BackpackComponent>(player);
        if (m_TestTooltipItem < (int)bp.items.size())
        {
            sheet = ItemInfo::DescribePlaced(bp, m_TestTooltipItem);
            tooltip = true;
            anchor = m_TestTooltipPos;
        }
    }

    const bool gameOver = (m_GameOverTime >= 0.0f);
    if (!tooltip && !gameOver) return;   // 何も無いフレームは Begin / End も省く

    m_Sprite.Begin();
    m_Text.Begin();

    if (gameOver)
        DrawGameOver();

    if (tooltip)
    {
        const float shortSide = (std::min)(m_ScreenW, m_ScreenH);
        ItemSheetView::DrawTooltip(m_Sprite, m_WhiteTex, m_Text, sheet,
            anchor, { m_ScreenW, m_ScreenH }, shortSide * 0.36f,
            m_TooltipStyle.Scaled(shortSide / 900.0f));
    }

    m_Sprite.End();
    m_Text.End();
}

// TEMP-TEST: 自測で画面を開閉する（VFXL_BATTLE_AUTOTEST=ui）
void GameUI::TestShow(int layer)
{
    m_Stack.Clear();
    if (layer == 1) m_Stack.Push(UILayer::Backpack);
    if (layer == 2) { m_Stack.Push(UILayer::Pause); m_Pause.Open(); }
}

// ============================================================
// 「力尽きた」の幕
//   倒れる姿を少し見せてから（0.4 秒後）赤黒い幕と文字をじわっと出す。
//   リザルトへの切替はシーン（kDeathToResult）が決める
// ============================================================
void GameUI::DrawGameOver()
{
    const float t = m_GameOverTime;
    float a = (t - 0.4f) / 0.6f;
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;

    // 死んだ時は赤黒く、Boss を倒した時は暗い金に沈める（線形の色）
    const bool cleared = m_GameOverCleared;
    m_Sprite.Draw(m_WhiteTex, { 0.0f, 0.0f }, { m_ScreenW, m_ScreenH },
        cleared ? DirectX::SimpleMath::Vector4{ 0.06f, 0.04f, 0.0f, 0.60f * a }
                : DirectX::SimpleMath::Vector4{ 0.10f, 0.0f, 0.0f, 0.68f * a });

    const float k = (std::min)(m_ScreenW, m_ScreenH) / 900.0f;

    const std::wstring title = cleared ? L"ステージクリア" : L"力尽きた";
    const float ts = 1.4f * k;
    const DirectX::SimpleMath::Vector2 tsz = m_Text.Measure(title, ts);
    const DirectX::SimpleMath::Vector2 tp = { (m_ScreenW - tsz.x) * 0.5f, m_ScreenH * 0.40f - tsz.y * 0.5f };
    m_Text.Draw(title, { tp.x + 3.0f * k, tp.y + 3.0f * k }, { 0.0f, 0.0f, 0.0f, 0.8f * a }, ts);
    m_Text.Draw(title, tp, cleared ? DirectX::SimpleMath::Vector4{ 1.0f, 0.75f, 0.25f, a }
                                   : DirectX::SimpleMath::Vector4{ 1.0f, 0.35f, 0.30f, a }, ts);

    wchar_t buf[64];
    const int total = (int)m_FrameInfo.runTime;
    swprintf_s(buf, L"生存 %02d:%02d    撃破 %u", total / 60, total % 60, m_FrameInfo.kills);
    const std::wstring sub = buf;
    const float ss = 0.5f * k;
    const DirectX::SimpleMath::Vector2 ssz = m_Text.Measure(sub, ss);
    m_Text.Draw(sub, { (m_ScreenW - ssz.x) * 0.5f, tp.y + tsz.y + 12.0f * k },
        { 0.95f, 0.90f, 0.88f, a }, ss);
}

// ============================================================
// モーダルの描画
// スタックに積まれた順にそのまま描く。後から積んだものが上に重なる
// ============================================================
void GameUI::DrawModals(Registry& reg, Entity player)
{
    for (UILayer layer : m_Stack.GetStack())
    {
        switch (layer)
        {
        case UILayer::Backpack:
            if (reg.Has<BackpackComponent>(player))
            {
                m_Spellbook.Draw(m_Sprite);   // 箱が下、グリッドとドラッグ中が上
                m_Backpack.Draw(m_Sprite, reg.Get<BackpackComponent>(player));
            }
            break;

        case UILayer::LevelUp:
            if (reg.Has<LevelComponent>(player))
                m_LevelUp.Draw(m_Sprite, m_Text, reg.Get<LevelComponent>(player));
            break;

        case UILayer::Pause:
            m_Pause.Draw(m_Sprite, m_Text, m_WhiteTex);
            break;

        default:
            break;
        }
    }
}

// ============================================================
// ImGui: UI 内部のデバッグ表示
// Backpack パネルと HUD パネル。シーンの Begin/End の中で呼ばれる
// ============================================================
// ============================================================
// ImGui: UI 内部のデバッグ表示
// Backpack パネルと HUD パネル。シーンの Begin/End の中で呼ばれる
// ============================================================
void GameUI::DrawDebugUI(Registry& reg, Entity player, BackpackAggregateSystem& aggregate)
{
    // ---------- HUD ----------
    if (ImGui::CollapsingHeader("HUD"))
        m_HUD.DrawDebugUI();

    // ---------- Backpack ----------
    if (!ImGui::CollapsingHeader("Backpack", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    if (!reg.IsValid(player)) return;
    if (!reg.Has<BackpackComponent>(player)) return;
    if (!reg.Has<SpellbookComponent>(player)) return;

    auto& bp = reg.Get<BackpackComponent>(player);
    auto& book = reg.Get<SpellbookComponent>(player);

    // ============================================================
    // UI スタックの中身
    // 割り込みが正しく積まれ、正しく下りているかを目で確認する
    // ============================================================
    ImGui::Text("UI Stack :");
    {
        const auto& stack = m_Stack.GetStack();
        if (stack.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(empty)");
        }
        for (size_t i = 0; i < stack.size(); ++i)
        {
            ImGui::SameLine();
            const bool top = (i + 1 == stack.size());
            ImGui::TextColored(top ? ImVec4(1, 0.9f, 0.3f, 1)
                : ImVec4(0.6f, 0.6f, 0.6f, 1),
                "[%s]", UIManager::LayerName(stack[i]));
        }
    }

    ImGui::Text("Open : %s   (Tab / I / E / Start)",
        m_Stack.IsOpen(UILayer::Backpack) ? "YES" : "no");
    ImGui::Checkbox("Pause On Open", &m_PauseOnBackpack);
    ImGui::TextDisabled("Grab from the box, drop on the grid. RMB: remove");
    ImGui::TextDisabled("Wheel/R: rotate while dragging");
    ImGui::Text("Rotation : %d   %s", m_Backpack.GetRotation(),
        m_Backpack.IsDragging() ? "(dragging)" : "");

    ImGui::Separator();

    // ============================================================
    // パレット（デバッグ用の旧経路）
    // 本来の取り出し口は魔法書の箱。こちらはボタンで選んで
    // グリッドの空マスからドラッグする旧い方式で、動作確認用に残している。
    // 枠か魔法かは掴んだ id が決めるので、モード切替は無い。
    // x の後ろは取り出せる数 = 所持数 - 配置済み
    // ============================================================
    bool anyShown = false;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;
        if (!book.HasLearned(id)) continue;

        anyShown = true;

        const int avail = m_Backpack.GetAvailableCount(bp, id);
        const bool usable = (avail > 0);

        // 取り出せない時は暗くする（消すと存在自体が分からなくなる）
        const float dim = usable ? 1.0f : 0.35f;
        ImGui::PushStyleColor(ImGuiCol_Button,
            ImVec4(c->color.x * 0.6f * dim, c->color.y * 0.6f * dim,
                c->color.z * 0.6f * dim, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(c->color.x * 0.9f * dim, c->color.y * 0.9f * dim,
                c->color.z * 0.9f * dim, 1.0f));

        char label[128];
        sprintf_s(label, "%s  x%d", c->name, avail);

        if (ImGui::Button(label) && usable)
            m_Backpack.SetSelectedItem(id);

        ImGui::PopStyleColor(2);

        bool selected = m_Backpack.HasSelection()
            && m_Backpack.GetSelectedItem() == id;
        if (selected)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "<-");
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();

    if (!anyShown)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "Nothing learned yet");

    if (ImGui::Button("Clear Selection")) m_Backpack.ClearSelection();

    // ---- 配置状況 ----
    ImGui::Separator();
    ImGui::Text("Spells : %zu   Frames : %zu", bp.items.size(), bp.frames.size());
    ImGui::Text("Box bodies : %d", m_Spellbook.GetBodyCount());

    // 置けるマス数。枠が無いと魔法は1つも置けない
    const int placeable = bp.PlaceableCount();
    const int total = BackpackComponent::GRID * BackpackComponent::GRID;
    ImGui::Text("Placeable : %d / %d cells", placeable, total);

    if (placeable == 0)
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1),
            "No frame placed -> no spell can be placed");

    if (ImGui::Button("Clear Spells"))
    {
        bp.items.clear();
        BackpackLogic::RebuildOccupancy(bp);
        bp.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Frames"))
    {
        // 枠を消したら、足場を失った魔法も手元へ戻す。
        // ここを忘れると宙に浮いた魔法が残り、集約結果が嘘になる
        bp.frames.clear();
        BackpackLogic::RebuildFrameOccupancy(bp);
        BackpackLogic::ValidateItems(bp);
        bp.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to 3x3"))
    {
        bp.items.clear();
        bp.frames.clear();
        BackpackLogic::RebuildOccupancy(bp);
        BackpackLogic::RebuildFrameOccupancy(bp);

        // 中央へ戻す。Rect のアンカーは左上なので、中心の 1 つ手前を指定する
        const int start = BackpackComponent::GRID / 2 - 1;
        BackpackLogic::PlaceFrame(bp, ItemID::Frame3x3, start, start, 0);
        bp.dirty = true;
    }

    // ---- 手元へ戻された魔法があれば知らせる ----
    if (m_Backpack.GetLastEvicted() > 0)
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "%d spell(s) returned (lost their frame)", m_Backpack.GetLastEvicted());
        ImGui::SameLine();
        if (ImGui::Button("OK")) m_Backpack.ClearLastEvicted();
    }

    // ============================================================
    // 習得（デバッグ用）
    // 本来の入手経路はレベルアップの三択。ここは動作確認用。
    // + を押すと次の同期で箱に1個降ってくる
    // ============================================================
    if (ImGui::TreeNode("Debug: Learn"))
    {
        ImGui::TextDisabled("Temporary. The level-up choice is the real path.");
        ImGui::TextDisabled("Reducing below the placed count is allowed;");
        ImGui::TextDisabled("it just blocks taking new ones out.");

        for (ItemID id : ItemDatabase::GetAllIDs())
        {
            const ItemCommon* c = ItemDatabase::GetCommon(id);
            if (!c) continue;

            ImGui::PushID((int)id);
            if (ImGui::SmallButton("+")) book.Learn(id, 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("-")) book.Forget(id, 1);
            ImGui::SameLine();

            const int owned = book.GetCount(id);
            const int placedN = ItemDatabase::IsFrame(id)
                ? BackpackLogic::CountPlacedFrames(bp, id)
                : BackpackLogic::CountPlaced(bp, id);

            ImGui::Text("%-14s owned %d / placed %d", c->name, owned, placedN);
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    // ---- 表示設定 ----
    if (ImGui::TreeNode("Display"))
    {
        ImGui::Checkbox("Influence on hover only", &m_Backpack.showInfluenceOnHover);
        ImGui::TextDisabled("Showing every influence cell at once fills the grid");
        ImGui::TextDisabled("with color and hides which block affects which.");

        ImGui::Checkbox("Highlight influenced blocks", &m_Backpack.highlightInfluenced);
        ImGui::DragFloat("Drag Alpha", &m_Backpack.dragAlpha, 0.01f, 0.1f, 1.0f);

        // ---- 魔法書の箱（物理の調整）----
        ImGui::SeparatorText("Spellbook Box");
        ImGui::Text("Bodies : %d", m_Spellbook.GetBodyCount());
        ImGui::DragFloat("Gravity##sb", &m_Spellbook.gravity, 10.0f, 0.0f, 5000.0f);
        ImGui::DragFloat("Restitution##sb", &m_Spellbook.restitution, 0.01f, 0.0f, 0.9f);
        ImGui::DragFloat("Spin##sb", &m_Spellbook.spinTransfer, 0.01f, 0.0f, 2.0f);
        ImGui::DragFloat("Box Scale##sb", &m_Spellbook.boxScale, 0.01f, 0.3f, 1.0f);

        ImGui::TreePop();
    }

    // ---- 集約結果（グリッド から 杖）----
    ImGui::Separator();
    ImGui::Text("Aggregate (rebuilt %d times)", aggregate.GetRebuildCount());

    const auto& logs = aggregate.GetLog();
    if (logs.empty())
    {
        ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1),
            "No attack block placed -> wand fires nothing");
    }
    for (const auto& log : logs)
    {
        ImGui::Text("%s (%d,%d)", log.sourceName.c_str(), log.row, log.col);
        if (log.influencedBy.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  no influence");
        }
        else
        {
            for (const auto& n : log.influencedBy)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.4f, 1, 1, 1), " <- %s", n.c_str());
            }
        }
    }

    if (ImGui::Button("Force Rebuild"))
        aggregate.ForceRebuild(reg, player);

    // ---- レイアウト（見た目の調整）----
    if (ImGui::TreeNode("Layout"))
    {
        bool dirty = false;

        int anchorIdx = (int)m_Backpack.anchor;
        const char* anchorNames[] = { "Top Left", "Top Right", "Center",
                                      "Bottom Left", "Bottom Right" };
        if (ImGui::Combo("Anchor", &anchorIdx, anchorNames, 5))
        {
            m_Backpack.anchor = (BackpackUI::Anchor)anchorIdx;
            dirty = true;
        }

        dirty |= ImGui::DragFloat("Grid Ratio", &m_Backpack.gridScreenRatio, 0.005f, 0.10f, 0.95f);
        dirty |= ImGui::DragFloat("Gap Ratio", &m_Backpack.cellGapRatio, 0.002f, 0.00f, 0.50f);
        dirty |= ImGui::DragFloat("Pad Ratio", &m_Backpack.framePadRatio, 0.005f, 0.00f, 1.00f);
        dirty |= ImGui::DragFloat("Margin Ratio", &m_Backpack.marginRatio, 0.002f, 0.00f, 0.30f);

        // ---- 箱のレイアウト ----
        dirty |= ImGui::DragFloat("Box Ratio", &m_Spellbook.boxScreenRatio, 0.005f, 0.10f, 0.95f);
        dirty |= ImGui::DragFloat("Wall Ratio", &m_Spellbook.wallRatio, 0.002f, 0.00f, 0.20f);

        if (dirty) Layout(m_ScreenW, m_ScreenH);

        ImGui::Text("Screen : %.0f x %.0f", m_ScreenW, m_ScreenH);
        ImGui::Text("Cell : %.1f px   Gap : %.1f px",
            m_Backpack.GetCellSize(), m_Backpack.GetCellGap());

        ImGui::ColorEdit4("Frame Color", &m_Backpack.frameColor.x);
        ImGui::ColorEdit4("Cell Color", &m_Backpack.cellColor.x);
        ImGui::ColorEdit4("Locked Cell", &m_Backpack.lockedCellColor.x);
        ImGui::ColorEdit4("Box Frame", &m_Spellbook.frameColor.x);
        ImGui::ColorEdit4("Box Inner", &m_Spellbook.innerColor.x);

        ImGui::Text("Sprites : %u   Draw calls : %u",
            m_Sprite.GetLastSpriteCount(), m_Sprite.GetLastDrawCalls());
        ImGui::TreePop();
    }
}