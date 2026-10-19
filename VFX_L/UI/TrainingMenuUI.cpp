// ============================================================
// TrainingMenuUI.cpp
// トレーニングのメニュー：行の組み立て・配置・入力（描画は TrainingMenuUIDraw.cpp）。説明は TrainingMenuUI.h
// ※日本語の文字列リテラルを含むので UTF-8（BOM 付き）で保存する
// ============================================================
#include "UI/TrainingMenuUI.h"
#include "Manager/InputManager.h"
#include "Audio/AudioSystem.h"
#include "Item/ItemDatabase.h"
#include "Item/ItemTypes.h"
#include "UI/UIDeco.h"

using namespace DirectX::SimpleMath;

const float TrainingMenuUI::kTargetHp[kHpChoices] = { 100.0f, 1000.0f, 10000.0f, 100000.0f, 1000000.0f };
const wchar_t* const TrainingMenuUI::kSwarmKinds[kKindChoices] = { L"雑魚", L"自爆兵", L"スプリッター", L"重装兵", L"突撃兵", L"盾兵", L"幽霊", L"トゲ玉", L"砂潜り", L"砂嵐の術師", L"黒幽霊", L"突撃爆弾兵", L"混合" };
const int TrainingMenuUI::kSwarmCounts[kCountChoices] = { 10, 30, 100, 300 };
const wchar_t* const TrainingMenuUI::kRandomModeNames[kRandomModes] = { L"基本魔法だけ", L"魔法 + ルーン", L"全部（上級・召喚も）" };
const int TrainingMenuUI::kRandomCountValues[kRandomCounts] = { 4, 6, 8, 12 };

namespace
{
    constexpr float kRepeatFirst = 0.30f;  // 押しっぱなしで動き出すまで
    constexpr float kRepeatNext = 0.08f;
    constexpr float kFrame = 0.016f;       // 止まっている間も使うので固定の 1 フレーム
}

TrainingMenuUI::TrainingMenuUI() = default;

void TrainingMenuUI::Build()
{
    m_Rows.clear();
    m_Rows.push_back({ L"的を置く", RowKind::Buttons, RowId::Targets, { L"正面に 3 体", L"周りに 12 体", L"エリート" }, {} });
    m_Rows.push_back({ L"的の HP", RowKind::Choice, RowId::TargetHp, {}, {} });
    m_Rows.push_back({ L"群れの種類", RowKind::Choice, RowId::SwarmKind, {}, {} });
    m_Rows.push_back({ L"群れの数", RowKind::Choice, RowId::SwarmCount, {}, {} });
    m_Rows.push_back({ L"敵が動く", RowKind::Toggle, RowId::Moving, {}, {} });
    m_Rows.push_back({ L"敵を出す", RowKind::Buttons, RowId::Spawn, { L"群れを出す", L"ボスを呼ぶ", L"敵を全部消す" }, {} });
    // フィールド（2026-10-20）：平地の実験場か、本番の面の地形か。トレーニングのモード（無敵・MP 無限・湧き停止）はそのまま
    m_Rows.push_back({ L"フィールド", RowKind::Buttons, RowId::Field, { L"平地", L"草原", L"砂漠", L"遺跡" }, {} });

    // 魔法・ルーン・召喚物（枠と能力カードは除く。SpellLab::FillChest と同じ選び方）を kItemsPerRow 個ずつの行に
    std::vector<ItemID> items;
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCategory c = ItemDatabase::GetCategory(id);
        if (c == ItemCategory::Projectile || c == ItemCategory::Area || c == ItemCategory::Function || c == ItemCategory::Summon)
            items.push_back(id);
    }
    for (size_t i = 0; i < items.size(); i += kItemsPerRow)
    {
        Row r{ (i == 0) ? L"木箱に入れる" : L"", RowKind::Items, RowId::Items, {}, {} };
        r.items.assign(items.begin() + i, items.begin() + (std::min)(items.size(), i + kItemsPerRow));
        m_Rows.push_back(std::move(r));
    }

    m_Rows.push_back({ L"木箱", RowKind::Buttons, RowId::Chest, { L"全部入れる（各 3 個）", L"木箱を空にする" }, {} });
    // バックパックをランダムに組む（2026-10-14 ユーザー：ランダムに魔法を置く選択肢）。中身と個数を選んでボタン
    m_Rows.push_back({ L"ランダムの中身", RowKind::Choice, RowId::RandomMode, {}, {} });
    m_Rows.push_back({ L"ランダムの数", RowKind::Choice, RowId::RandomCount, {}, {} });
    m_Rows.push_back({ L"バックパック", RowKind::Buttons, RowId::Backpack, { L"ランダムに組む", L"バックパックを空にする" }, {} });
    m_Rows.push_back({ L"戻る", RowKind::Back, RowId::Back, {}, {} });
}

