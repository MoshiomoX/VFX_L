// ============================================================
// HUD.cpp
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Component/WandComponent.h"
#include "Player/LevelComponent.h"
#include "Item/ItemDatabase.h"
#include "UI/UIDeco.h"
#include "Manager/ResourceManager.h"
#include <cmath>
#include "ResourcePaths.h"
#include "imgui.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

using namespace DirectX::SimpleMath;
using json = nlohmann::json;

namespace
{
    // 表示用の整数へ（瀕死で 0 と表示しないよう切り上げ）
    int CeilInt(float v) { return (int)std::ceil(v); }

    float Clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    float SafeRatio(float current, float max)
    {
        return (max > 0.0f) ? Clamp01(current / max) : 0.0f;
    }

    // ---- JSON 変換（座標と色は配列で持つ。キーが増えず読みやすい）----
    json ToJson(const Vector2& v) { return json::array({ v.x, v.y }); }
    json ToJson(const Vector4& v) { return json::array({ v.x, v.y, v.z, v.w }); }

    json ToJson(const HUDAnchor& a)
    {
        json j;
        j["anchor"] = ToJson(a.anchor);
        j["offset"] = ToJson(a.offset);
        return j;
    }

    // 読み込みは「キーが無ければ既定値のまま」。
    // 項目を増やした後も古い JSON がそのまま読めるようにするため
    void ReadFloat(const json& j, const char* key, float& out)
    {
        if (j.contains(key) && j[key].is_number()) out = j[key].get<float>();
    }

    void ReadBool(const json& j, const char* key, bool& out)
    {
        if (j.contains(key) && j[key].is_boolean()) out = j[key].get<bool>();
    }

    void ReadVec2(const json& j, const char* key, Vector2& out)
    {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < 2) return;
        out.x = j[key][0].get<float>();
        out.y = j[key][1].get<float>();
    }

    void ReadVec4(const json& j, const char* key, Vector4& out)
    {
        if (!j.contains(key) || !j[key].is_array() || j[key].size() < 4) return;
        out.x = j[key][0].get<float>();
        out.y = j[key][1].get<float>();
        out.z = j[key][2].get<float>();
        out.w = j[key][3].get<float>();
    }

    void ReadAnchor(const json& j, const char* key, HUDAnchor& out)
    {
        if (!j.contains(key) || !j[key].is_object()) return;
        ReadVec2(j[key], "anchor", out.anchor);
        ReadVec2(j[key], "offset", out.offset);
    }

    // ---- ImGui の小物 ----
    void DragAnchor(const char* label, HUDAnchor& a)
    {
        ImGui::PushID(label);
        ImGui::Text("%s", label);
        ImGui::Indent();
        ImGui::DragFloat2("anchor", &a.anchor.x, 0.005f, 0.0f, 1.0f);
        ImGui::DragFloat2("offset", &a.offset.x, 1.0f, -4000.0f, 4000.0f);
        ImGui::Unindent();
        ImGui::PopID();
    }
}

// ============================================================
// 初期化
// ============================================================
bool HUD::Initialize(ID3D11Device* device)
{
    m_WhiteTex = std::make_shared<Texture>();
    if (!m_WhiteTex->CreateSolid(device, 255, 255, 255, 255))
    {
        std::cout << "[Error] HUD: white texture failed" << std::endl;
        m_WhiteTex.reset();
        return false;
    }

    // 魔力解放の欄の絵（無ければ円だけ描く）・金貨の絵
    m_SurgeIcon = ResourceManager::Get().LoadTexture(Res::Icon::ManaSurge);
    m_GoldIcon = ResourceManager::Get().LoadTexture(Res::Icon::Gold);

    // 保存済みの調整があれば使う。無ければコードの既定値のまま
    LoadStyle();

    std::cout << "[OK] HUD initialized" << std::endl;
    return true;
}

void HUD::Layout(float screenW, float screenH)
{
    m_ScreenW = screenW;
    m_ScreenH = screenH;
}

// ============================================================
// 残像の追従
//
// 減った直後は trailDelay の間だけ止め、その後 trailSpeed で
// 追いつかせる。増えた時は待たずに合わせる（回復は即反映）。
// ============================================================
void HUD::BarTrail::Update(float dt, float ratio, const HUDStyle& style)
{
    if (!style.damageTrail || ratio >= value)
    {
        value = ratio;
        timer = 0.0f;
        return;
    }

    timer += dt;
    if (timer < style.trailDelay) return;

    value -= style.trailSpeed * dt;
    if (value < ratio) value = ratio;
}

void HUD::Update(float dt, const HealthComponent& hp, const ManaComponent& mp)
{
    m_Time += dt;
    m_HpTrail.Update(dt, SafeRatio(hp.current, hp.max), m_Style);
    m_MpTrail.Update(dt, SafeRatio(mp.current, mp.max), m_Style);

    // 魔力解放の再使用待ちが明けた瞬間に欄を一度光らせる
    const bool ready = !mp.SurgeActive() && mp.surgeCooldownLeft <= 0.0f;
    if (ready && !m_SurgeWasReady) m_SurgeReadyFlash = 1.0f;
    m_SurgeWasReady = ready;
    m_SurgeReadyFlash = (std::max)(0.0f, m_SurgeReadyFlash - dt / 0.6f);
}

