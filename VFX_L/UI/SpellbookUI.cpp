// ============================================================
// SpellbookUI.cpp
// ============================================================
#include "UI/SpellbookUI.h"
#include "UI/ShapeSprite.h"
#include "UI/UIDeco.h"
#include "Graphics/Renderer/SpriteRenderer.h"
#include "Graphics/Material/Texture.h"
#include "Component/SpellbookComponent.h"
#include "Component/BackpackComponent.h"
#include "Item/BackpackLogic.h"
#include "Item/ItemDatabase.h"
#include "Manager/ResourceManager.h"
#include "Manager/InputManager.h"
#include "ResourcePaths.h"
#include "Audio/AudioSystem.h"
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
    constexpr float kAngularDamp = 0.995f;
    constexpr float kPickupLerp = 12.0f;    // visAngle / visScale の収束速度

    constexpr float kAllowedPenetration = 0.5f;   // px。これ以下のめり込みは押し戻さない（震え防止）
    constexpr float kBiasFactor = 0.2f;           // めり込みを 1 ステップで戻す割合
    constexpr float kMaxBias = 400.0f;            // px/s。出現時の大きな重なりで弾け飛ばないように
    constexpr float kBounceThreshold = 90.0f;     // px/s。これより遅い衝突は跳ねない（静止の接触を揺らさない）

    constexpr float kSleepLinear = 6.0f;          // px/s
    constexpr float kSleepAngular = 0.08f;        // rad/s
    constexpr float kSleepTime = 0.5f;            // 秒
    // 静かな収め：全体がこれより遅い状態が kQuietTime 続いたら、毎ステップ kSettleDamp を掛けて止める
    constexpr float kQuietLinear = 60.0f;         // px/s
    constexpr float kQuietAngular = 1.5f;         // rad/s
    constexpr float kQuietTime = 1.0f;            // 秒
    constexpr float kSettleDamp = 0.92f;          // 1 ステップあたり（120 Hz で約 0.1 秒で 1/e）
    constexpr float kQuietSmooth = 0.25f;         // 秒。速さを均す時定数

    constexpr float kWallThick = 400.0f;          // 壁の四角の厚さ（px。薄いと速い物が抜ける）
    constexpr uint32_t kWallUid[4] = { 1, 2, 3, 4 };   // 左 右 上 床

    // 木箱の枠の絵の縁（512 のうち 128）
    constexpr float kFrameBorderUV = 128.0f / 512.0f;
    constexpr float kLockAspect = 160.0f / 128.0f;

    // ---- 2x2 行列（列ベクトル）。Box2D-lite の書き方に合わせる ----
    struct Mat22 { Vector2 col1, col2; };
    Mat22 Rot(float c, float s) { return { { c, s }, { -s, c } }; }
    Mat22 Transpose(const Mat22& m) { return { { m.col1.x, m.col2.x }, { m.col1.y, m.col2.y } }; }
    Vector2 Mul(const Mat22& m, const Vector2& v) { return m.col1 * v.x + m.col2 * v.y; }
    Mat22 Mul(const Mat22& a, const Mat22& b) { return { Mul(a, b.col1), Mul(a, b.col2) }; }
    Vector2 Abs(const Vector2& v) { return { std::fabs(v.x), std::fabs(v.y) }; }
    Mat22 Abs(const Mat22& m) { return { Abs(m.col1), Abs(m.col2) }; }
    float Cross(const Vector2& a, const Vector2& b) { return a.x * b.y - a.y * b.x; }
    Vector2 Cross(float w, const Vector2& r) { return { -w * r.y, w * r.x }; }   // 角速度 × 腕
    float Sign(float x) { return x < 0.0f ? -1.0f : 1.0f; }

    // ---- 接触の特徴（どの辺とどの辺か）。前ステップの接触と対応付けるのに使う ----
    enum Edge : uint8_t { NO_EDGE = 0, EDGE1, EDGE2, EDGE3, EDGE4 };
    struct FeaturePair { uint8_t inEdge1 = 0, outEdge1 = 0, inEdge2 = 0, outEdge2 = 0; };
    uint32_t Pack(const FeaturePair& f)
    {
        return (uint32_t)f.inEdge1 | ((uint32_t)f.outEdge1 << 8) | ((uint32_t)f.inEdge2 << 16) | ((uint32_t)f.outEdge2 << 24);
    }
    void Flip(FeaturePair& f) { std::swap(f.inEdge1, f.inEdge2); std::swap(f.outEdge1, f.outEdge2); }

    struct ClipVertex { Vector2 v; FeaturePair fp; };

    int ClipSegmentToLine(ClipVertex vOut[2], const ClipVertex vIn[2], const Vector2& normal, float offset, uint8_t clipEdge)
    {
        int numOut = 0;
        const float d0 = normal.Dot(vIn[0].v) - offset;
        const float d1 = normal.Dot(vIn[1].v) - offset;
        if (d0 <= 0.0f) vOut[numOut++] = vIn[0];
        if (d1 <= 0.0f) vOut[numOut++] = vIn[1];
        if (d0 * d1 < 0.0f)
        {
            const float t = d0 / (d0 - d1);
            vOut[numOut].v = vIn[0].v + (vIn[1].v - vIn[0].v) * t;
            if (d0 > 0.0f)
            {
                vOut[numOut].fp = vIn[0].fp;
                vOut[numOut].fp.inEdge1 = clipEdge;
                vOut[numOut].fp.inEdge2 = NO_EDGE;
            }
            else
            {
                vOut[numOut].fp = vIn[1].fp;
                vOut[numOut].fp.outEdge1 = clipEdge;
                vOut[numOut].fp.outEdge2 = NO_EDGE;
            }
            ++numOut;
        }
        return numOut;
    }

    // 基準の四角の法線に一番逆を向いている、相手の辺
    void ComputeIncidentEdge(ClipVertex c[2], const Vector2& h, const Vector2& pos, const Mat22& rot, const Vector2& normal)
    {
        const Vector2 n = -Mul(Transpose(rot), normal);
        const Vector2 nAbs = Abs(n);
        if (nAbs.x > nAbs.y)
        {
            if (Sign(n.x) > 0.0f)
            {
                c[0].v = { h.x, -h.y }; c[0].fp.inEdge2 = EDGE3; c[0].fp.outEdge2 = EDGE4;
                c[1].v = { h.x,  h.y }; c[1].fp.inEdge2 = EDGE4; c[1].fp.outEdge2 = EDGE1;
            }
            else
            {
                c[0].v = { -h.x,  h.y }; c[0].fp.inEdge2 = EDGE1; c[0].fp.outEdge2 = EDGE2;
                c[1].v = { -h.x, -h.y }; c[1].fp.inEdge2 = EDGE2; c[1].fp.outEdge2 = EDGE3;
            }
        }
        else
        {
            if (Sign(n.y) > 0.0f)
            {
                c[0].v = {  h.x, h.y }; c[0].fp.inEdge2 = EDGE4; c[0].fp.outEdge2 = EDGE1;
                c[1].v = { -h.x, h.y }; c[1].fp.inEdge2 = EDGE1; c[1].fp.outEdge2 = EDGE2;
            }
            else
            {
                c[0].v = { -h.x, -h.y }; c[0].fp.inEdge2 = EDGE2; c[0].fp.outEdge2 = EDGE3;
                c[1].v = {  h.x, -h.y }; c[1].fp.inEdge2 = EDGE3; c[1].fp.outEdge2 = EDGE4;
            }
        }
        c[0].v = pos + Mul(rot, c[0].v);
        c[1].v = pos + Mul(rot, c[1].v);
    }

    // 9 分割で描く（四隅は等倍、辺は伸ばす）。border は画面上の縁の太さ
    void DrawNineSlice(SpriteRenderer& sprite, const std::shared_ptr<Texture>& tex,
        const Vector2& pos, const Vector2& size, float border, float borderUV, const Vector4& color)
    {
        const float xs[3] = { pos.x, pos.x + border, pos.x + size.x - border };
        const float ws[3] = { border, size.x - border * 2.0f, border };
        const float ys[3] = { pos.y, pos.y + border, pos.y + size.y - border };
        const float hs[3] = { border, size.y - border * 2.0f, border };
        const float us[3] = { 0.0f, borderUV, 1.0f - borderUV };
        const float uw[3] = { borderUV, 1.0f - borderUV * 2.0f, borderUV };
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i)
            {
                if (i == 1 && j == 1) continue;   // 真ん中は透明（中身が見える）
                sprite.Draw(tex, { xs[i], ys[j] }, { ws[i], hs[j] }, color, { us[i], us[j], uw[i], uw[j] });
            }
    }
}