// ============================================================
// 配置
// ============================================================
float TrainingMenuUI::RowHeight(int i) const
{
    return (m_Rows[i].kind == RowKind::Items) ? m_ItemH : m_RowH;
}

void TrainingMenuUI::Layout(float screenW, float screenH)
{
    if (m_Rows.empty()) Build();
    m_Screen = { screenW, screenH };
    m_Short = (std::min)(screenW, screenH);

    // 箱の高さ（倍率 1 の時の画面短辺に対する割合）が 95% を超えない所まで、幅も画面幅の 95% まで
    float rowsRatio = 0.0f;
    for (const Row& r : m_Rows) rowsRatio += (r.kind == RowKind::Items) ? itemRowRatio : rowHeightRatio;
    const int n = (int)m_Rows.size();
    const float baseH = kPadRatio * 2.0f + kTitleRatio + rowsRatio + rowGapRatio * (float)(n - 1) + kFootRatio;
    m_Scale = UIDeco::FitScale(baseH, 0.985f);   // 行が多いので画面の高さほぼ一杯まで
    // FitScale は 1.0 倍より小さくしないが、このメニューは行が多くて 1 倍でも画面より高い（1600x900 で上下が切れていた。
    // 2026-10-20 フィールドの行を足した時に直した）：高さが収まる所まで 1 倍未満にも縮める
    if (baseH > 0.0f) m_Scale = (std::min)(m_Scale, 0.985f / baseH);
    m_Scale = (std::min)(m_Scale, screenW * 0.95f / (m_Short * panelWidthRatio));
    const float s = m_Short * m_Scale;

    const float pad = s * kPadRatio;
    m_RowH = s * rowHeightRatio;
    m_ItemH = s * itemRowRatio;
    m_Gap = s * rowGapRatio;
    float listH = 0.0f;
    for (int i = 0; i < n; ++i) listH += RowHeight(i);
    listH += m_Gap * (float)(n - 1);

    m_PanelSize = { s * panelWidthRatio, pad + s * kTitleRatio + listH + s * kFootRatio + pad };
    m_PanelPos = { (screenW - m_PanelSize.x) * 0.5f, (screenH - m_PanelSize.y) * 0.5f };
    m_RowsTop = m_PanelPos.y + pad + s * kTitleRatio;
}

Vector2 TrainingMenuUI::RowPos(int i) const
{
    const float pad = m_Short * m_Scale * kPadRatio;
    float y = m_RowsTop;
    for (int j = 0; j < i; ++j) y += RowHeight(j) + m_Gap;
    return { m_PanelPos.x + pad, y };
}

void TrainingMenuUI::ControlRect(int i, Vector2& pos, Vector2& size) const
{
    const float pad = m_Short * m_Scale * kPadRatio;
    const float rowW = m_PanelSize.x - pad * 2.0f;
    const Vector2 rp = RowPos(i);
    if (m_Rows[i].kind == RowKind::Back)
    {
        pos = rp;
        size = { rowW, RowHeight(i) };
        return;
    }
    const float labelW = rowW * kLabelRatio;
    pos = { rp.x + labelW, rp.y };
    size = { rowW - labelW, RowHeight(i) };
}

