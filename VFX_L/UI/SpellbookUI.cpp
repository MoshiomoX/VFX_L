// ============================================================
// SpellbookUI.cpp
// ============================================================
#include "UI/SpellbookUI.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/SpellbookComponent.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "imgui.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

using namespace DirectX::SimpleMath;

namespace
{
    constexpr float kFixedStep = 1.0f / 120.0f;
    constexpr float kMaxAccum = 0.1f;     // コマ落ち時の積算上限（死のスパイラル防止）
    constexpr float kLinearDamp = 0.999f;   // 1ステップあたり
    constexpr float kAngularDamp = 0.98f;
    constexpr float kFloorFriction = 0.96f;
    constexpr float kPickupLerp = 12.0f;    // visAngle / visScale の収束速度
}

void SpellbookUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;
}

void SpellbookUI::LoadIcons()
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

std::shared_ptr<Texture> SpellbookUI::GetIcon(ItemID id) const
{
    for (const auto& p : m_Icons)
        if (p.first == id) return p.second;
    return nullptr;
}

// ============================================================
// レイアウト
// 画面右側に正方形の箱。グリッド（左）と対になる配置
// ============================================================
void SpellbookUI::Layout(float screenW, float screenH)
{
    const float shortSide = (screenW < screenH) ? screenW : screenH;
    const float inner = shortSide * boxScreenRatio;
    const float margin = shortSide * marginRatio;
    m_Wall = inner * wallRatio;

    // 右端から壁の分を空けて内寸を置く
    m_BoxMax.x = screenW - margin - m_Wall;
    m_BoxMin.x = m_BoxMax.x - inner;
    m_BoxMin.y = (screenH - inner) * 0.5f;
    m_BoxMax.y = m_BoxMin.y + inner;

    // 画面サイズが変わったら中身を範囲内へ寄せる
    for (auto& b : m_Bodies)
    {
        b.pos.x = std::clamp(b.pos.x, m_BoxMin.x + b.radius, m_BoxMax.x - b.radius);
        b.pos.y = std::clamp(b.pos.y, m_BoxMin.y + b.radius, m_BoxMax.y - b.radius);
    }
}

// ============================================================
// 物理半径
// occupyCells の AABB の外接円 × 0.8。
// 少し小さめにして視覚的に軽く重なるようにする（雑物感が出る）
// ============================================================
float SpellbookUI::ComputeRadius(ItemID id) const
{
    const ItemCommon* c = ItemDatabase::GetCommon(id);
    if (!c || c->occupyCells.empty()) return m_CellSize * boxScale * 0.5f;

    int minR = 999, maxR = -999, minC = 999, maxC = -999;
    for (const auto& o : c->occupyCells)
    {
        minR = (std::min)(minR, o.row); maxR = (std::max)(maxR, o.row);
        minC = (std::min)(minC, o.col); maxC = (std::max)(maxC, o.col);
    }
    const float w = (float)(maxC - minC + 1);
    const float h = (float)(maxR - minR + 1);
    const float cell = m_CellSize * boxScale;

    return 0.5f * (std::max)(w, h) * cell * 0.8f;
}

// ============================================================
// 箱の上から降らせる
// 高さをばらけさせ、既存と重なる位置を避けて置く。
// ※生成時の大きな重なりは分離処理が一撃で弾き飛ばすので、
//   入り口で重ならせないのが一番安い
// ============================================================
void SpellbookUI::SpawnBody(ItemID id)
{
    BodyState b;
    b.id = id;
    b.radius = ComputeRadius(id);

    const float x0 = m_BoxMin.x + b.radius;
    const float x1 = m_BoxMax.x - b.radius;

    // 何回か試して、既存とぶつからない位置を探す。
    // 見つからなくても最後の候補で出す（分離が少しずつ押し分ける）
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const float t = (float)rand() / RAND_MAX;
        b.pos.x = x0 + (x1 - x0) * t;
        b.pos.y = m_BoxMin.y + b.radius
            + ((float)rand() / RAND_MAX) * b.radius * 2.0f;

        bool overlap = false;
        for (const auto& o : m_Bodies)
        {
            const float minDist = (b.radius + o.radius) * 0.9f;
            if ((b.pos - o.pos).LengthSquared() < minDist * minDist)
            {
                overlap = true;
                break;
            }
        }
        if (!overlap) break;
    }

    b.vel = { ((float)rand() / RAND_MAX - 0.5f) * 120.0f, 0.0f };
    b.angVel = ((float)rand() / RAND_MAX - 0.5f) * 6.0f;

    m_Bodies.push_back(b);
}
// ============================================================
// 差分同期。ここが箱の中身の唯一の決定者。
// あるべき数 = 所持数 - グリッド配置数 - （自分から掴んでドラッグ中なら1）
// ============================================================
void SpellbookUI::SyncBodies(const SpellbookComponent& book, const BackpackComponent& bp)
{
    for (ItemID id : ItemDatabase::GetAllIDs())
    {
        const ItemCommon* c = ItemDatabase::GetCommon(id);
        if (!c) continue;

        int want = book.GetCount(id)
            - (ItemDatabase::IsFrame(id)
                ? BackpackLogic::CountPlacedFrames(bp, id)
                : BackpackLogic::CountPlaced(bp, id));

        // 箱から掴んで宙にある分。これを引かないと掴んだ瞬間に補充される
        if (m_Drag && m_Drag->IsActive()
            && m_Drag->source == DragSource::Spellbook && m_Drag->id == id)
            want -= 1;

        if (want < 0) want = 0;

        int have = 0;
        for (const auto& b : m_Bodies)
            if (b.id == id) ++have;

        if (have < want)
            SpawnBody(id);

        // 多い分を後ろから消す（後ろ = 新しい方から消えるが、見た目上は気にならない）
        while (have > want)
        {
            for (int i = (int)m_Bodies.size() - 1; i >= 0; --i)
            {
                if (m_Bodies[i].id == id)
                {
                    m_Bodies.erase(m_Bodies.begin() + i);
                    break;
                }
            }
            --have;
        }
    }
}