void SpellbookUI::Initialize(std::shared_ptr<Texture> blockTex)
{
    m_BlockTex = blockTex;

    // 木箱の絵は色付きなので sRGB で読む（サンプラーが線形へ戻す）。
    // 共有キャッシュの絵は UNORM 読みなので、ここだけ専用に作る
    auto* device = ResourceManager::Get().GetDevice();
    auto load = [device](const wchar_t* path) -> std::shared_ptr<Texture>
    {
        if (!device) return nullptr;
        auto tex = std::make_shared<Texture>();
        return tex->Load(device, path, true) ? tex : nullptr;
    };
    m_FrameTex = load(Res::Deco::ChestFrame);
    m_BackTex = load(Res::Deco::ChestBack);
    m_LockTex = load(Res::Deco::ChestLock);
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

    const Vector2 oldMin = m_BoxMin;
    const bool hadLayout = m_BoxMax.x > m_BoxMin.x;

    // 右端から枠の分を空けて内寸を置く
    m_BoxMax.x = screenW - margin - m_Wall;
    m_BoxMin.x = m_BoxMax.x - inner;
    m_BoxMin.y = (screenH - inner) * 0.5f;
    m_BoxMax.y = m_BoxMin.y + inner;

    // 箱が動いたら中身も一緒に動かし、はみ出た分は範囲内へ寄せる
    const float cellPx = CellPx();
    for (auto& b : m_Bodies)
    {
        if (hadLayout) b.pos += m_BoxMin - oldMin;
        const float r = (std::min)(b.bound * cellPx, inner * 0.5f);
        b.pos.x = std::clamp(b.pos.x, m_BoxMin.x + r, m_BoxMax.x - r);
        b.pos.y = std::clamp(b.pos.y, m_BoxMin.y + r, m_BoxMax.y - r);
    }
    Wake();
}