void TrainingMenuUI::CellRect(int i, int col, Vector2& pos, Vector2& size) const
{
    Vector2 cp, cs;
    ControlRect(i, cp, cs);
    const Row& r = m_Rows[i];
    if (r.kind == RowKind::Buttons)
    {
        const int n = (std::max)(1, (int)r.buttons.size());
        const float g = m_RowH * 0.18f;
        const float w = (cs.x - g * (float)(n - 1)) / (float)n;
        pos = { cp.x + (w + g) * (float)col, cp.y };
        size = { w, cs.y };
    }
    else if (r.kind == RowKind::Items)
    {
        const float cell = cs.x / (float)kItemsPerRow;
        const float sz = (std::min)(cell, cs.y) * 0.92f;
        pos = { cp.x + cell * (float)col + (cell - sz) * 0.5f, cp.y + (cs.y - sz) * 0.5f };
        size = { sz, sz };
    }
    else
    {
        pos = cp;
        size = cs;
    }
}

int TrainingMenuUI::ColCount(const Row& r) const
{
    if (r.kind == RowKind::Buttons) return (int)r.buttons.size();
    if (r.kind == RowKind::Items) return (int)r.items.size();
    return 1;
}

int TrainingMenuUI::HitTest(const Vector2& p, int& col) const
{
    col = -1;
    const float pad = m_Short * m_Scale * kPadRatio;
    const float rowW = m_PanelSize.x - pad * 2.0f;
    for (int i = 0; i < (int)m_Rows.size(); ++i)
    {
        const Vector2 rp = RowPos(i);
        if (p.x < rp.x || p.x > rp.x + rowW || p.y < rp.y || p.y > rp.y + RowHeight(i)) continue;
        const Row& r = m_Rows[i];
        if (r.kind == RowKind::Buttons || r.kind == RowKind::Items)
        {
            for (int c = 0; c < ColCount(r); ++c)
            {
                Vector2 cp, cs;
                CellRect(i, c, cp, cs);
                if (p.x >= cp.x && p.x <= cp.x + cs.x && p.y >= cp.y && p.y <= cp.y + cs.y) { col = c; break; }
            }
        }
        else
            col = 0;
        return i;
    }
    return -1;
}

ItemID TrainingMenuUI::FocusedItem() const
{
    if (m_Row < 0 || m_Row >= (int)m_Rows.size()) return ItemID::Unknown;
    const Row& r = m_Rows[m_Row];
    if (r.kind != RowKind::Items || m_Col < 0 || m_Col >= (int)r.items.size()) return ItemID::Unknown;
    return r.items[m_Col];
}

std::wstring TrainingMenuUI::ValueText(const Row& r) const
{
    switch (r.id)
    {
    case RowId::TargetHp:
    {
        // 3 桁ごとに区切る（1,000,000）
        std::wstring s = std::to_wstring((long long)kTargetHp[m_HpIndex]);
        for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, L",");
        return s;
    }
    case RowId::SwarmKind:  return kSwarmKinds[m_KindIndex];
    case RowId::SwarmCount: return std::to_wstring(kSwarmCounts[m_CountIndex]) + L" 体";
    case RowId::Moving:     return m_Moving ? L"オン" : L"オフ";
    case RowId::RandomMode: return kRandomModeNames[m_RandomModeIndex];
    case RowId::RandomCount: return std::to_wstring(kRandomCountValues[m_RandomCountIndex]) + L" 個";
    default:                return L"";
    }
}

// ============================================================
// 操作
// ============================================================
void TrainingMenuUI::Open()
{
    if (m_Rows.empty()) Build();
    m_Row = std::clamp(m_Row, 0, (int)m_Rows.size() - 1);
    m_Col = std::clamp(m_Col, 0, (std::max)(0, ColCount(m_Rows[m_Row]) - 1));
    m_InputDelay = 0.15f;
    m_RepeatDelay = 0.0f;
    m_LastMouse = { -1.0f, -1.0f };
}