// ============================================================
// 物理 1 ステップ
// 精密さより「収納箱の雑物」の見た目を優先している。
// 回転は衝突の接線成分と床の転がりから作る（トルクの符号は見た目基準）
// ============================================================
void SpellbookUI::StepPhysics(float dt)
{
    // ---- 重力 + 積分 ----
    for (auto& b : m_Bodies)
    {
        b.vel.y += gravity * dt;
        b.pos += b.vel * dt;
        b.angle += b.angVel * dt;
    }

    // ---- 円同士の分離 ----
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        for (size_t j = i + 1; j < m_Bodies.size(); ++j)
        {
            auto& a = m_Bodies[i];
            auto& b = m_Bodies[j];

            Vector2 d = b.pos - a.pos;
            float dist = d.Length();
            float minDist = a.radius + b.radius;
            if (dist >= minDist) continue;

            Vector2 n = (dist > 1e-4f) ? d / dist : Vector2(0.0f, -1.0f);

            const float kMaxPush = 3.0f;   // px / step
            const float push = (std::min)((minDist - dist) * 0.5f, kMaxPush);
            a.pos -= n * push;
            b.pos += n * push;

            // 法線方向の相対速度を殺す（反発は小さめ）
            Vector2 relV = b.vel - a.vel;
            float vn = relV.Dot(n);
            if (vn < 0.0f)
            {
                const float imp = -vn * (1.0f + restitution) * 0.5f;
                a.vel -= n * imp;
                b.vel += n * imp;
            }

            // 接線方向の相対速度 → 回転（縁を掠めると回る）
            Vector2 t(-n.y, n.x);
            const float vt = relV.Dot(t);
            a.angVel += vt / a.radius * spinTransfer;
            b.angVel += vt / b.radius * spinTransfer;
        }
    }

    // ---- 壁と床 ----
    for (auto& b : m_Bodies)
    {
        // 左右
        if (b.pos.x < m_BoxMin.x + b.radius)
        {
            b.pos.x = m_BoxMin.x + b.radius;
            if (b.vel.x < 0.0f) b.vel.x *= -restitution;
        }
        if (b.pos.x > m_BoxMax.x - b.radius)
        {
            b.pos.x = m_BoxMax.x - b.radius;
            if (b.vel.x > 0.0f) b.vel.x *= -restitution;
        }

        // 天井（降らせた直後にはみ出さないように）
        if (b.pos.y < m_BoxMin.y + b.radius)
        {
            b.pos.y = m_BoxMin.y + b.radius;
            if (b.vel.y < 0.0f) b.vel.y *= -restitution;
        }

        // 床。接地中は摩擦 + 転がり
        if (b.pos.y > m_BoxMax.y - b.radius)
        {
            b.pos.y = m_BoxMax.y - b.radius;
            if (b.vel.y > 0.0f) b.vel.y *= -restitution;

            b.vel.x *= kFloorFriction;

            // 転がり: 横滑りを角速度へ寄せる（円木のように転がる）
            const float rollTarget = b.vel.x / b.radius;
            b.angVel += (rollTarget - b.angVel) * 0.3f;
        }

        // ---- 減衰 ----
        b.vel *= kLinearDamp;
        b.angVel *= kAngularDamp;
    }
}