// ============================================================
// 箱の上から降らせる
// 形（occupyCells）から重心・慣性・外接円・露出した辺を作り、
// 既存と重ならない高さを探して置く。
// ※出現時の大きな重なりは押し戻しが一気に弾くので、入り口で重ならせないのが一番安い
// ============================================================
void SpellbookUI::SpawnBody(ItemID id)
{
    BodyState b;
    b.uid = m_NextUid++;
    b.id = id;

    std::vector<CellOffset> occ;
    if (const ItemCommon* c = ItemDatabase::GetCommon(id))
        occ = c->occupyCells;
    if (occ.empty()) occ.push_back({ 0, 0 });

    // 重心（マス単位。x = 列、y = 行）
    Vector2 com = { 0, 0 };
    for (const auto& o : occ) com += Vector2((float)o.col, (float)o.row);
    com /= (float)occ.size();

    const CellOffset iconAt = ShapeSprite::CenterCell(occ);
    auto has = [&occ](int r, int c)
    {
        for (const auto& o : occ) if (o.row == r && o.col == c) return true;
        return false;
    };

    // 1 マス = 質量 1 の正方形。慣性 = Σ(1/6 + 重心からの距離^2)（マス寸^2 倍は使う時に）
    b.inertia = 0.0f;
    b.bound = 0.0f;
    for (size_t i = 0; i < occ.size(); ++i)
    {
        const auto& o = occ[i];
        const Vector2 d = Vector2((float)o.col, (float)o.row) - com;
        b.cells.push_back(d);
        b.inertia += 1.0f / 6.0f + d.LengthSquared();
        b.bound = (std::max)(b.bound, d.Length() + 0.7072f);

        uint8_t open = 0;
        if (!has(o.row, o.col - 1)) open |= 1;
        if (!has(o.row, o.col + 1)) open |= 2;
        if (!has(o.row - 1, o.col)) open |= 4;
        if (!has(o.row + 1, o.col)) open |= 8;
        b.openSides.push_back(open);

        if (o.row == iconAt.row && o.col == iconAt.col) b.iconCell = (int)i;
    }

    const float cellPx = CellPx();
    const float r = b.bound * cellPx;
    const float x0 = m_BoxMin.x + r;
    const float x1 = (std::max)(x0, m_BoxMax.x - r);

    // 何回か試して、既存とぶつからない位置を探す（外接円で粗く）。
    // 見つからなくても最後の候補で出す（押し戻しが少しずつ押し分ける）
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        b.pos.x = x0 + (x1 - x0) * ((float)rand() / RAND_MAX);
        b.pos.y = m_BoxMin.y + r + ((float)rand() / RAND_MAX) * r;

        bool overlap = false;
        for (const auto& o : m_Bodies)
        {
            const float minDist = (b.bound + o.bound) * cellPx * 0.8f;
            if ((b.pos - o.pos).LengthSquared() < minDist * minDist)
            {
                overlap = true;
                break;
            }
        }
        if (!overlap) break;
    }

    b.angle = ((float)rand() / RAND_MAX - 0.5f) * 0.7f;
    b.vel = { ((float)rand() / RAND_MAX - 0.5f) * 120.0f, 0.0f };
    b.angVel = ((float)rand() / RAND_MAX - 0.5f) * 4.0f;

    m_Bodies.push_back(std::move(b));
    Wake();
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
                    Wake();
                    break;
                }
            }
            --have;
        }
    }
}

// ============================================================
// マス 1 個の四角（画面座標）
// ============================================================
SpellbookUI::Box SpellbookUI::CellBox(const BodyState& b, int cell) const
{
    const float cellPx = CellPx();
    const float c = std::cos(b.angle), s = std::sin(b.angle);
    const Vector2 d = b.cells[cell] * cellPx;
    Box box;
    box.pos = b.pos + Vector2(c * d.x - s * d.y, s * d.x + c * d.y);
    box.h = { cellPx * 0.5f, cellPx * 0.5f };
    box.c = c;
    box.s = s;
    return box;
}