void TrainingMenuUI::Step(int dir)
{
    const Row& r = m_Rows[m_Row];
    auto wrap = [dir](int v, int n) { return (v + dir + n) % n; };
    switch (r.id)
    {
    case RowId::TargetHp:   m_HpIndex = wrap(m_HpIndex, kHpChoices); break;
    case RowId::SwarmKind:  m_KindIndex = wrap(m_KindIndex, kKindChoices); break;
    case RowId::SwarmCount: m_CountIndex = wrap(m_CountIndex, kCountChoices); break;
    case RowId::Moving:     m_Moving = !m_Moving; break;
    case RowId::RandomMode: m_RandomModeIndex = wrap(m_RandomModeIndex, kRandomModes); break;
    case RowId::RandomCount: m_RandomCountIndex = wrap(m_RandomCountIndex, kRandomCounts); break;
    default: return;
    }
    AudioSystem::Get().Play("ui_hover");
}

void TrainingMenuUI::Activate()
{
    const Row& r = m_Rows[m_Row];
    TrainingRequest q;
    q.targetHp = kTargetHp[m_HpIndex];
    q.swarmKind = m_KindIndex;
    q.count = kSwarmCounts[m_CountIndex];
    q.moving = m_Moving;
    q.randomMode = m_RandomModeIndex;
    q.randomCount = kRandomCountValues[m_RandomCountIndex];
    switch (r.id)
    {
    case RowId::Targets:
        q.kind = TrainingRequest::Kind::Targets;
        q.pattern = m_Col;
        break;
    case RowId::Spawn:
        q.kind = (m_Col == 0) ? TrainingRequest::Kind::Swarm
            : (m_Col == 1) ? TrainingRequest::Kind::Boss : TrainingRequest::Kind::KillAll;
        break;
    case RowId::Items:
        if (m_Col < 0 || m_Col >= (int)r.items.size()) return;
        q.kind = TrainingRequest::Kind::AddItem;
        q.item = r.items[m_Col];
        break;
    case RowId::Chest:
        q.kind = (m_Col == 0) ? TrainingRequest::Kind::AddAll : TrainingRequest::Kind::ClearChest;
        break;
    case RowId::Backpack:
        q.kind = (m_Col == 0) ? TrainingRequest::Kind::RandomBuild : TrainingRequest::Kind::ClearBackpack;
        break;
    case RowId::Field:
        q.kind = TrainingRequest::Kind::Field;
        q.field = m_Col;   // 0 平地 / 1 草原 / 2 砂漠 / 3 遺跡
        break;
    case RowId::Moving:        // 値の行は Enter / A で 1 段進める
    case RowId::TargetHp:
    case RowId::SwarmKind:
    case RowId::SwarmCount:
    case RowId::RandomMode:
    case RowId::RandomCount:
        Step(+1);
        return;
    default:
        return;
    }
    m_Requests.push_back(q);
    AudioSystem::Get().Play("ui_select");
}

void TrainingMenuUI::TestActivate(int rowId, int col)
{
    if (m_Rows.empty()) Build();
    for (int i = 0; i < (int)m_Rows.size(); ++i)
        if ((int)m_Rows[i].id == rowId)
        {
            m_Row = i;
            m_Col = std::clamp(col, 0, (std::max)(0, ColCount(m_Rows[i]) - 1));
            Activate();
            return;
        }
}

