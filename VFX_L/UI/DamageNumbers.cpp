// ============================================================
// DamageNumbers.cpp
// ============================================================
#include "UI/DamageNumbers.h"
#include "UI/UIDeco.h"
#include "Core/GameSettings.h"
#include "Graphics/Renderer/TextRenderer.h"

#include "imgui.h"

using namespace DirectX::SimpleMath;

void DamageNumbers::Add(const Vector3& worldPos, float amount, Kind kind, int element)
{
    if (!style.enabled || !GameSettings::Get().damageNumbers || amount <= 0.0f) return;   // 設定（一時停止メニュー）でも切れる（2026-10-19）
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    Item it;
    it.pos = worldPos + Vector3(u(m_Rng) * style.jitter, u(m_Rng) * 0.1f, u(m_Rng) * style.jitter);
    it.amount = amount;
    it.kind = kind;
    it.element = (element > 0 && element < 9) ? element : 0;
    it.drift = u(m_Rng) * 0.4f;
    if ((int)m_Items.size() >= (std::max)(style.maxCount, 1))
        m_Items.erase(m_Items.begin());
    m_Items.push_back(it);
    ++m_TotalAdded;
    ++m_ElementCounts[it.element];
}

void DamageNumbers::Update(float dt)
{
    for (auto& it : m_Items)
    {
        it.age += dt;
        it.pos.y += style.rise * dt;
        it.pos.x += it.drift * dt;
    }
    m_Items.erase(std::remove_if(m_Items.begin(), m_Items.end(),
        [&](const Item& it) { return it.age >= style.life; }), m_Items.end());
}

// ============================================================
// 世界 → 画面。カメラの後ろ・画面外は描かない。文字は中央揃え、影を右下に 1 回
// ============================================================
void DamageNumbers::Draw(TextRenderer& text, const Matrix& viewProj, float screenW, float screenH)
{
    if (!style.enabled || !GameSettings::Get().damageNumbers || m_Items.empty()) return;
    const float ui = UIDeco::UIScale() * (std::min)(screenW, screenH) / 900.0f;
    // 字体を替えても見た目の高さが変わらないように、倍率は「毛筆体 32px の行の高さ 46.34px」を基準に正規化する（2026-10-18）
    const float fontNorm = 46.34f / (std::max)(text.GetLineHeight(1.0f), 1.0f);
    wchar_t buf[32];
    for (const auto& it : m_Items)
    {
        const Vector4 clip = Vector4::Transform(Vector4(it.pos.x, it.pos.y, it.pos.z, 1.0f), viewProj);
        if (clip.w <= 1.0e-4f) continue;
        const Vector2 p = { (clip.x / clip.w * 0.5f + 0.5f) * screenW, (0.5f - clip.y / clip.w * 0.5f) * screenH };
        if (p.x < -200.0f || p.x > screenW + 200.0f || p.y < -100.0f || p.y > screenH + 100.0f) continue;

        // 色：属性があればその色（倒した一撃も属性の色で大きく）、無ければ種類の色
        float s = style.scale * fontNorm;
        Vector4 col = (it.element > 0) ? style.elementColors[it.element] : style.normalColor;
        switch (it.kind)
        {
        case Kind::Poison: col = style.poisonColor; break;
        case Kind::Kill:   if (it.element == 0) col = style.killColor; s = style.killScale * fontNorm; break;
        case Kind::Player: col = style.playerColor; s = style.playerScale * fontNorm; break;
        default: break;
        }
        if (it.amount >= style.bigAt) s *= style.bigMul;
        const float t = it.age / (std::max)(style.life, 1e-3f);
        const float popT = (std::min)(1.0f, it.age / 0.12f);
        s *= 1.0f + style.pop * (1.0f - popT) * (1.0f - popT);
        s *= ui;
        const float alpha = (t < style.fadeFrom) ? 1.0f : (std::max)(0.0f, 1.0f - (t - style.fadeFrom) / (std::max)(1.0f - style.fadeFrom, 1e-3f));
        col.w *= alpha;

        swprintf_s(buf, L"%.0f", (std::max)(1.0f, std::round(it.amount)));   // 整数で（1 未満の毒の削りも 1）
        const Vector2 size = text.Measure(buf, s);
        const Vector2 at = { p.x - size.x * 0.5f, p.y - size.y * 0.5f };
        // 縁取り：8 方向に縁の色で描いてから本体（影は任意）
        const float o = style.outlinePx * ui;
        if (o > 0.01f)
        {
            Vector4 oc = style.outlineColor;
            oc.w *= alpha;
            static const Vector2 kDirs[8] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 0.71f, 0.71f }, { -0.71f, 0.71f }, { 0.71f, -0.71f }, { -0.71f, -0.71f } };
            for (const Vector2& d : kDirs) text.Draw(buf, at + d * o, oc, s);
        }
        if (style.shadow || o <= 0.01f)
        {
            Vector4 shadow = style.shadowColor;
            shadow.w *= alpha;
            const float off = (std::max)(1.0f, 2.0f * s / 0.5f);
            text.Draw(buf, at + Vector2(off, off), shadow, s);
        }
        text.Draw(buf, at, col, s);
    }
}

void DamageNumbers::DrawImGui()
{
    ImGui::Checkbox("Damage Numbers", &style.enabled);
    ImGui::Text("active %d  total %u", ActiveCount(), m_TotalAdded);
    ImGui::DragFloat("Life (s)", &style.life, 0.01f, 0.2f, 3.0f);
    ImGui::DragFloat("Rise (m/s)", &style.rise, 0.05f, 0.0f, 5.0f);
    ImGui::DragFloat("Scale", &style.scale, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat("Kill Scale", &style.killScale, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat("Player Scale", &style.playerScale, 0.01f, 0.1f, 2.0f);
    ImGui::DragFloat("Big At", &style.bigAt, 1.0f, 1.0f, 1000.0f);
    ImGui::DragFloat("Big Mul", &style.bigMul, 0.01f, 1.0f, 3.0f);
    ImGui::DragFloat("Pop", &style.pop, 0.01f, 0.0f, 1.5f);
    ImGui::DragFloat("Jitter (m)", &style.jitter, 0.01f, 0.0f, 1.5f);
    ImGui::DragFloat("Fade From", &style.fadeFrom, 0.01f, 0.0f, 1.0f);
    ImGui::DragInt("Max Count", &style.maxCount, 1, 10, 1000);
    ImGui::ColorEdit4("Normal", &style.normalColor.x);
    ImGui::ColorEdit4("Poison", &style.poisonColor.x);
    ImGui::ColorEdit4("Kill", &style.killColor.x);
    ImGui::ColorEdit4("Player", &style.playerColor.x);
    ImGui::ColorEdit4("Shadow", &style.shadowColor.x);
    ImGui::Checkbox("Shadow too", &style.shadow);
    ImGui::DragFloat("Outline (px)", &style.outlinePx, 0.1f, 0.0f, 6.0f);
    ImGui::ColorEdit4("Outline", &style.outlineColor.x);
    if (ImGui::TreeNode("Element Colors"))
    {
        static const char* kNames[9] = { "none", "fire", "ice", "lightning", "arcane", "earth", "poison", "wind", "light" };
        for (int i = 1; i < 9; ++i) ImGui::ColorEdit4(kNames[i], &style.elementColors[i].x);
        ImGui::TreePop();
    }
}