// ============================================================
// 四角 vs 四角（Box2D-lite の Collide をそのまま移植）
// 分離軸で一番浅い面を基準にし、相手の辺をその面の両端で切り取って
// 最大 2 点の接触を作る。法線は A → B
// ============================================================
int SpellbookUI::Collide(Contact out[2], const Box& A, const Box& B)
{
    const Vector2 hA = A.h, hB = B.h;
    const Vector2 posA = A.pos, posB = B.pos;
    const Mat22 rotA = Rot(A.c, A.s), rotB = Rot(B.c, B.s);
    const Mat22 rotAT = Transpose(rotA), rotBT = Transpose(rotB);

    const Vector2 dp = posB - posA;
    const Vector2 dA = Mul(rotAT, dp);
    const Vector2 dB = Mul(rotBT, dp);

    const Mat22 C = Mul(rotAT, rotB);
    const Mat22 absC = Abs(C);
    const Mat22 absCT = Transpose(absC);

    // A の面
    const Vector2 faceA = Abs(dA) - hA - Mul(absC, hB);
    if (faceA.x > 0.0f || faceA.y > 0.0f) return 0;
    // B の面
    const Vector2 faceB = Abs(dB) - Mul(absCT, hA) - hB;
    if (faceB.x > 0.0f || faceB.y > 0.0f) return 0;

    // 一番浅い軸（A の面をわずかに優先して、軸が毎ステップ入れ替わらないようにする）
    enum Axis { FACE_A_X, FACE_A_Y, FACE_B_X, FACE_B_Y };
    constexpr float relativeTol = 0.95f;
    constexpr float absoluteTol = 0.01f;

    Axis axis = FACE_A_X;
    float separation = faceA.x;
    Vector2 normal = dA.x > 0.0f ? rotA.col1 : -rotA.col1;

    if (faceA.y > relativeTol * separation + absoluteTol * hA.y)
    {
        axis = FACE_A_Y; separation = faceA.y;
        normal = dA.y > 0.0f ? rotA.col2 : -rotA.col2;
    }
    if (faceB.x > relativeTol * separation + absoluteTol * hB.x)
    {
        axis = FACE_B_X; separation = faceB.x;
        normal = dB.x > 0.0f ? rotB.col1 : -rotB.col1;
    }
    if (faceB.y > relativeTol * separation + absoluteTol * hB.y)
    {
        axis = FACE_B_Y; separation = faceB.y;
        normal = dB.y > 0.0f ? rotB.col2 : -rotB.col2;
    }

    // 切り取り面の準備
    Vector2 frontNormal, sideNormal;
    ClipVertex incident[2];
    float front = 0.0f, negSide = 0.0f, posSide = 0.0f;
    uint8_t negEdge = NO_EDGE, posEdge = NO_EDGE;

    switch (axis)
    {
    case FACE_A_X:
    {
        frontNormal = normal;
        front = posA.Dot(frontNormal) + hA.x;
        sideNormal = rotA.col2;
        const float side = posA.Dot(sideNormal);
        negSide = -side + hA.y; posSide = side + hA.y;
        negEdge = EDGE3; posEdge = EDGE1;
        ComputeIncidentEdge(incident, hB, posB, rotB, frontNormal);
        break;
    }
    case FACE_A_Y:
    {
        frontNormal = normal;
        front = posA.Dot(frontNormal) + hA.y;
        sideNormal = rotA.col1;
        const float side = posA.Dot(sideNormal);
        negSide = -side + hA.x; posSide = side + hA.x;
        negEdge = EDGE2; posEdge = EDGE4;
        ComputeIncidentEdge(incident, hB, posB, rotB, frontNormal);
        break;
    }
    case FACE_B_X:
    {
        frontNormal = -normal;
        front = posB.Dot(frontNormal) + hB.x;
        sideNormal = rotB.col2;
        const float side = posB.Dot(sideNormal);
        negSide = -side + hB.y; posSide = side + hB.y;
        negEdge = EDGE3; posEdge = EDGE1;
        ComputeIncidentEdge(incident, hA, posA, rotA, frontNormal);
        break;
    }
    case FACE_B_Y:
    {
        frontNormal = -normal;
        front = posB.Dot(frontNormal) + hB.y;
        sideNormal = rotB.col1;
        const float side = posB.Dot(sideNormal);
        negSide = -side + hB.x; posSide = side + hB.x;
        negEdge = EDGE2; posEdge = EDGE4;
        ComputeIncidentEdge(incident, hA, posA, rotA, frontNormal);
        break;
    }
    }

    // 相手の辺を基準の面の両端（側面 2 枚）で切る
    ClipVertex clip1[2], clip2[2];
    if (ClipSegmentToLine(clip1, incident, -sideNormal, negSide, negEdge) < 2) return 0;
    if (ClipSegmentToLine(clip2, clip1, sideNormal, posSide, posEdge) < 2) return 0;

    // 基準の面より奥にある点だけが接触
    int num = 0;
    for (int i = 0; i < 2; ++i)
    {
        const float sep = frontNormal.Dot(clip2[i].v) - front;
        if (sep > 0.0f) continue;
        Contact& c = out[num];
        c = Contact{};
        c.separation = sep;
        c.normal = normal;
        c.position = clip2[i].v - frontNormal * sep;   // 基準の面の上へ寄せる
        FeaturePair fp = clip2[i].fp;
        if (axis == FACE_B_X || axis == FACE_B_Y) Flip(fp);
        c.feature = Pack(fp);
        ++num;
    }
    return num;
}

