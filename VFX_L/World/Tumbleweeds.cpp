// ============================================================
// Tumbleweeds.cpp
// ============================================================
#include "World/Tumbleweeds.h"
#include "World/GridWorld.h"
#include "World/WindState.h"
#include "World/LocalWind.h"
#include "ECS/Registry.h"
#include "Component/TransformComponent.h"
#include "Component/ModelComponent.h"
#include "Graphics/PrimitiveBuilder.h"
#include "Graphics/Model/Model.h"

#include <imgui.h>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr int kModelVariants = 4;

    // 枝を絡めた玉（半径 1、中心が原点）。細い四角柱の枝を、ばらばらの向きの円弧に沿って並べる。
    // 外側の弧 22 本 + 内側 10 本（中が透けすぎないように）。色は枝毎に少しずらす（頂点色、線形）
    std::shared_ptr<Model> BuildTumbleweedModel(ID3D11Device* device, uint32_t seed, const Vector3& color)
    {
        std::mt19937 rng(seed);
        auto U = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        auto randUnit = [&]()
            {
                for (;;)
                {
                    Vector3 v(U(-1.0f, 1.0f), U(-1.0f, 1.0f), U(-1.0f, 1.0f));
                    const float l = v.Length();
                    if (l > 0.2f && l <= 1.0f) return v / l;
                }
            };

        PrimitiveBuilder::HexahedronBatch batch;
        // a → b の細い四角柱（BoxVerts と同じ並び：局所の (x, y, z) = (u, w, 向き)。u × w = 向き で右手系を保つ）
        auto twig = [&](const Vector3& a, const Vector3& b, float t, const Vector4& c)
            {
                Vector3 d = b - a;
                const float len = d.Length();
                if (len < 1e-4f) return;
                d /= len;
                Vector3 u = (std::fabs(d.y) < 0.9f) ? Vector3(0, 1, 0).Cross(d) : Vector3(1, 0, 0).Cross(d);
                u.Normalize();
                const Vector3 w = d.Cross(u);
                auto P = [&](float x, float y, float z) { return a + u * x + w * y + d * z; };
                const Vector3 v[8] = {
                    P(-t, -t, 0.0f), P(t, -t, 0.0f), P(t, -t, len), P(-t, -t, len),
                    P(-t, t, 0.0f), P(t, t, 0.0f), P(t, t, len), P(-t, t, len) };
                batch.Append(v, c, c);
            };

        const int outer = 22, inner = 10;
        for (int k = 0; k < outer + inner; ++k)
        {
            const Vector3 n = randUnit();
            Vector3 e1 = (std::fabs(n.y) < 0.9f) ? Vector3(0, 1, 0).Cross(n) : Vector3(1, 0, 0).Cross(n);
            e1.Normalize();
            const Vector3 e2 = n.Cross(e1);
            const float r = (k < outer) ? U(0.72f, 1.0f) : U(0.3f, 0.62f);
            const float a0 = U(0.0f, 6.2831853f);
            const float span = U(1.6f, 3.6f);
            const int segs = 5 + (int)(span * 2.0f);
            const float t = U(0.026f, 0.042f);
            const float shade = U(0.72f, 1.18f);
            const Vector4 c(color.x * shade, color.y * shade * U(0.92f, 1.04f), color.z * shade * U(0.85f, 1.05f), 1.0f);
            Vector3 prev;
            for (int i = 0; i <= segs; ++i)
            {
                const float th = a0 + span * (float)i / (float)segs;
                Vector3 p = (e1 * std::cos(th) + e2 * std::sin(th)) * (r * U(0.94f, 1.06f)) + randUnit() * 0.03f;
                if (i > 0) twig(prev, p, t, c);
                prev = p;
            }
        }
        return batch.Build(device);
    }

    // 行列 → TransformComponent::rotation（度。Transform は CreateFromYawPitchRoll(yaw, pitch, roll) で戻す）
    Vector3 EulerDegFromQuaternion(const Quaternion& q)
    {
        const Matrix m = Matrix::CreateFromQuaternion(q);
        const float pitch = std::asin(std::clamp(-m._32, -1.0f, 1.0f));
        const float yaw = std::atan2(m._31, m._33);
        const float roll = std::atan2(m._12, m._22);
        return Vector3(DirectX::XMConvertToDegrees(pitch), DirectX::XMConvertToDegrees(yaw), DirectX::XMConvertToDegrees(roll));
    }
}