// ============================================================
// 描画
// ============================================================
void HUD::Draw(SpriteRenderer& sprite, TextRenderer& text,
    const HealthComponent& hp, const ManaComponent& mp,
    const LevelComponent& lv, const HUDFrameInfo& info)
{
    if (!m_WhiteTex) return;

    // ---- 一番下の層：瀕死の赤い縁と画面外の目印（バーや文字の下に敷く）----
    DrawLowHpVignette(sprite, hp);
    DrawSurgeVignette(sprite, mp);
    DrawMarkers(sprite, info);

    // ---- 経験値バー（既定では最上段の通し）----
    // 選択待ちで持ち越し中は 1.0 を超えるので丸める
    const Vector2 expPos = m_Style.expBar.Resolve(m_ScreenW, m_ScreenH)
        + Vector2(m_Style.expBarMargin, 0.0f);
    const Vector2 expSize = {
        m_ScreenW - m_Style.expBarMargin * 2.0f,
        m_Style.expBarHeight
    };
    const float expRatio = Clamp01(lv.Progress());
    DrawBar(sprite, expPos, expSize, expRatio, expRatio, m_Style.expColor);

    // ---- HP / MP バー ----
    const Vector2 hpPos = m_Style.hpBar.Resolve(m_ScreenW, m_ScreenH);
    const Vector2 mpPos = m_Style.mpBar.Resolve(m_ScreenW, m_ScreenH);

    DrawBar(sprite, hpPos, m_Style.hpBarSize,
        SafeRatio(hp.current, hp.max), m_HpTrail.value, m_Style.hpColor, true);
    // 魔力解放中は金色で脈打つ（減らないことが一目で分かるように）
    Vector4 mpCol = m_Style.mpColor;
    if (mp.SurgeActive())
    {
        const float pulse = 0.85f + 0.15f * std::sin(m_Time * 10.0f);
        mpCol = m_Style.mpSurgeColor * pulse;
        mpCol.w = m_Style.mpSurgeColor.w;
    }
    DrawBar(sprite, mpPos, m_Style.mpBarSize,
        SafeRatio(mp.current, mp.max), m_MpTrail.value, mpCol, true);

    // ---- 数値 ----
    wchar_t buf[32];

    swprintf_s(buf, L"Lv %d", lv.level);
    DrawLabel(text, buf, m_Style.lvText.Resolve(m_ScreenW, m_ScreenH),
        m_Style.lvTextScale);

    swprintf_s(buf, L"HP %d/%d", CeilInt(hp.current), (int)hp.max);
    DrawBarLabel(text, buf, hpPos, m_Style.hpBarSize);

    swprintf_s(buf, L"MP %d/%d", CeilInt(mp.current), (int)mp.max);
    DrawBarLabel(text, buf, mpPos, m_Style.mpBarSize);

    // ---- 金貨（MP バーの下）----
    if (m_Style.showGold && info.gold >= 0)
        DrawGold(sprite, text, info.gold);

    // ---- 経過時間・撃破数、魔法の欄、その上の魔力解放（Q）----
    // 魔力解放は 2026-10-04 まで MP バーの下の一行の文字だった
    DrawRunInfo(sprite, text, info);
    if (m_Style.showSpellBar && info.wand)
        DrawSpellBar(sprite, *info.wand, mp);
    if (m_Style.showSurgeSkill)
        DrawSurgeSkill(sprite, text, mp);
}

// ============================================================
// 経過時間（大）と撃破数（小）を上の中央に。間に古金の細い分割線
// ============================================================
void HUD::DrawRunInfo(SpriteRenderer& sprite, TextRenderer& text, const HUDFrameInfo& info)
{
    const Vector2 a = m_Style.runInfo.Resolve(m_ScreenW, m_ScreenH);
    wchar_t buf[32];

    // 制限時間があれば残りを数え下ろす（切り上げ: 0:00 になった瞬間が時間切れ）。
    // 過ぎたら超過分を赤く「+」付きで
    const bool countdown = info.stageTime > 0.0f;
    const bool overtime = countdown && info.runTime >= info.stageTime;
    int total = (int)info.runTime;
    if (countdown)
        total = overtime ? (int)(info.runTime - info.stageTime) : (int)std::ceil(info.stageTime - info.runTime);
    swprintf_s(buf, overtime ? L"+%02d:%02d" : L"%02d:%02d", total / 60, total % 60);
    const Vector2 ts = text.Measure(buf, m_Style.timerScale);
    const Vector2 tp = { a.x - ts.x * 0.5f, a.y };
    if (overtime)
    {
        // 線形の色（HUD はシーンの HDR に描いてからトーンマッピングを通る）
        const float o = m_Style.textShadowOffset;
        if (m_Style.textShadow) text.Draw(buf, { tp.x + o, tp.y + o }, m_Style.shadowColor, m_Style.timerScale);
        text.Draw(buf, tp, { 1.0f, 0.12f, 0.08f, 1.0f }, m_Style.timerScale);
    }
    else
        DrawLabel(text, buf, tp, m_Style.timerScale);

    float y = a.y + ts.y;
    if (m_Style.runDividerWidth > 0.0f)
    {
        const float dh = m_Style.runDividerWidth * 0.05f;
        UIDeco::DrawDivider(sprite, false, { a.x, y }, m_Style.runDividerWidth, m_Style.borderColor);
        y += dh * 0.5f;
    }

    if (info.stage > 0)
        swprintf_s(buf, overtime ? L"ステージ%d %s   最終ウェーブ   撃破 %u" : L"ステージ%d %s   撃破 %u", info.stage, info.stageName, info.kills);
    else
        swprintf_s(buf, overtime ? L"最終ウェーブ   撃破 %u" : L"撃破 %u", info.kills);
    const Vector2 ks = text.Measure(buf, m_Style.killScale);
    DrawLabel(text, buf, { a.x - ks.x * 0.5f, y }, m_Style.killScale);

    // ---- Boss の HP 条（呼んでいる間だけ。撃破数の下に画面幅の 4 割）----
    if (info.bossHp >= 0.0f)
    {
        const Vector2 size = { m_ScreenW * 0.4f, m_Style.hpBarSize.y };
        const Vector2 pos = { a.x - size.x * 0.5f, y + ks.y + 8.0f };
        const float r = Clamp01(info.bossHp);
        DrawBar(sprite, pos, size, r, r, { 0.35f, 0.05f, 0.55f, 1.0f }, true);   // 紫（線形）
        DrawBarLabel(text, L"ボス", pos, size);
    }
}