// ============================================================
// 物理 1 ステップ（Box2D-lite の World::Step と同じ流れ）
//   接触を作る（前ステップの累積インパルスを引き継ぐ）→ 重力 →
//   事前計算 + warm start → インパルスを反復 → 速度で位置を進める
// ============================================================
void SpellbookUI::StepPhysics(float dt)
{
    const float invDt = 1.0f / dt;
    const float cellPx = CellPx();

    // ---- 壁（左 右 上 床）。内寸の外側に置いた動かない大きな四角 ----
    const Vector2 center = (m_BoxMin + m_BoxMax) * 0.5f;
    const Vector2 half = (m_BoxMax - m_BoxMin) * 0.5f;
    const float ht = kWallThick * 0.5f;
    Box walls[4];
    walls[0] = { { m_BoxMin.x - ht, center.y }, { ht, half.y + kWallThick } };
    walls[1] = { { m_BoxMax.x + ht, center.y }, { ht, half.y + kWallThick } };
    walls[2] = { { center.x, m_BoxMin.y - ht }, { half.x + kWallThick, ht } };
    walls[3] = { { center.x, m_BoxMax.y + ht }, { half.x + kWallThick, ht } };

    // ---- マスの四角を先に全部作る ----
    std::vector<int> firstBox(m_Bodies.size() + 1, 0);
    std::vector<Box> boxes;
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        firstBox[i] = (int)boxes.size();
        for (int k = 0; k < (int)m_Bodies[i].cells.size(); ++k)
            boxes.push_back(CellBox(m_Bodies[i], k));
    }
    firstBox[m_Bodies.size()] = (int)boxes.size();

    // ---- 接触 ----
    std::map<uint64_t, Arbiter> next;
    auto key = [](uint32_t uidA, int cellA, uint32_t uidB, int cellB)
    {
        return ((uint64_t)(uidA & 0xFFFFFF) << 40) | ((uint64_t)(cellA & 0xFF) << 32)
            | ((uint64_t)(uidB & 0xFFFFFF) << 8) | (uint64_t)(cellB & 0xFF);
    };
    auto addPair = [&](uint64_t k, int ia, int ib, const Box& A, const Box& B)
    {
        Contact cs[2];
        const int num = Collide(cs, A, B);
        if (num == 0) return;
        Arbiter arb;
        arb.a = ia;
        arb.b = ib;
        arb.numContacts = num;
        auto old = m_Arbiters.find(k);
        for (int i = 0; i < num; ++i)
        {
            arb.contacts[i] = cs[i];
            if (old == m_Arbiters.end()) continue;
            for (int j = 0; j < old->second.numContacts; ++j)
            {
                if (old->second.contacts[j].feature != cs[i].feature) continue;
                arb.contacts[i].Pn = old->second.contacts[j].Pn;
                arb.contacts[i].Pt = old->second.contacts[j].Pt;
                break;
            }
        }
        next.emplace(k, arb);
    };

    for (int i = 0; i < (int)m_Bodies.size(); ++i)
    {
        const auto& bi = m_Bodies[i];
        const float r = bi.bound * cellPx;

        // 壁（外接円が届く壁だけ）
        const bool nearWall[4] = {
            bi.pos.x - r < m_BoxMin.x, bi.pos.x + r > m_BoxMax.x,
            bi.pos.y - r < m_BoxMin.y, bi.pos.y + r > m_BoxMax.y };
        for (int w = 0; w < 4; ++w)
        {
            if (!nearWall[w]) continue;
            for (int k = firstBox[i]; k < firstBox[i + 1]; ++k)
                addPair(key(kWallUid[w], 0, bi.uid, k - firstBox[i]), -1, i, walls[w], boxes[k]);
        }

        // 他の物（外接円で粗く弾いてからマス同士）
        for (int j = i + 1; j < (int)m_Bodies.size(); ++j)
        {
            const auto& bj = m_Bodies[j];
            const float rr = r + bj.bound * cellPx;
            if ((bi.pos - bj.pos).LengthSquared() > rr * rr) continue;

            // A = uid の小さい方（キーと法線の向きを毎ステップ揃える）
            const bool iFirst = bi.uid < bj.uid;
            const int ia = iFirst ? i : j, ib = iFirst ? j : i;
            for (int ka = firstBox[ia]; ka < firstBox[ia + 1]; ++ka)
                for (int kb = firstBox[ib]; kb < firstBox[ib + 1]; ++kb)
                    addPair(key(m_Bodies[ia].uid, ka - firstBox[ia], m_Bodies[ib].uid, kb - firstBox[ib]),
                        ia, ib, boxes[ka], boxes[kb]);
        }
    }
    m_Arbiters.swap(next);

    // ---- 重力 ----
    for (auto& b : m_Bodies)
        b.vel.y += gravity * dt;

    auto invMass = [&](int i) { return i < 0 ? 0.0f : 1.0f / (float)m_Bodies[i].cells.size(); };
    auto invI = [&](int i) { return i < 0 ? 0.0f : 1.0f / (m_Bodies[i].inertia * cellPx * cellPx); };
    Vector2 zeroV = { 0, 0 };
    float zeroW = 0.0f;
    auto velOf = [&](int i) -> Vector2& { return i < 0 ? (zeroV = { 0, 0 }) : m_Bodies[i].vel; };
    auto angOf = [&](int i) -> float& { return i < 0 ? (zeroW = 0.0f) : m_Bodies[i].angVel; };
    auto posOf = [&](int i, const Contact& c) { return i < 0 ? c.position : m_Bodies[i].pos; };

    // ---- 事前計算 + warm start ----
    m_ContactCount = 0;
    for (auto& kv : m_Arbiters)
    {
        Arbiter& arb = kv.second;
        const float mA = invMass(arb.a), mB = invMass(arb.b);
        const float iA = invI(arb.a), iB = invI(arb.b);
        for (int k = 0; k < arb.numContacts; ++k)
        {
            Contact& c = arb.contacts[k];
            c.r1 = c.position - posOf(arb.a, c);
            c.r2 = c.position - posOf(arb.b, c);

            const float rn1 = c.r1.Dot(c.normal), rn2 = c.r2.Dot(c.normal);
            float kNormal = mA + mB
                + iA * (c.r1.Dot(c.r1) - rn1 * rn1) + iB * (c.r2.Dot(c.r2) - rn2 * rn2);
            c.massNormal = 1.0f / kNormal;

            const Vector2 tangent = { c.normal.y, -c.normal.x };
            const float rt1 = c.r1.Dot(tangent), rt2 = c.r2.Dot(tangent);
            float kTangent = mA + mB
                + iA * (c.r1.Dot(c.r1) - rt1 * rt1) + iB * (c.r2.Dot(c.r2) - rt2 * rt2);
            c.massTangent = 1.0f / kTangent;

            c.bias = (std::min)(kMaxBias,
                -kBiasFactor * invDt * (std::min)(0.0f, c.separation + kAllowedPenetration));

            // 速くぶつかった時だけ跳ねる
            const Vector2 dv = velOf(arb.b) + Cross(angOf(arb.b), c.r2) - velOf(arb.a) - Cross(angOf(arb.a), c.r1);
            const float vn = dv.Dot(c.normal);
            if (vn < -kBounceThreshold)
                c.bias = (std::max)(c.bias, -restitution * vn);

            const Vector2 P = c.normal * c.Pn + tangent * c.Pt;
            if (arb.a >= 0) { m_Bodies[arb.a].vel -= P * mA; m_Bodies[arb.a].angVel -= iA * Cross(c.r1, P); }
            if (arb.b >= 0) { m_Bodies[arb.b].vel += P * mB; m_Bodies[arb.b].angVel += iB * Cross(c.r2, P); }
            ++m_ContactCount;
        }
    }

    // ---- インパルスの反復（法線 → 摩擦。累積値でクランプ）----
    for (int it = 0; it < iterations; ++it)
    {
        for (auto& kv : m_Arbiters)
        {
            Arbiter& arb = kv.second;
            const float mA = invMass(arb.a), mB = invMass(arb.b);
            const float iA = invI(arb.a), iB = invI(arb.b);
            for (int k = 0; k < arb.numContacts; ++k)
            {
                Contact& c = arb.contacts[k];

                Vector2 dv = velOf(arb.b) + Cross(angOf(arb.b), c.r2) - velOf(arb.a) - Cross(angOf(arb.a), c.r1);
                const float vn = dv.Dot(c.normal);
                float dPn = c.massNormal * (-vn + c.bias);
                const float Pn0 = c.Pn;
                c.Pn = (std::max)(Pn0 + dPn, 0.0f);
                dPn = c.Pn - Pn0;

                Vector2 P = c.normal * dPn;
                if (arb.a >= 0) { m_Bodies[arb.a].vel -= P * mA; m_Bodies[arb.a].angVel -= iA * Cross(c.r1, P); }
                if (arb.b >= 0) { m_Bodies[arb.b].vel += P * mB; m_Bodies[arb.b].angVel += iB * Cross(c.r2, P); }

                dv = velOf(arb.b) + Cross(angOf(arb.b), c.r2) - velOf(arb.a) - Cross(angOf(arb.a), c.r1);
                const Vector2 tangent = { c.normal.y, -c.normal.x };
                const float vt = dv.Dot(tangent);
                float dPt = c.massTangent * (-vt);
                const float maxPt = friction * c.Pn;
                const float Pt0 = c.Pt;
                c.Pt = std::clamp(Pt0 + dPt, -maxPt, maxPt);
                dPt = c.Pt - Pt0;

                P = tangent * dPt;
                if (arb.a >= 0) { m_Bodies[arb.a].vel -= P * mA; m_Bodies[arb.a].angVel -= iA * Cross(c.r1, P); }
                if (arb.b >= 0) { m_Bodies[arb.b].vel += P * mB; m_Bodies[arb.b].angVel += iB * Cross(c.r2, P); }
            }
        }
    }

    // ---- 位置を進める + 減衰 + 保険（重心が箱の外へ出たら戻す）----
    float maxV = 0.0f, maxW = 0.0f;
    for (auto& b : m_Bodies)
    {
        b.pos += b.vel * dt;
        b.angle += b.angVel * dt;
        b.vel *= kLinearDamp;
        b.angVel *= kAngularDamp;

        b.pos.x = std::clamp(b.pos.x, m_BoxMin.x, m_BoxMax.x);
        b.pos.y = std::clamp(b.pos.y, m_BoxMin.y, m_BoxMax.y);

        maxV = (std::max)(maxV, b.vel.Length());
        maxW = (std::max)(maxW, std::fabs(b.angVel));
    }
    m_MaxV = maxV;
    m_MaxW = maxW;

    // ---- 静かな収め（2026-10-04）----
    // 基本魔法が Z・L・凸字の多マスになってから、斜めに寄り掛かった物が摩擦の限界の角度でゆっくり滑り続け、
    // 箱がいつまでも眠らなくなった（自動テスト chest：10 秒経っても 4〜14 px/s・0.1〜0.3 rad/s）。
    // 全体がゆっくりになって kQuietTime 続いたら強く減衰させて止める（倒れ込む・落ちる速い動きには掛からない）。
    // 1 ステップだけの接触の跳ね（瞬間の速さ）で毎回やり直しにならないよう、速さは時定数 kQuietSmooth で均して見る
    const float k = 1.0f - std::exp(-dt / kQuietSmooth);
    m_QuietV += (maxV - m_QuietV) * k;
    m_QuietW += (maxW - m_QuietW) * k;
    if (m_QuietV < kQuietLinear && m_QuietW < kQuietAngular)
    {
        m_QuietTimer += dt;
        if (m_QuietTimer >= kQuietTime)
            for (auto& b : m_Bodies) { b.vel *= kSettleDamp; b.angVel *= kSettleDamp; }
    }
    else
        m_QuietTimer = 0.0f;

    // ---- 箱ごと眠る（全部止まって一定時間）----
    if (allowSleep && maxV < kSleepLinear && maxW < kSleepAngular)
    {
        m_SleepTimer += dt;
        if (m_SleepTimer >= kSleepTime)
        {
            m_Asleep = true;
            for (auto& b : m_Bodies) { b.vel = { 0, 0 }; b.angVel = 0.0f; }
        }
    }
    else
        m_SleepTimer = 0.0f;
}