void Tumbleweeds::Init(ID3D11Device* device)
{
    m_Device = device;
}

void Tumbleweeds::BuildModels(ID3D11Device* device, const Vector3& color)
{
    if (!device) return;
    if (!m_Models.empty() && (color - m_ModelColor).LengthSquared() < 1e-6f) return;
    m_Models.clear();
    for (int i = 0; i < kModelVariants; ++i)
        if (auto m = BuildTumbleweedModel(device, 9137u + (uint32_t)i * 7919u, color)) m_Models.push_back(m);
    m_ModelColor = color;
}

void Tumbleweeds::Clear(Registry& reg)
{
    for (const Weed& w : m_Weeds)
        if (reg.IsValid(w.e)) reg.Destroy(w.e);
    m_Weeds.clear();
}

void Tumbleweeds::Reset(Registry& reg, const GridWorld& grid, TerrainGenerator::Biome biome, uint32_t seed, const Vector3& center)
{
    Clear(reg);
    m_Biome = biome;
    m_Rng.seed(seed * 2246822519u + 13u);
    const auto& s = m_Settings;
    if (!s.enabled) return;
    const int count = (biome == TerrainGenerator::Biome::Desert) ? s.countDesert
        : (biome == TerrainGenerator::Biome::Dungeon) ? s.countRuins : s.countGrass;
    if (count <= 0) return;
    BuildModels(m_Device, biome == TerrainGenerator::Biome::Dungeon ? s.colorRuins : s.colorDesert);
    if (m_Models.empty()) return;

    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (int i = 0; i < count; ++i)
    {
        Weed w;
        w.radius = s.radiusMin + (s.radiusMax - s.radiusMin) * u01(m_Rng);
        w.rot = Quaternion::CreateFromYawPitchRoll(u01(m_Rng) * 6.28f, u01(m_Rng) * 6.28f, u01(m_Rng) * 6.28f);
        if (!Place(grid, w, center, s.spawnMin, s.spawnMax, nullptr)) continue;
        w.e = reg.Create();
        TransformComponent tf;
        tf.position = w.pos;
        tf.rotation = EulerDegFromQuaternion(w.rot);
        tf.scale = Vector3(w.radius);
        reg.Add<TransformComponent>(w.e, tf);
        ModelComponent mc;
        mc.model = m_Models[(size_t)i % m_Models.size()];
        reg.Add<ModelComponent>(w.e, mc);
        m_Weeds.push_back(w);
    }
}

// center の周りの rMin〜rMax の輪の通れるマス（upwind が有れば風上 ±70 度）に、静止して置く
bool Tumbleweeds::Place(const GridWorld& grid, Weed& w, const Vector3& center, float rMin, float rMax, const Vector2* upwind)
{
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    for (int attempt = 0; attempt < 40; ++attempt)
    {
        float a = u01(m_Rng) * 6.2831853f;
        if (upwind) a = std::atan2(upwind->y, upwind->x) + (u01(m_Rng) - 0.5f) * 2.44f;   // ±70 度
        const float r = rMin + (rMax - rMin) * u01(m_Rng);
        const Vector3 p = center + Vector3(std::cos(a) * r, 0.0f, std::sin(a) * r);
        int gx = 0, gz = 0;
        grid.WorldToCell(p, gx, gz);
        if (!grid.IsWalkable(gx, gz)) continue;
        const float g = grid.SampleHeight(p.x, p.z);
        // 崖際・急な坂には置かない
        const float gx1 = grid.SampleHeight(p.x + 1.0f, p.z), gx0 = grid.SampleHeight(p.x - 1.0f, p.z);
        const float gz1 = grid.SampleHeight(p.x, p.z + 1.0f), gz0 = grid.SampleHeight(p.x, p.z - 1.0f);
        if (std::fabs(gx1 - gx0) > 1.2f || std::fabs(gz1 - gz0) > 1.2f) continue;
        w.pos = Vector3(p.x, g + w.radius, p.z);
        w.vel = Vector3::Zero;
        w.spin = 0.0f;
        w.airborne = false;
        return true;
    }
    return false;
}