// ============================================================
// 掴み判定
// 後ろ（後に描かれた = 上に見える）から当てる。
// 掴んだ body は即消す。以後は DragContext の管轄。
// 置き損ねても何もしない: 次の同期で数が合わなくなり、勝手に降って戻る
// ============================================================
void SpellbookUI::TryGrab()
{
    if (!m_Drag || m_Drag->IsActive()) return;
    if (ImGui::GetIO().WantCaptureMouse) return;

    auto& input = InputManager::Get();
    if (!input.GetMouseTrigger(0)) return;

    auto mp = input.GetMousePos();
    Vector2 mouse(mp.x, mp.y);

    for (int i = (int)m_Bodies.size() - 1; i >= 0; --i)
    {
        const auto& b = m_Bodies[i];
        if ((mouse - b.pos).LengthSquared() > b.radius * b.radius) continue;

        // ---- DragContext へ引き渡す ----
        m_Drag->source = DragSource::Spellbook;
        m_Drag->id = b.id;
        m_Drag->rotation = 0;             // グリッド回転は正から始める
        m_Drag->originalIndex = -1;

        // ブロックの中心がマウスに来るように、アンカーマスの中心を掴んだ扱いにする
        m_Drag->grabOffset = { m_CellSize * 0.5f, m_CellSize * 0.5f };

        // 見た目: 箱の中の角度と縮小率から、正立・等倍へ収束していく
        float a = std::fmod(b.angle, 6.2831853f);
        if (a > 3.1415926f)  a -= 6.2831853f;
        if (a < -3.1415926f) a += 6.2831853f;
        m_Drag->visAngle = a;
        m_Drag->visScale = boxScale;

        m_Bodies.erase(m_Bodies.begin() + i);
        return;
    }
}

// ============================================================
// Update: 同期 → 拾い上げ演出の収束 → 物理（固定ステップ）→ 掴み
// ============================================================
void SpellbookUI::Update(const SpellbookComponent& book, const BackpackComponent& bp, float dt)
{
    SyncBodies(book, bp);

    // ---- 拾い上げの見た目を 0 / 1 へ寄せる ----
    // 出どころに関係なく寄せて良い（Spellbook 以外は最初から 0 / 1）
    if (m_Drag && m_Drag->IsActive())
    {
        const float k = (std::min)(1.0f, dt * kPickupLerp);
        m_Drag->visAngle += (0.0f - m_Drag->visAngle) * k;
        m_Drag->visScale += (1.0f - m_Drag->visScale) * k;
    }

    // ---- 物理（固定ステップ）----
    m_PhysAccum += dt;
    if (m_PhysAccum > kMaxAccum) m_PhysAccum = kMaxAccum;

    while (m_PhysAccum >= kFixedStep)
    {
        StepPhysics(kFixedStep);
        m_PhysAccum -= kFixedStep;
    }

    TryGrab();
}

// ============================================================
// 描画
// 箱（壁 + 内側）→ 中身。
// 中身は円の中心に occupyCells の AABB を重ね、angle で回す。
// 全マスが同じ pivot（円の中心）を共有するので、剛体として回って見える
// ============================================================
void SpellbookUI::Draw(SpriteRenderer& sprite)
{
    if (!m_BlockTex) return;

    // ---- 箱 ----
    Vector2 outerPos = { m_BoxMin.x - m_Wall, m_BoxMin.y - m_Wall };
    Vector2 outerSize = { (m_BoxMax.x - m_BoxMin.x) + m_Wall * 2.0f,
                          (m_BoxMax.y - m_BoxMin.y) + m_Wall * 2.0f };
    sprite.Draw(m_BlockTex, outerPos, outerSize, frameColor);
    sprite.Draw(m_BlockTex, m_BoxMin,
        { m_BoxMax.x - m_BoxMin.x, m_BoxMax.y - m_BoxMin.y }, innerColor);

    // ---- 中身 ----
    const float cell = m_CellSize * boxScale;

    for (const auto& b : m_Bodies)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(b.id);
        if (!c || c->occupyCells.empty()) continue;

        // 形状 AABB を求め、中心を body.pos に合わせる
        int minR = 999, maxR = -999, minC = 999, maxC = -999;
        for (const auto& o : c->occupyCells)
        {
            minR = (std::min)(minR, o.row); maxR = (std::max)(maxR, o.row);
            minC = (std::min)(minC, o.col); maxC = (std::max)(maxC, o.col);
        }
        const float w = (float)(maxC - minC + 1) * cell;
        const float h = (float)(maxR - minR + 1) * cell;

        auto icon = GetIcon(b.id);
        auto tex = icon ? icon : m_BlockTex;
        Vector4 col = icon ? Vector4(1, 1, 1, 1) : c->color;

        for (const auto& o : c->occupyCells)
        {
            Vector2 pos = {
                b.pos.x - w * 0.5f + (float)(o.col - minC) * cell,
                b.pos.y - h * 0.5f + (float)(o.row - minR) * cell
            };
            sprite.Draw(tex, pos, { cell, cell }, col, b.angle, b.pos);
        }
    }
}