// ============================================================
// 入力。閉じるなら true
// ============================================================
bool TrainingMenuUI::HandleInput()
{
    if (m_Rows.empty()) Build();
    auto& input = InputManager::Get();
    const int n = (int)m_Rows.size();
    if (m_MessageTime > 0.0f) m_MessageTime -= kFrame;

    // B で閉じる（Esc・T・RB は GameUI が先に見て閉じる）
    if (input.GetPadTrigger(XINPUT_GAMEPAD_B))
    {
        AudioSystem::Get().Play("ui_close");
        return true;
    }

    const auto mp = input.GetMousePos();
    const Vector2 mouse = { mp.x, mp.y };
    const bool mouseMoved = (mouse - m_LastMouse).LengthSquared() > 0.25f;
    m_LastMouse = mouse;

    if (m_InputDelay > 0.0f)
    {
        m_InputDelay -= kFrame;
        return false;
    }

    // ---- マウス：動かした時だけ選ぶ ----
    int hoverCol = -1;
    const int hover = HitTest(mouse, hoverCol);
    if (hover >= 0 && mouseMoved && (hoverCol >= 0 || ColCount(m_Rows[hover]) <= 1))
    {
        const int c = (std::max)(0, hoverCol);
        if (hover != m_Row || c != m_Col) AudioSystem::Get().Play("ui_hover");
        m_Row = hover;
        m_Col = c;
    }

    // ---- 上下（行）。部品の位置はなるべく保つ ----
    const int beforeRow = m_Row;
    if (input.GetKeyTrigger(VK_UP) || input.GetKeyTrigger('W') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_UP))
        m_Row = (m_Row + n - 1) % n;
    if (input.GetKeyTrigger(VK_DOWN) || input.GetKeyTrigger('S') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_DOWN))
        m_Row = (m_Row + 1) % n;
    if (m_Row != beforeRow)
    {
        m_Col = std::clamp(m_Col, 0, (std::max)(0, ColCount(m_Rows[m_Row]) - 1));
        AudioSystem::Get().Play("ui_hover");
    }

    // ---- 左右（値・ボタン・絵。押しっぱなしで連続）----
    const bool leftHeld = input.GetKeyPress(VK_LEFT) || input.GetKeyPress('A') || input.GetPadPress(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightHeld = input.GetKeyPress(VK_RIGHT) || input.GetKeyPress('D') || input.GetPadPress(XINPUT_GAMEPAD_DPAD_RIGHT);
    const bool leftTrig = input.GetKeyTrigger(VK_LEFT) || input.GetKeyTrigger('A') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_LEFT);
    const bool rightTrig = input.GetKeyTrigger(VK_RIGHT) || input.GetKeyTrigger('D') || input.GetPadTrigger(XINPUT_GAMEPAD_DPAD_RIGHT);
    int dir = 0;
    if (leftTrig || rightTrig)
    {
        dir = rightTrig ? +1 : -1;
        m_RepeatDelay = kRepeatFirst;
    }
    else if (leftHeld || rightHeld)
    {
        m_RepeatDelay -= kFrame;
        if (m_RepeatDelay <= 0.0f)
        {
            dir = rightHeld ? +1 : -1;
            m_RepeatDelay = kRepeatNext;
        }
    }
    if (dir != 0)
    {
        const Row& r = m_Rows[m_Row];
        if (r.kind == RowKind::Buttons || r.kind == RowKind::Items)
        {
            const int c = std::clamp(m_Col + dir, 0, ColCount(r) - 1);
            if (c != m_Col) AudioSystem::Get().Play("ui_hover");
            m_Col = c;
        }
        else
            Step(dir);
    }

    // ---- 決定 ----
    if (input.GetKeyTrigger(VK_RETURN) || input.GetKeyTrigger(VK_SPACE) || input.GetPadTrigger(XINPUT_GAMEPAD_A))
    {
        if (m_Rows[m_Row].kind == RowKind::Back)
        {
            AudioSystem::Get().Play("ui_close");
            return true;
        }
        Activate();
    }

    // ---- クリック ----
    if (input.GetMouseTrigger(0) && hover >= 0)
    {
        const Row& r = m_Rows[hover];
        m_Row = hover;
        if (r.kind == RowKind::Back)
        {
            AudioSystem::Get().Play("ui_close");
            return true;
        }
        if (r.kind == RowKind::Choice || r.kind == RowKind::Toggle)
        {
            // 左右の < > の上なら 1 段、真ん中は進める
            Vector2 cp, cs;
            ControlRect(hover, cp, cs);
            const float arrowW = cs.y;
            if (mouse.x >= cp.x && mouse.x < cp.x + arrowW) Step(-1);
            else if (mouse.x >= cp.x) Step(+1);
        }
        else if (hoverCol >= 0)
        {
            m_Col = hoverCol;
            Activate();
        }
    }
    return false;
}