void Tumbleweeds::Update(Registry& reg, const GridWorld& grid, float dt, const WindState& wind, float windTime,
    const LocalWind* local, const Vector3* player, const Vector3& playerVel)
{
    if (m_Weeds.empty() || dt <= 0.0f) return;
    const auto& s = m_Settings;
    const Vector2 upwind(-wind.dir.x, -wind.dir.y);
    // 1 歩が長いと崖の手前の検査を飛び越えるので刻む
    const int steps = std::clamp((int)std::ceil(dt / (1.0f / 60.0f)), 1, 4);
    const float h = (std::min)(dt, 0.1f) / (float)steps;

    for (Weed& w : m_Weeds)
    {
        if (!reg.IsValid(w.e) || !reg.Has<TransformComponent>(w.e)) continue;
        // 遠く離れた（風に流され切った / プレイヤーが走り去った）物は風上へ置き直す
        if (player)
        {
            const Vector3 d = w.pos - *player;
            if (d.x * d.x + d.z * d.z > s.despawn * s.despawn)
                Place(grid, w, *player, s.respawnMin, s.respawnMax, &upwind);
        }
        for (int k = 0; k < steps; ++k)
            Step(w, grid, h, wind, windTime, local, player, playerVel);

        auto& tf = reg.Get<TransformComponent>(w.e);
        tf.position = w.pos;
        tf.rotation = EulerDegFromQuaternion(w.rot);
        tf.scale = Vector3(w.radius);
    }
}