// ============================================================
// 魔法の欄
//   杖の出力（集約後の spells → areas の順）を 1 マスずつ並べる。
//   クールダウンの残りは上から暗く被せ、MP が足りない物は青く沈める。
//   詠唱を止めている間は欄ごと暗くする
// ============================================================
void HUD::DrawSpellBar(SpriteRenderer& sprite,
    const WandComponent& wand, const ManaComponent& mp)
{
    struct Slot { ItemID id; float timer; float interval; float cost; };
    std::vector<Slot> slots;
    for (const auto& s : wand.spells)
        slots.push_back({ s.id, s.castTimer, s.castInterval, s.manaCost });
    for (const auto& a : wand.areas)
        slots.push_back({ a.id, a.castTimer, a.castInterval, a.manaCost });
    if (slots.empty()) return;

    const float size = m_Style.slotSize;
    const float gap = m_Style.slotGap;
    const float totalW = size * (float)slots.size() + gap * (float)(slots.size() - 1);
    const Vector2 a = m_Style.spellBar.Resolve(m_ScreenW, m_ScreenH);
    const float left = a.x - totalW * 0.5f;
    const float top = a.y - size;
    const float iconSize = size * 0.56f;

    // 丸い欄（円のテクスチャが無ければ四角に戻す）
    const UIDeco::Textures& deco = UIDeco::Tex();
    const auto& disc = deco.disc ? deco.disc : m_WhiteTex;
    const float spin = UIDeco::Clock() * 0.10f;

    float x = left;
    int index = 0;
    for (const auto& sl : slots)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(sl.id);
        const Vector4 col = c ? c->color : Vector4(1, 1, 1, 1);
        const Vector2 center = { x + size * 0.5f, top + size * 0.5f };

        // 後ろの魔法陣（アイテムの種類の色。隣同士で逆に回す）。明るい草の上でも見えるよう、暗い円を敷いてから
        if (m_Style.slotCircleScale > 0.0f)
        {
            const float d = size * m_Style.slotCircleScale;
            sprite.Draw(disc, { center.x - d * 0.5f, center.y - d * 0.5f }, { d, d }, { 0.0f, 0.0f, 0.0f, 0.55f });
            Vector4 ring = UIDeco::CategoryColor(c ? c->category : ItemCategory::Projectile);
            ring.w = 0.85f;
            UIDeco::DrawCircle(sprite, false, center, d, ring, (index % 2) ? -spin : spin);
        }

        sprite.Draw(disc, { x, top }, { size, size }, m_Style.slotBgColor);

        auto icon = m_IconLookup ? m_IconLookup(sl.id) : nullptr;
        const Vector2 ip = { center.x - iconSize * 0.5f, center.y - iconSize * 0.5f };
        if (icon) sprite.Draw(icon, ip, { iconSize, iconSize }, m_Style.textColor);
        else      sprite.Draw(disc, ip, { iconSize, iconSize }, col);

        // クールダウンの残り（castTimer は castInterval から 0 へ減る）。円の上から cd の割合だけ暗く
        const float cd = (sl.interval > 0.0f) ? Clamp01(sl.timer / sl.interval) : 0.0f;
        if (cd > 0.0f)
            sprite.Draw(disc, { x, top }, { size, size * cd }, m_Style.cooldownColor, { 0.0f, 0.0f, 1.0f, cd });

        if (!mp.CanAfford(sl.cost))
            sprite.Draw(disc, { x, top }, { size, size }, m_Style.noManaColor);
        if (wand.castingPaused)
            sprite.Draw(disc, { x, top }, { size, size }, { 0.0f, 0.0f, 0.0f, 0.55f });

        if (m_Style.drawBorder && deco.ring)
            sprite.Draw(deco.ring, { x, top }, { size, size }, m_Style.borderColor);
        else if (m_Style.drawBorder)
            DrawBorder(sprite, { x, top }, { size, size });

        x += size + gap;
        ++index;
    }
}

// ============================================================
// 金貨（2026-10-04）：硬貨の絵 + 枚数。増えた瞬間は 0.35 秒ほど明るく、少し大きく
// ============================================================
void HUD::DrawGold(SpriteRenderer& sprite, TextRenderer& text, int gold)
{
    if (m_LastGold >= 0 && gold > m_LastGold) m_GoldPulseAt = m_Time;
    m_LastGold = gold;
    const float pulse = std::exp(-(m_Time - m_GoldPulseAt) * 8.0f);   // 1 → 0

    const Vector2 p = m_Style.goldText.Resolve(m_ScreenW, m_ScreenH);
    const float is = m_Style.goldIconSize;
    Vector4 col = m_Style.goldColor;
    col.x *= 1.0f + 0.6f * pulse;
    col.y *= 1.0f + 0.6f * pulse;
    col.z *= 1.0f + 0.6f * pulse;
    if (m_GoldIcon)
    {
        const float o = m_Style.textShadowOffset + 0.5f;
        sprite.Draw(m_GoldIcon, { p.x + o, p.y + o }, { is, is }, m_Style.shadowColor);
        sprite.Draw(m_GoldIcon, p, { is, is }, col);
    }

    wchar_t buf[24];
    swprintf_s(buf, L"%d", gold);
    const float s = m_Style.goldTextScale * (1.0f + 0.12f * pulse);
    const Vector2 ts = text.Measure(buf, s);
    const Vector2 tp = { p.x + is + 6.0f, p.y + (is - ts.y) * 0.5f };
    if (m_Style.textShadow)
    {
        const float o = m_Style.textShadowOffset;
        text.Draw(buf, { tp.x + o, tp.y + o }, m_Style.shadowColor, s);
    }
    text.Draw(buf, tp, col, s);
}

