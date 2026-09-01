// ============================================================
// HUD.cpp
// ============================================================
#include "UI/HUD.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Renderer/TextRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/HealthComponent.h"
#include "Component/ManaComponent.h"
#include "Player/LevelComponent.h"
#include "ResourcePaths.h"
#include "imgui.h"

#include <nlohmann/json.hpp>
#include <fstream>
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

    // ---- ImGui の小道具 ----
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
    m_HpTrail.Update(dt, SafeRatio(hp.current, hp.max), m_Style);
    m_MpTrail.Update(dt, SafeRatio(mp.current, mp.max), m_Style);
}

// ============================================================
// 描画
// ============================================================
void HUD::Draw(SpriteRenderer& sprite, TextRenderer& text,
    const HealthComponent& hp, const ManaComponent& mp,
    const LevelComponent& lv)
{
    if (!m_WhiteTex) return;

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
        SafeRatio(hp.current, hp.max), m_HpTrail.value, m_Style.hpColor);
    DrawBar(sprite, mpPos, m_Style.mpBarSize,
        SafeRatio(mp.current, mp.max), m_MpTrail.value, m_Style.mpColor);

    // ---- 数値 ----
    wchar_t buf[32];

    swprintf_s(buf, L"Lv %d", lv.level);
    DrawLabel(text, buf, m_Style.lvText.Resolve(m_ScreenW, m_ScreenH),
        m_Style.lvTextScale);

    swprintf_s(buf, L"HP %d/%d", CeilInt(hp.current), (int)hp.max);
    DrawBarLabel(text, buf, hpPos, m_Style.hpBarSize);

    swprintf_s(buf, L"MP %d/%d", CeilInt(mp.current), (int)mp.max);
    DrawBarLabel(text, buf, mpPos, m_Style.mpBarSize);
}

void HUD::DrawBar(SpriteRenderer& sprite,
    const Vector2& pos, const Vector2& size,
    float ratio, float trailRatio, const Vector4& fillColor)
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

    // 中身
    const float w = innerW * ratio;
    if (w > 0.0f)
        sprite.Draw(m_WhiteTex, innerPos, { w, innerH }, fillColor);

    if (m_Style.drawBorder)
        DrawBorder(sprite, pos, size);
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

    ImGui::Separator();
    ImGui::Text("Colors");
    ImGui::ColorEdit4("Background", &m_Style.bgColor.x);
    ImGui::ColorEdit4("HP", &m_Style.hpColor.x);
    ImGui::ColorEdit4("MP", &m_Style.mpColor.x);
    ImGui::ColorEdit4("Exp", &m_Style.expColor.x);
    ImGui::ColorEdit4("Trail", &m_Style.trailColor.x);
    ImGui::ColorEdit4("Border Color", &m_Style.borderColor.x);
    ImGui::ColorEdit4("Text", &m_Style.textColor.x);
    ImGui::ColorEdit4("Shadow", &m_Style.shadowColor.x);

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

    root["bgColor"] = ToJson(m_Style.bgColor);
    root["hpColor"] = ToJson(m_Style.hpColor);
    root["mpColor"] = ToJson(m_Style.mpColor);
    root["expColor"] = ToJson(m_Style.expColor);
    root["trailColor"] = ToJson(m_Style.trailColor);
    root["borderColor"] = ToJson(m_Style.borderColor);
    root["textColor"] = ToJson(m_Style.textColor);
    root["shadowColor"] = ToJson(m_Style.shadowColor);

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

    ReadVec4(root, "bgColor", m_Style.bgColor);
    ReadVec4(root, "hpColor", m_Style.hpColor);
    ReadVec4(root, "mpColor", m_Style.mpColor);
    ReadVec4(root, "expColor", m_Style.expColor);
    ReadVec4(root, "trailColor", m_Style.trailColor);
    ReadVec4(root, "borderColor", m_Style.borderColor);
    ReadVec4(root, "textColor", m_Style.textColor);
    ReadVec4(root, "shadowColor", m_Style.shadowColor);

    std::cout << "[OK] HUD style loaded: " << file << std::endl;
    return true;
}

void HUD::ResetStyle()
{
    m_Style = HUDStyle{};
    std::cout << "[Info] HUD style reset to defaults" << std::endl;
}