void Tumbleweeds::Step(Weed& w, const GridWorld& grid, float dt, const WindState& wind, float windTime,
    const LocalWind* local, const Vector3* player, const Vector3& playerVel)
{
    const auto& s = m_Settings;
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const float ground = grid.SampleHeight(w.pos.x, w.pos.z);
    const float bottom = w.pos.y - w.radius;

    // ---- 空気の速さ：世界の風 + 局所の風源（高く上がるほど弱める。吹き上げは軽いので強めに受ける）----
    Vector3 air = wind.Velocity(windTime, w.pos.x, w.pos.z);
    Vector3 suck = Vector3::Zero;
    if (local)
    {
        const float above = bottom - ground;
        const float fade = std::clamp(1.0f - (above - s.localFadeLow) / (std::max)(s.localFadeHigh - s.localFadeLow, 0.1f), 0.0f, 1.0f);
        Vector3 la = local->Air(w.pos.x, w.pos.z) * fade;
        la.y *= s.liftMul;
        air += la;
        suck = local->Inward(w.pos.x, w.pos.z) * (s.suck * fade);
    }

    const bool onGround = bottom <= ground + 0.04f && w.vel.y <= 0.5f;
    Vector3 acc = suck + Vector3(0.0f, -s.gravity, 0.0f);
    acc.y += (air.y - w.vel.y) * s.airDrag;
    const float hd = onGround ? s.groundDrag : s.airDrag;
    acc.x += (air.x - w.vel.x) * hd;
    acc.z += (air.z - w.vel.z) * hd;
    const float sp = std::sqrt(w.vel.x * w.vel.x + w.vel.z * w.vel.z);
    if (onGround)
    {
        // 転がりの摩擦と、坂を下る力
        if (sp > 0.05f)
        {
            acc.x -= w.vel.x / sp * s.rollFriction;
            acc.z -= w.vel.z / sp * s.rollFriction;
        }
        const float sx = grid.SampleHeight(w.pos.x + 0.5f, w.pos.z) - grid.SampleHeight(w.pos.x - 0.5f, w.pos.z);
        const float sz = grid.SampleHeight(w.pos.x, w.pos.z + 0.5f) - grid.SampleHeight(w.pos.x, w.pos.z - 0.5f);
        if (std::fabs(sx) < 0.6f && std::fabs(sz) < 0.6f)   // 崖の縁（段差の補間）は坂として扱わない
        {
            acc.x -= s.gravity * sx * s.slopePull;
            acc.z -= s.gravity * sz * s.slopePull;
        }
        // でこぼこを転がる時の小さな跳ね
        if (sp > 1.0f && u01(m_Rng) < s.hopRate * dt * (std::min)(sp / 3.0f, 1.5f))
            w.vel.y += s.hopSpeed * (0.6f + 0.4f * (std::min)(sp / 4.0f, 1.5f)) * (0.6f + 0.6f * u01(m_Rng));
    }
    w.vel += acc * dt;

    // ---- プレイヤーに当たったら蹴られる ----
    if (player)
    {
        Vector3 d = w.pos - *player;
        const float dy = d.y;
        d.y = 0.0f;
        const float dist = d.Length();
        const float minD = w.radius + 0.4f;
        if (dist < minD && std::fabs(dy) < 0.9f + w.radius)
        {
            const Vector3 n = dist > 1e-3f ? d / dist : Vector3(1.0f, 0.0f, 0.0f);
            Vector3 rel = w.vel - playerVel;
            rel.y = 0.0f;
            const float into = rel.Dot(n);
            if (into < 0.0f)
            {
                w.vel -= n * (into * (1.0f + s.kick));
                const float pspeed = std::sqrt(playerVel.x * playerVel.x + playerVel.z * playerVel.z);
                w.vel.y = (std::max)(w.vel.y, s.kickUp * std::clamp(pspeed / 5.0f + 0.3f, 0.3f, 1.4f));
            }
            w.pos.x = player->x + n.x * minD;
            w.pos.z = player->z + n.z * minD;
        }
    }

    // ---- 横：進む先（玉の前の縁）が崖・通れないマスなら跳ね返る ----
    Vector3 move = w.vel * dt;
    const float msp = std::sqrt(move.x * move.x + move.z * move.z);
    if (msp > 1e-5f)
    {
        const float ux = move.x / msp, uz = move.z / msp;
        const float px = w.pos.x + ux * (w.radius * 0.9f + msp), pz = w.pos.z + uz * (w.radius * 0.9f + msp);
        int gx = 0, gz = 0;
        grid.WorldToCell(Vector3(px, 0.0f, pz), gx, gz);
        const float gAhead = grid.SampleHeight(px, pz);
        const bool blocked = !grid.IsWalkable(gx, gz)
            ? (bottom < gAhead + 0.9f)                          // 木・岩・箱・水たまり・場の外
            : (gAhead > ground + 0.45f && bottom < gAhead - 0.05f);   // 登れない段差（崖）
        if (blocked)
        {
            w.vel.x = -w.vel.x * 0.35f;
            w.vel.z = -w.vel.z * 0.35f;
            move.x = move.z = 0.0f;
        }
    }
    w.pos += move;

    // ---- 縦：地面に着いたら跳ね返る / 止まる ----
    const float g2 = grid.SampleHeight(w.pos.x, w.pos.z);
    if (w.pos.y - w.radius < g2)
    {
        w.pos.y = g2 + w.radius;
        if (w.vel.y < -1.2f) w.vel.y = -w.vel.y * s.bounce;
        else if (w.vel.y < 0.0f) w.vel.y = 0.0f;
    }
    w.airborne = (w.pos.y - w.radius) > g2 + 0.05f;

    // ---- 回転：接地中は転がる向き・速さ、空中は惰性でゆっくり弱まる ----
    const float hs = std::sqrt(w.vel.x * w.vel.x + w.vel.z * w.vel.z);
    if (!w.airborne && hs > 0.02f)
    {
        w.spinAxis = Vector3(w.vel.z, 0.0f, -w.vel.x) / hs;   // 上 × 進む向き
        w.spin = hs / (std::max)(w.radius, 0.05f);
    }
    else
        w.spin *= 1.0f - (std::min)(0.4f * dt, 1.0f);
    if (w.spin > 1e-4f)
    {
        w.rot = w.rot * Quaternion::CreateFromAxisAngle(w.spinAxis, w.spin * dt);
        w.rot.Normalize();
    }
}