// ============================================================
// 点が乗っている物。後ろ（後に描かれた = 上に見える）から当てる
// ============================================================
int SpellbookUI::HitTest(const Vector2& p) const
{
    const float cellPx = CellPx();
    for (int i = (int)m_Bodies.size() - 1; i >= 0; --i)
    {
        const auto& b = m_Bodies[i];
        const Vector2 d = p - b.pos;
        if (d.LengthSquared() > b.bound * b.bound * cellPx * cellPx) continue;

        // 物の座標系（マス単位）へ戻して、どれかのマスに入っているか
        const float c = std::cos(b.angle), s = std::sin(b.angle);
        const Vector2 local = Vector2(c * d.x + s * d.y, -s * d.x + c * d.y) / cellPx;
        for (const auto& cell : b.cells)
            if (std::fabs(local.x - cell.x) <= 0.5f && std::fabs(local.y - cell.y) <= 0.5f)
                return i;
    }
    return -1;
}

// ============================================================
// 掴み判定
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
    const int i = HitTest(Vector2(mp.x, mp.y));
    if (i < 0) return;
    const auto& b = m_Bodies[i];

    // ---- DragContext へ引き渡す ----
    AudioSystem::Get().Play("item_pick");
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
    Wake();
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

    // ---- 物理（固定ステップ。眠っている間は回さない）----
    if (!allowSleep) m_Asleep = false;
    if (m_Asleep)
        m_PhysAccum = 0.0f;
    else
    {
        m_PhysAccum += dt;
        if (m_PhysAccum > kMaxAccum) m_PhysAccum = kMaxAccum;

        while (m_PhysAccum >= kFixedStep && !m_Asleep)
        {
            StepPhysics(kFixedStep);
            m_PhysAccum -= kFixedStep;
        }
    }

    TryGrab();

    // ---- マウスが乗っている物（tooltip 用）----
    m_HasHover = false;
    if (!(m_Drag && m_Drag->IsActive()) && !ImGui::GetIO().WantCaptureMouse)
    {
        const auto mp = InputManager::Get().GetMousePos();
        const int i = HitTest(Vector2(mp.x, mp.y));
        if (i >= 0)
        {
            m_HoverId = m_Bodies[i].id;
            m_HasHover = true;
        }
    }
}