// ============================================================
// 魔力解放（Q）の大きな欄（2026-10-04）
//   魔法の欄の上の中央。魔法の欄と同じ作りを大きくし、後ろは星の魔法陣（魔法の欄は花）。
//   使える     : 絵が明るく、魔法陣がゆっくり息をする。使えるようになった瞬間に一度光る
//   解放中     : 輪と魔法陣が金に光り（HDR）、速く回る。絵は金に沈め、中央に残り秒（小数 1 桁、白）
//   再使用待ち : 上から残りの割合だけ暗く、絵も沈む。中央に残り秒（切り上げ）
// ============================================================
void HUD::DrawSurgeSkill(SpriteRenderer& sprite, TextRenderer& text, const ManaComponent& mp)
{
    const float size = m_Style.surgeSkillSize;
    const Vector2 a = m_Style.surgeSkill.Resolve(m_ScreenW, m_ScreenH);
    const Vector2 center = { a.x, a.y - size * 0.5f };
    const Vector2 tl = { center.x - size * 0.5f, center.y - size * 0.5f };

    const bool active = mp.SurgeActive();
    const bool ready = !active && mp.surgeCooldownLeft <= 0.0f;
    const float cd = (!active && mp.surgeCooldown > 0.0f) ? Clamp01(mp.surgeCooldownLeft / mp.surgeCooldown) : 0.0f;

    const UIDeco::Textures& deco = UIDeco::Tex();
    const auto& disc = deco.disc ? deco.disc : m_WhiteTex;
    const Vector4 gold = UIDeco::TintColor(UIDeco::Tint::Gold);
    const Vector4& glow = m_Style.surgeActiveColor;
    const float breathe = 0.5f + 0.5f * std::sin(m_Time * 2.5f);   // 使える時の息（0..1）
    const float pulse = 0.5f + 0.5f * std::sin(m_Time * 10.0f);    // 解放中の脈（0..1）

    // ---- 後ろ：暗い円 → （解放中）金の光 → 魔法陣 ----
    if (m_Style.surgeCircleScale > 0.0f)
    {
        const float d = size * m_Style.surgeCircleScale;
        sprite.Draw(disc, { center.x - d * 0.5f, center.y - d * 0.5f }, { d, d }, { 0.0f, 0.0f, 0.0f, 0.55f });
        if (active)
        {
            const float g = d * (1.12f + 0.06f * pulse);
            sprite.Draw(disc, { center.x - g * 0.5f, center.y - g * 0.5f }, { g, g },
                { glow.x, glow.y, glow.z, 0.22f + 0.12f * pulse });
        }

        Vector4 ring = active ? glow : gold;
        ring.w = active ? 1.0f : (ready ? 0.65f + 0.30f * breathe : 0.40f);
        const float spin = UIDeco::Clock() * (active ? 1.2f : 0.15f);
        UIDeco::DrawCircle(sprite, true, center, d, ring, spin);
    }

    // ---- 欄の地と絵 ----
    sprite.Draw(disc, tl, { size, size }, m_Style.slotBgColor);

    const float iconSize = size * 0.62f;
    const Vector2 ip = { center.x - iconSize * 0.5f, center.y - iconSize * 0.5f };
    Vector4 iconCol = m_Style.textColor;
    if (active)   // 金に沈めて、上に重ねる残り秒（白）を読めるようにする
        iconCol = { glow.x * 0.25f, glow.y * 0.25f, glow.z * 0.25f, 1.0f };
    else if (!ready)
        iconCol = { iconCol.x * 0.55f, iconCol.y * 0.55f, iconCol.z * 0.55f, 1.0f };
    if (m_SurgeIcon) sprite.Draw(m_SurgeIcon, ip, { iconSize, iconSize }, iconCol);
    else             sprite.Draw(disc, ip, { iconSize, iconSize }, iconCol);

    // 再使用待ち：上から残りの割合だけ暗く（魔法の欄と同じ）
    if (cd > 0.0f)
        sprite.Draw(disc, tl, { size, size * cd }, m_Style.cooldownColor, { 0.0f, 0.0f, 1.0f, cd });

    // 使えるようになった瞬間の光
    if (m_SurgeReadyFlash > 0.0f)
        sprite.Draw(disc, tl, { size, size }, { glow.x, glow.y, glow.z, 0.55f * m_SurgeReadyFlash });

    // 縁の輪（解放中は金に光る）
    if (deco.ring)
    {
        Vector4 edge = m_Style.borderColor;
        if (active) edge = { glow.x, glow.y, glow.z, 1.0f };
        sprite.Draw(deco.ring, tl, { size, size }, edge);
    }
    else
        DrawBorder(sprite, tl, { size, size });

    // ---- 中央の残り秒 ----
    wchar_t buf[16] = {};
    if (active)
        swprintf_s(buf, L"%.1f", mp.surgeTime);
    else if (!ready)
        swprintf_s(buf, L"%d", CeilInt(mp.surgeCooldownLeft));
    if (buf[0])
    {
        const float s = m_Style.surgeNumberScale;
        const Vector2 ts = text.Measure(buf, s);
        const Vector2 tp = { center.x - ts.x * 0.5f, center.y - ts.y * 0.5f };
        const float o = m_Style.textShadowOffset + 0.5f;
        text.Draw(buf, { tp.x + o, tp.y + o }, m_Style.shadowColor, s);
        text.Draw(buf, tp, active ? Vector4(1.3f, 1.2f, 1.0f, 1.0f) : m_Style.textColor, s);   // 解放中は少し光る白
    }

    // ---- 下の縁の「Q」の札 ----
    {
        const float k = size * 0.34f;
        const Vector2 kc = { center.x, tl.y + size };
        const Vector2 kp = { kc.x - k * 0.5f, kc.y - k * 0.5f };
        sprite.Draw(disc, kp, { k, k }, m_Style.slotBgColor);
        if (deco.ring) sprite.Draw(deco.ring, kp, { k, k }, active ? Vector4(glow.x, glow.y, glow.z, 1.0f) : m_Style.borderColor);
        const Vector2 ts = text.Measure(L"Q", m_Style.surgeKeyScale);
        DrawLabel(text, L"Q", { kc.x - ts.x * 0.5f, kc.y - ts.y * 0.5f }, m_Style.surgeKeyScale);
    }
}

// ============================================================
// 瀕死の赤い縁
//   太さの違う枠を重ねて、外側ほど濃いぼかしに見せる（テクスチャを持たないため）。
//   HP が低いほど濃く、脈打つように明滅させる
// ============================================================
void HUD::DrawLowHpVignette(SpriteRenderer& sprite, const HealthComponent& hp)
{
    if (!m_Style.lowHpVignette || hp.max <= 0.0f || hp.current <= 0.0f) return;
    if (m_Style.lowHpRatio <= 0.0f) return;

    const float ratio = hp.current / hp.max;
    if (ratio >= m_Style.lowHpRatio) return;

    const float k = 1.0f - ratio / m_Style.lowHpRatio;   // 0（境目）〜 1（瀕死）
    const float pulse = 0.7f + 0.3f * std::sin(m_Time * m_Style.vignettePulse);
    DrawEdgeGlow(sprite, m_Style.vignetteColor, m_Style.vignetteColor.w * (0.35f + 0.65f * k) * pulse,
        m_Style.vignetteWidth);
}

// ============================================================
// 魔力解放中の金の縁（2026-10-01）。出始め 0.2 秒で濃くなり、終わり 0.4 秒で消える。ゆっくり脈打つ
// ============================================================
void HUD::DrawSurgeVignette(SpriteRenderer& sprite, const ManaComponent& mp)
{
    if (!m_Style.surgeVignette || !mp.SurgeActive()) return;
    const float in = Clamp01((mp.surgeDuration - mp.surgeTime) / 0.2f);
    const float out = Clamp01(mp.surgeTime / 0.4f);
    const float pulse = 0.8f + 0.2f * std::sin(m_Time * 6.0f);
    DrawEdgeGlow(sprite, m_Style.surgeVignetteColor, m_Style.surgeVignetteColor.w * in * out * pulse,
        m_Style.surgeVignetteWidth);
}