bool Tumbleweeds::GetDebug(int i, Vector3& pos, Vector3& vel, bool& airborne) const
{
    if (i < 0 || i >= (int)m_Weeds.size()) return false;
    pos = m_Weeds[i].pos;
    vel = m_Weeds[i].vel;
    airborne = m_Weeds[i].airborne;
    return true;
}

void Tumbleweeds::DrawImGui(Registry& reg, const GridWorld& grid, const Vector3& center)
{
    if (!ImGui::CollapsingHeader("Tumbleweeds (wind)")) return;
    auto& s = m_Settings;
    ImGui::Text("on field %d", (int)m_Weeds.size());
    bool reset = ImGui::Checkbox("Enabled##tw", &s.enabled);
    reset |= ImGui::SliderInt("Count Desert", &s.countDesert, 0, 40);
    reset |= ImGui::SliderInt("Count Ruins", &s.countRuins, 0, 40);
    reset |= ImGui::SliderInt("Count Grassland", &s.countGrass, 0, 40);
    ImGui::DragFloatRange2("Radius##tw", &s.radiusMin, &s.radiusMax, 0.01f, 0.1f, 1.5f);
    ImGui::DragFloatRange2("Respawn Ring (upwind)", &s.respawnMin, &s.respawnMax, 0.5f, 5.0f, 80.0f);
    ImGui::SliderFloat("Despawn Distance", &s.despawn, 20.0f, 100.0f);
    ImGui::SliderFloat("Gravity##tw", &s.gravity, 1.0f, 30.0f);
    ImGui::SliderFloat("Air Drag (1/s)##tw", &s.airDrag, 0.1f, 5.0f);
    ImGui::SliderFloat("Ground Drag (1/s)##tw", &s.groundDrag, 0.1f, 5.0f);
    ImGui::SliderFloat("Roll Friction##tw", &s.rollFriction, 0.0f, 3.0f);
    ImGui::SliderFloat("Slope Pull##tw", &s.slopePull, 0.0f, 2.0f);
    ImGui::SliderFloat("Bounce##tw", &s.bounce, 0.0f, 0.9f);
    ImGui::SliderFloat("Hops / s##tw", &s.hopRate, 0.0f, 4.0f);
    ImGui::SliderFloat("Hop Speed##tw", &s.hopSpeed, 0.0f, 6.0f);
    ImGui::SliderFloat("Updraft x (local wind)", &s.liftMul, 0.0f, 6.0f);
    ImGui::SliderFloat("Suck to Core x", &s.suck, 0.0f, 10.0f);
    ImGui::DragFloatRange2("Local Wind Fade Height", &s.localFadeLow, &s.localFadeHigh, 0.1f, 0.0f, 20.0f);
    ImGui::SliderFloat("Player Kick##tw", &s.kick, 0.0f, 4.0f);
    ImGui::SliderFloat("Player Kick Up##tw", &s.kickUp, 0.0f, 8.0f);
    bool recolor = ImGui::ColorEdit3("Color Desert##tw", &s.colorDesert.x);
    recolor |= ImGui::ColorEdit3("Color Ruins##tw", &s.colorRuins.x);
    if (ImGui::Button("Re-place around player##tw") || reset || recolor)
        Reset(reg, grid, m_Biome, (uint32_t)m_Rng(), center);
}