// ============================================================
// 描画
// 落ち影 → 奥の板 → 中身 → 枠（中身の縁に被さる）→ 錠前。
// 中身はマスごとに四角を描き、マスの中心を軸に物の角度で回す
// （中心は重心から回して求めてあるので、全体が剛体として回って見える）
// ============================================================
void SpellbookUI::Draw(SpriteRenderer& sprite)
{
    if (!m_BlockTex) return;

    const auto& white = UIDeco::Tex().white ? UIDeco::Tex().white : m_BlockTex;

    const Vector2 outerPos = { m_BoxMin.x - m_Wall, m_BoxMin.y - m_Wall };
    const Vector2 innerSize = m_BoxMax - m_BoxMin;
    const Vector2 outerSize = { innerSize.x + m_Wall * 2.0f, innerSize.y + m_Wall * 2.0f };

    // ---- 箱の奥 ----
    if (m_FrameTex && m_BackTex)
    {
        sprite.Draw(white, { outerPos.x + m_Wall * 0.25f, outerPos.y + m_Wall * 0.4f }, outerSize, shadowColor);
        sprite.Draw(m_BackTex, m_BoxMin, innerSize, backTint);
    }
    else
    {
        // 木箱の絵が無い時は幻想 UI のパネルで代用
        UIDeco::PanelStyle ps;
        ps.innerInset = (std::max)(3.0f, m_Wall * 0.35f);
        ps.cornerSize = outerSize.x * 0.16f;
        UIDeco::DrawPanel(sprite, outerPos, outerSize, UIDeco::TintColor(UIDeco::Tint::Gold), ps);
    }

    // ---- 中身 ----
    const float cellPx = CellPx();
    const float halfPx = cellPx * 0.5f;
    const float th = (std::max)(1.0f, cellPx * 0.05f);

    for (const auto& b : m_Bodies)
    {
        const ItemCommon* c = ItemDatabase::GetCommon(b.id);
        if (!c) continue;

        // バックパックと同じ「色ガラス」：アイテムの色を沈めた地 + 外周だけ種類の色の縁
        // （同じ物のマス同士の境目には縁を引かないので、一続きの形に見える）
        Vector4 edge = UIDeco::CategoryColor(c->category);
        edge.w = 0.95f;
        const Vector4 glass = { c->color.x * ShapeSprite::kGlassDim, c->color.y * ShapeSprite::kGlassDim,
            c->color.z * ShapeSprite::kGlassDim, 1.0f };

        for (int k = 0; k < (int)b.cells.size(); ++k)
        {
            const Box box = CellBox(b, k);
            const Vector2 tl = { box.pos.x - halfPx, box.pos.y - halfPx };
            sprite.Draw(white, tl, { cellPx, cellPx }, glass, b.angle, box.pos);

            const uint8_t open = b.openSides[k];
            if (open & 1) sprite.Draw(white, tl, { th, cellPx }, edge, b.angle, box.pos);
            if (open & 2) sprite.Draw(white, { tl.x + cellPx - th, tl.y }, { th, cellPx }, edge, b.angle, box.pos);
            if (open & 4) sprite.Draw(white, tl, { cellPx, th }, edge, b.angle, box.pos);
            if (open & 8) sprite.Draw(white, { tl.x, tl.y + cellPx - th }, { cellPx, th }, edge, b.angle, box.pos);
        }

        // アイコンは中心のマスに 1 つだけ（暖かい白）
        if (auto icon = GetIcon(b.id))
        {
            const Box box = CellBox(b, b.iconCell);
            const float s = cellPx * ShapeSprite::kIconScale;
            sprite.Draw(icon, { box.pos.x - s * 0.5f, box.pos.y - s * 0.5f }, { s, s },
                ShapeSprite::kIconTint, b.angle, box.pos);
        }
    }

    // ---- 枠（中身の縁に被さる）+ 前板の錠前 ----
    if (m_FrameTex)
        DrawNineSlice(sprite, m_FrameTex, outerPos, outerSize, m_Wall, kFrameBorderUV, frameTint);
    if (m_LockTex)
    {
        const float lw = m_Wall * lockScale;
        const float lh = lw * kLockAspect;
        sprite.Draw(m_LockTex, { (m_BoxMin.x + m_BoxMax.x) * 0.5f - lw * 0.5f, m_BoxMax.y + m_Wall * 0.12f },
            { lw, lh }, frameTint);
    }
}