// ============================================================
// 画面の縁のぼかし（瀕死の赤・魔力解放の金で共用）
//   太さの違う枠を重ねて、外側ほど濃いぼかしに見せる（テクスチャを持たないため）。
//   外端の濃さ E に対して、薄い層を N 枚重ねる：1 枚の濃さ p = 1 - (1 - E)^(1/N)。
//   一番内側は 1 枚だけ（ほぼ透明）、外へ行くほど重なって E に近づく
// ============================================================
void HUD::DrawEdgeGlow(SpriteRenderer& sprite, const Vector4& color, float edgeAlpha, float widthRatio)
{
    edgeAlpha = Clamp01(edgeAlpha);
    if (edgeAlpha <= 0.0f) return;
    const float thick = (std::min)(m_ScreenW, m_ScreenH) * widthRatio;

    constexpr int kLayers = 12;
    Vector4 c = color;
    c.w = 1.0f - std::pow(1.0f - edgeAlpha, 1.0f / (float)kLayers);

    for (int i = 1; i <= kLayers; ++i)
    {
        const float w = thick * (float)i / (float)kLayers;
        sprite.Draw(m_WhiteTex, { 0.0f, 0.0f }, { m_ScreenW, w }, c);
        sprite.Draw(m_WhiteTex, { 0.0f, m_ScreenH - w }, { m_ScreenW, w }, c);
        sprite.Draw(m_WhiteTex, { 0.0f, w }, { w, m_ScreenH - w * 2.0f }, c);
        sprite.Draw(m_WhiteTex, { m_ScreenW - w, w }, { w, m_ScreenH - w * 2.0f }, c);
    }
}

// ============================================================
// 画面外の目印
//   見えている物には出さない。外にある物は、画面の中心からその方向へ伸ばして
//   内側の枠（markerMargin）に当たった所に「>」を出す。
//   カメラの後ろの物は |w| で割って左右を保ち、必ず縁に出す
// ============================================================
void HUD::DrawMarkers(SpriteRenderer& sprite, const HUDFrameInfo& info)
{
    if (!m_Style.showMarkers || info.markers.empty()) return;

    const Vector2 center = { m_ScreenW * 0.5f, m_ScreenH * 0.5f };
    const float m = m_Style.markerMargin;
    const float hx = center.x - m;
    const float hy = center.y - m;
    if (hx <= 0.0f || hy <= 0.0f) return;

    for (const auto& mk : info.markers)
    {
        const Vector4 clip = Vector4::Transform(
            Vector4(mk.position.x, mk.position.y, mk.position.z, 1.0f), info.viewProj);
        const bool behind = clip.w <= 1.0e-4f;
        const float w = (std::max)(std::fabs(clip.w), 1.0e-4f);
        const Vector2 p = {
            (clip.x / w * 0.5f + 0.5f) * m_ScreenW,
            (0.5f - clip.y / w * 0.5f) * m_ScreenH
        };

        const bool inside = !behind && std::fabs(p.x - center.x) <= hx && std::fabs(p.y - center.y) <= hy;
        if (inside) continue;

        Vector2 d = p - center;
        if (d.LengthSquared() < 1.0e-4f) d = { 0.0f, 1.0f };
        const float s = (std::min)(hx / (std::max)(std::fabs(d.x), 1.0e-4f),
                                   hy / (std::max)(std::fabs(d.y), 1.0e-4f));
        const Vector2 tip = center + d * s;
        const float ang = std::atan2(d.y, d.x);

        // 「>」：先端から後ろへ 2 本の棒。回転の中心は先端
        const float len = m_Style.markerSize;
        const float thick = (std::max)(2.0f, len * 0.22f);
        Vector4 col = mk.color;
        col.w = 0.95f;
        for (float spread : { 0.65f, -0.65f })
        {
            sprite.Draw(m_WhiteTex, { tip.x, tip.y - thick * 0.5f }, { len, thick },
                col, ang + 3.14159265f + spread, tip);
        }

        // 後ろに小さな菱形（何の目印か色で分かるように）
        const float gem = len * 0.55f;
        const Vector2 gc = tip - Vector2(std::cos(ang), std::sin(ang)) * (len * 1.25f);
        sprite.Draw(m_WhiteTex, { gc.x - gem * 0.5f, gc.y - gem * 0.5f }, { gem, gem },
            col, 0.785398f, gc);
    }
}

void HUD::DrawBar(SpriteRenderer& sprite,
    const Vector2& pos, const Vector2& size,
    float ratio, float trailRatio, const Vector4& fillColor, bool gems)
{
    // 背景（枠を兼ねる）
    sprite.Draw(m_WhiteTex, pos, size, m_Style.bgColor);

    const float pad = m_Style.barPadding;
    const float innerW = size.x - pad * 2.0f;
    const float innerH = size.y - pad * 2.0f;
    if (innerW <= 0.0f || innerH <= 0.0f) return;

    const Vector2 innerPos = { pos.x + pad, pos.y + pad };

    // 残像（中身より広い分だけ。隠れる部分は描かない）
    if (m_Style.damageTrail && trailRatio > ratio)
    {
        const float from = innerW * ratio;
        const float to = innerW * Clamp01(trailRatio);
        sprite.Draw(m_WhiteTex,
            { innerPos.x + from, innerPos.y },
            { to - from, innerH },
            m_Style.trailColor);
    }

    // 中身（下 4 割を少し暗くして厚みを出す）
    const float w = innerW * ratio;
    if (w > 0.0f)
    {
        sprite.Draw(m_WhiteTex, innerPos, { w, innerH }, fillColor);
        sprite.Draw(m_WhiteTex, { innerPos.x, innerPos.y + innerH * 0.6f }, { w, innerH * 0.4f },
            { 0.0f, 0.0f, 0.0f, 0.30f });
    }

    if (m_Style.drawBorder)
        DrawBorder(sprite, pos, size);

    // 両端の菱形（枠の色）
    if (gems && m_Style.gemSize > 0.0f)
    {
        const float g = m_Style.gemSize;
        const float cy = pos.y + size.y * 0.5f;
        for (const float cx : { pos.x, pos.x + size.x })
            sprite.Draw(m_WhiteTex, { cx - g * 0.5f, cy - g * 0.5f }, { g, g },
                m_Style.borderColor, 0.785398f, { cx, cy });
    }
}

// 枠は4本の細い矩形。矩形しか描けないので線ではなくこの形にする
void HUD::DrawBorder(SpriteRenderer& sprite,
    const Vector2& pos, const Vector2& size)
{
    const float t = m_Style.borderSize;
    if (t <= 0.0f) return;

    const Vector4& c = m_Style.borderColor;
    sprite.Draw(m_WhiteTex, pos, { size.x, t }, c);
    sprite.Draw(m_WhiteTex, { pos.x, pos.y + size.y - t }, { size.x, t }, c);
    sprite.Draw(m_WhiteTex, { pos.x, pos.y + t }, { t, size.y - t * 2.0f }, c);
    sprite.Draw(m_WhiteTex, { pos.x + size.x - t, pos.y + t },
        { t, size.y - t * 2.0f }, c);
}

void HUD::DrawLabel(TextRenderer& text, const std::wstring& str,
    const Vector2& pos, float scale)
{
    if (m_Style.textShadow)
    {
        const float o = m_Style.textShadowOffset;
        text.Draw(str, { pos.x + o, pos.y + o }, m_Style.shadowColor, scale);
    }
    text.Draw(str, pos, m_Style.textColor, scale);
}

void HUD::DrawBarLabel(TextRenderer& text, const std::wstring& str,
    const Vector2& barPos, const Vector2& barSize)
{
    const float scale = m_Style.barTextScale;

    Vector2 pos = {
        barPos.x + m_Style.barTextOffset.x,
        barPos.y + m_Style.barTextOffset.y
    };

    if (m_Style.centerBarText)
    {
        const Vector2 sz = text.Measure(str, scale);
        pos.x = barPos.x + (barSize.x - sz.x) * 0.5f;
        pos.y = barPos.y + (barSize.y - sz.y) * 0.5f;
    }

    DrawLabel(text, str, pos, scale);
}

// ============================================================
// ImGui（呼ぶ側は CollapsingHeader の中で呼ぶ）
// ============================================================
void HUD::DrawDebugUI()
{
    if (ImGui::Button("Save"))  SaveStyle();
    ImGui::SameLine();
    if (ImGui::Button("Load"))  LoadStyle();
    ImGui::SameLine();
    if (ImGui::Button("Reset")) ResetStyle();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", Res::Cfg::HUD);

    ImGui::Separator();
    ImGui::Text("Layout");
    DragAnchor("Exp Bar", m_Style.expBar);
    ImGui::DragFloat("Exp Height", &m_Style.expBarHeight, 0.5f, 1.0f, 200.0f);
    ImGui::DragFloat("Exp Margin", &m_Style.expBarMargin, 1.0f, 0.0f, 800.0f);
    DragAnchor("Lv Text", m_Style.lvText);
    DragAnchor("HP Bar", m_Style.hpBar);
    DragAnchor("MP Bar", m_Style.mpBar);
    ImGui::DragFloat2("HP Bar Size", &m_Style.hpBarSize.x, 1.0f, 4.0f, 2000.0f);
    ImGui::DragFloat2("MP Bar Size", &m_Style.mpBarSize.x, 1.0f, 4.0f, 2000.0f);
    ImGui::DragFloat("Bar Padding", &m_Style.barPadding, 0.25f, 0.0f, 20.0f);

    ImGui::Separator();
    ImGui::Text("Text");
    ImGui::DragFloat("Lv Scale", &m_Style.lvTextScale, 0.01f, 0.05f, 3.0f);
    ImGui::DragFloat("Bar Text Scale", &m_Style.barTextScale, 0.01f, 0.05f, 3.0f);
    ImGui::Checkbox("Center Bar Text", &m_Style.centerBarText);
    if (!m_Style.centerBarText)
    {
        ImGui::DragFloat2("Bar Text Offset",
            &m_Style.barTextOffset.x, 0.5f, -200.0f, 200.0f);
    }
    ImGui::Checkbox("Text Shadow", &m_Style.textShadow);
    if (m_Style.textShadow)
        ImGui::DragFloat("Shadow Offset", &m_Style.textShadowOffset, 0.1f, 0.0f, 8.0f);

    ImGui::Separator();
    ImGui::Text("Damage Trail");
    ImGui::Checkbox("Enable Trail", &m_Style.damageTrail);
    if (m_Style.damageTrail)
    {
        ImGui::DragFloat("Trail Delay", &m_Style.trailDelay, 0.01f, 0.0f, 3.0f);
        ImGui::DragFloat("Trail Speed", &m_Style.trailSpeed, 0.05f, 0.05f, 10.0f);
        ImGui::Text("hp trail %.3f   mp trail %.3f",
            m_HpTrail.value, m_MpTrail.value);
    }

    ImGui::Separator();
    ImGui::Text("Border");
    ImGui::Checkbox("Draw Border", &m_Style.drawBorder);
    if (m_Style.drawBorder)
        ImGui::DragFloat("Border Size", &m_Style.borderSize, 0.1f, 0.0f, 10.0f);
    ImGui::DragFloat("Bar End Gems", &m_Style.gemSize, 0.25f, 0.0f, 40.0f);

    ImGui::Separator();
    ImGui::Text("Colors");
    ImGui::ColorEdit4("Background", &m_Style.bgColor.x);
    ImGui::ColorEdit4("HP", &m_Style.hpColor.x);
    ImGui::ColorEdit4("MP", &m_Style.mpColor.x);
    ImGui::ColorEdit4("MP (surge)", &m_Style.mpSurgeColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::ColorEdit4("Exp", &m_Style.expColor.x);
    ImGui::ColorEdit4("Trail", &m_Style.trailColor.x);
    ImGui::ColorEdit4("Border Color", &m_Style.borderColor.x);
    ImGui::ColorEdit4("Text", &m_Style.textColor.x);
    ImGui::ColorEdit4("Shadow", &m_Style.shadowColor.x);

    ImGui::Separator();
    ImGui::Text("Run Info (time / kills)");
    DragAnchor("Run Info", m_Style.runInfo);
    ImGui::DragFloat("Timer Scale", &m_Style.timerScale, 0.01f, 0.05f, 3.0f);
    ImGui::DragFloat("Kill Scale", &m_Style.killScale, 0.01f, 0.05f, 3.0f);
    ImGui::DragFloat("Divider Width", &m_Style.runDividerWidth, 1.0f, 0.0f, 800.0f);

    ImGui::Separator();
    ImGui::Text("Spell Bar");
    ImGui::Checkbox("Show Spell Bar", &m_Style.showSpellBar);
    DragAnchor("Spell Bar", m_Style.spellBar);
    ImGui::DragFloat("Slot Size", &m_Style.slotSize, 0.5f, 8.0f, 300.0f);
    ImGui::DragFloat("Slot Gap", &m_Style.slotGap, 0.25f, 0.0f, 100.0f);
    ImGui::DragFloat("Magic Circle Scale", &m_Style.slotCircleScale, 0.01f, 0.0f, 4.0f);
    ImGui::ColorEdit4("Slot Bg", &m_Style.slotBgColor.x);
    ImGui::ColorEdit4("Cooldown", &m_Style.cooldownColor.x);
    ImGui::ColorEdit4("No Mana", &m_Style.noManaColor.x);

    ImGui::Separator();
    ImGui::Text("Gold");
    ImGui::Checkbox("Show Gold", &m_Style.showGold);
    DragAnchor("Gold", m_Style.goldText);
    ImGui::DragFloat("Gold Icon Size", &m_Style.goldIconSize, 0.5f, 4.0f, 200.0f);
    ImGui::DragFloat("Gold Text Scale", &m_Style.goldTextScale, 0.01f, 0.05f, 3.0f);
    ImGui::ColorEdit4("Gold Color", &m_Style.goldColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

    ImGui::Separator();
    ImGui::Text("Mana Surge Skill (Q)");
    ImGui::Checkbox("Show Surge Skill", &m_Style.showSurgeSkill);
    DragAnchor("Surge Skill", m_Style.surgeSkill);
    ImGui::DragFloat("Surge Size", &m_Style.surgeSkillSize, 0.5f, 8.0f, 300.0f);
    ImGui::DragFloat("Surge Circle Scale", &m_Style.surgeCircleScale, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Surge Number Scale", &m_Style.surgeNumberScale, 0.01f, 0.05f, 3.0f);
    ImGui::DragFloat("Surge Key Scale", &m_Style.surgeKeyScale, 0.01f, 0.05f, 3.0f);
    ImGui::ColorEdit4("Surge Active", &m_Style.surgeActiveColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

    ImGui::Separator();
    ImGui::Text("Low HP Vignette");
    ImGui::Checkbox("Enable Vignette", &m_Style.lowHpVignette);
    ImGui::SliderFloat("Below HP Ratio", &m_Style.lowHpRatio, 0.0f, 1.0f);
    ImGui::DragFloat("Edge Width", &m_Style.vignetteWidth, 0.005f, 0.0f, 0.5f);
    ImGui::DragFloat("Pulse Speed", &m_Style.vignettePulse, 0.1f, 0.0f, 30.0f);
    ImGui::ColorEdit4("Vignette", &m_Style.vignetteColor.x);

    ImGui::Separator();
    ImGui::Text("Mana Surge Edge");
    ImGui::Checkbox("Enable Surge Edge", &m_Style.surgeVignette);
    ImGui::DragFloat("Surge Edge Width", &m_Style.surgeVignetteWidth, 0.005f, 0.0f, 0.5f);
    ImGui::ColorEdit4("Surge Edge", &m_Style.surgeVignetteColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);

    ImGui::Separator();
    ImGui::Text("Off-screen Markers");
    ImGui::Checkbox("Show Markers", &m_Style.showMarkers);
    ImGui::DragFloat("Marker Size", &m_Style.markerSize, 0.25f, 2.0f, 200.0f);
    ImGui::DragFloat("Marker Margin", &m_Style.markerMargin, 0.5f, 0.0f, 400.0f);

    ImGui::Separator();
    ImGui::Text("Screen : %.0f x %.0f", m_ScreenW, m_ScreenH);
}

// ============================================================
// JSON 保存 / 読み込み
// ============================================================
bool HUD::SaveStyle(const char* path) const
{
    const char* file = path ? path : Res::Cfg::HUD;

    json root;
    root["expBar"] = ToJson(m_Style.expBar);
    root["expBarHeight"] = m_Style.expBarHeight;
    root["expBarMargin"] = m_Style.expBarMargin;
    root["lvText"] = ToJson(m_Style.lvText);
    root["hpBar"] = ToJson(m_Style.hpBar);
    root["mpBar"] = ToJson(m_Style.mpBar);
    root["hpBarSize"] = ToJson(m_Style.hpBarSize);
    root["mpBarSize"] = ToJson(m_Style.mpBarSize);
    root["barPadding"] = m_Style.barPadding;

    root["lvTextScale"] = m_Style.lvTextScale;
    root["barTextScale"] = m_Style.barTextScale;
    root["centerBarText"] = m_Style.centerBarText;
    root["barTextOffset"] = ToJson(m_Style.barTextOffset);
    root["textShadow"] = m_Style.textShadow;
    root["textShadowOffset"] = m_Style.textShadowOffset;

    root["damageTrail"] = m_Style.damageTrail;
    root["trailDelay"] = m_Style.trailDelay;
    root["trailSpeed"] = m_Style.trailSpeed;

    root["drawBorder"] = m_Style.drawBorder;
    root["borderSize"] = m_Style.borderSize;
    root["gemSize"] = m_Style.gemSize;

    root["bgColor"] = ToJson(m_Style.bgColor);
    root["hpColor"] = ToJson(m_Style.hpColor);
    root["mpColor"] = ToJson(m_Style.mpColor);
    root["mpSurgeColor"] = ToJson(m_Style.mpSurgeColor);
    root["expColor"] = ToJson(m_Style.expColor);
    root["trailColor"] = ToJson(m_Style.trailColor);
    root["borderColor"] = ToJson(m_Style.borderColor);
    root["textColor"] = ToJson(m_Style.textColor);
    root["shadowColor"] = ToJson(m_Style.shadowColor);

    root["runInfo"] = ToJson(m_Style.runInfo);
    root["timerScale"] = m_Style.timerScale;
    root["killScale"] = m_Style.killScale;
    root["runDividerWidth"] = m_Style.runDividerWidth;

    root["showSpellBar"] = m_Style.showSpellBar;
    root["spellBar"] = ToJson(m_Style.spellBar);
    root["slotSize"] = m_Style.slotSize;
    root["slotGap"] = m_Style.slotGap;
    root["slotCircleScale"] = m_Style.slotCircleScale;
    root["slotBgColor"] = ToJson(m_Style.slotBgColor);
    root["cooldownColor"] = ToJson(m_Style.cooldownColor);
    root["noManaColor"] = ToJson(m_Style.noManaColor);

    root["showGold"] = m_Style.showGold;
    root["goldText"] = ToJson(m_Style.goldText);
    root["goldIconSize"] = m_Style.goldIconSize;
    root["goldTextScale"] = m_Style.goldTextScale;
    root["goldColor"] = ToJson(m_Style.goldColor);

    root["showSurgeSkill"] = m_Style.showSurgeSkill;
    root["surgeSkill"] = ToJson(m_Style.surgeSkill);
    root["surgeSkillSize"] = m_Style.surgeSkillSize;
    root["surgeCircleScale"] = m_Style.surgeCircleScale;
    root["surgeNumberScale"] = m_Style.surgeNumberScale;
    root["surgeKeyScale"] = m_Style.surgeKeyScale;
    root["surgeActiveColor"] = ToJson(m_Style.surgeActiveColor);

    root["lowHpVignette"] = m_Style.lowHpVignette;
    root["lowHpRatio"] = m_Style.lowHpRatio;
    root["vignetteWidth"] = m_Style.vignetteWidth;
    root["vignettePulse"] = m_Style.vignettePulse;
    root["vignetteColor"] = ToJson(m_Style.vignetteColor);
    root["surgeVignette"] = m_Style.surgeVignette;
    root["surgeVignetteWidth"] = m_Style.surgeVignetteWidth;
    root["surgeVignetteColor"] = ToJson(m_Style.surgeVignetteColor);

    root["showMarkers"] = m_Style.showMarkers;
    root["markerSize"] = m_Style.markerSize;
    root["markerMargin"] = m_Style.markerMargin;

    std::ofstream ofs(file);
    if (!ofs.is_open())
    {
        std::cout << "[Error] HUD: failed to save " << file << std::endl;
        return false;
    }

    ofs << root.dump(4);
    std::cout << "[OK] HUD style saved: " << file << std::endl;
    return true;
}

bool HUD::LoadStyle(const char* path)
{
    const char* file = path ? path : Res::Cfg::HUD;

    std::ifstream ifs(file);
    if (!ifs.is_open())
    {
        // 未保存はエラーではない（既定値のまま動く）
        std::cout << "[Info] HUD: no style file, using defaults" << std::endl;
        return false;
    }

    json root;
    try
    {
        ifs >> root;
    }
    catch (const json::exception& e)
    {
        std::cout << "[Error] HUD: json parse error: " << e.what() << std::endl;
        return false;
    }

    ReadAnchor(root, "expBar", m_Style.expBar);
    ReadFloat(root, "expBarHeight", m_Style.expBarHeight);
    ReadFloat(root, "expBarMargin", m_Style.expBarMargin);
    ReadAnchor(root, "lvText", m_Style.lvText);
    ReadAnchor(root, "hpBar", m_Style.hpBar);
    ReadAnchor(root, "mpBar", m_Style.mpBar);
    // 旧形式の barSize は両方へ流し込み、新キーがあれば上書きする
    ReadVec2(root, "barSize", m_Style.hpBarSize);
    ReadVec2(root, "barSize", m_Style.mpBarSize);
    ReadVec2(root, "hpBarSize", m_Style.hpBarSize);
    ReadVec2(root, "mpBarSize", m_Style.mpBarSize);
    ReadFloat(root, "barPadding", m_Style.barPadding);

    ReadFloat(root, "lvTextScale", m_Style.lvTextScale);
    ReadFloat(root, "barTextScale", m_Style.barTextScale);
    ReadBool(root, "centerBarText", m_Style.centerBarText);
    ReadVec2(root, "barTextOffset", m_Style.barTextOffset);
    ReadBool(root, "textShadow", m_Style.textShadow);
    ReadFloat(root, "textShadowOffset", m_Style.textShadowOffset);

    ReadBool(root, "damageTrail", m_Style.damageTrail);
    ReadFloat(root, "trailDelay", m_Style.trailDelay);
    ReadFloat(root, "trailSpeed", m_Style.trailSpeed);

    ReadBool(root, "drawBorder", m_Style.drawBorder);
    ReadFloat(root, "borderSize", m_Style.borderSize);
    ReadFloat(root, "gemSize", m_Style.gemSize);

    ReadVec4(root, "bgColor", m_Style.bgColor);
    ReadVec4(root, "hpColor", m_Style.hpColor);
    ReadVec4(root, "mpColor", m_Style.mpColor);
    ReadVec4(root, "mpSurgeColor", m_Style.mpSurgeColor);
    ReadVec4(root, "expColor", m_Style.expColor);
    ReadVec4(root, "trailColor", m_Style.trailColor);
    ReadVec4(root, "borderColor", m_Style.borderColor);
    ReadVec4(root, "textColor", m_Style.textColor);
    ReadVec4(root, "shadowColor", m_Style.shadowColor);

    ReadAnchor(root, "runInfo", m_Style.runInfo);
    ReadFloat(root, "timerScale", m_Style.timerScale);
    ReadFloat(root, "killScale", m_Style.killScale);
    ReadFloat(root, "runDividerWidth", m_Style.runDividerWidth);

    ReadBool(root, "showSpellBar", m_Style.showSpellBar);
    ReadAnchor(root, "spellBar", m_Style.spellBar);
    ReadFloat(root, "slotSize", m_Style.slotSize);
    ReadFloat(root, "slotGap", m_Style.slotGap);
    ReadFloat(root, "slotCircleScale", m_Style.slotCircleScale);
    ReadVec4(root, "slotBgColor", m_Style.slotBgColor);
    ReadVec4(root, "cooldownColor", m_Style.cooldownColor);
    ReadVec4(root, "noManaColor", m_Style.noManaColor);

    ReadBool(root, "showGold", m_Style.showGold);
    ReadAnchor(root, "goldText", m_Style.goldText);
    ReadFloat(root, "goldIconSize", m_Style.goldIconSize);
    ReadFloat(root, "goldTextScale", m_Style.goldTextScale);
    ReadVec4(root, "goldColor", m_Style.goldColor);

    ReadBool(root, "showSurgeSkill", m_Style.showSurgeSkill);
    ReadAnchor(root, "surgeSkill", m_Style.surgeSkill);
    ReadFloat(root, "surgeSkillSize", m_Style.surgeSkillSize);
    ReadFloat(root, "surgeCircleScale", m_Style.surgeCircleScale);
    ReadFloat(root, "surgeNumberScale", m_Style.surgeNumberScale);
    ReadFloat(root, "surgeKeyScale", m_Style.surgeKeyScale);
    ReadVec4(root, "surgeActiveColor", m_Style.surgeActiveColor);

    ReadBool(root, "lowHpVignette", m_Style.lowHpVignette);
    ReadFloat(root, "lowHpRatio", m_Style.lowHpRatio);
    ReadFloat(root, "vignetteWidth", m_Style.vignetteWidth);
    ReadFloat(root, "vignettePulse", m_Style.vignettePulse);
    ReadVec4(root, "vignetteColor", m_Style.vignetteColor);
    ReadBool(root, "surgeVignette", m_Style.surgeVignette);
    ReadFloat(root, "surgeVignetteWidth", m_Style.surgeVignetteWidth);
    ReadVec4(root, "surgeVignetteColor", m_Style.surgeVignetteColor);

    ReadBool(root, "showMarkers", m_Style.showMarkers);
    ReadFloat(root, "markerSize", m_Style.markerSize);
    ReadFloat(root, "markerMargin", m_Style.markerMargin);

    std::cout << "[OK] HUD style loaded: " << file << std::endl;
    return true;
}

void HUD::ResetStyle()
{
    m_Style = HUDStyle{};
    std::cout << "[Info] HUD style reset to defaults" << std::endl;
}
