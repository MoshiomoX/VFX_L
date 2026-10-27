// ============================================================
// SpellbookUI.h
// 魔法書。習得済みでグリッド未配置のブロックが物理で木箱に溜まる。
//
// データは持たない。箱の中身は毎フレームの差分同期で決まる:
//   あるべき数 = SpellbookComponent の所持数 - グリッド配置数
//                - （自分の箱から掴んでドラッグ中なら 1）
//   多ければ消し、足りなければ箱の上から降らせる。
//   グリッドから抜けば勝手に降ってくるし、掴んで置き損ねれば
//   勝手に降って戻る。どちらの UI も相手に通知しない。
//
// 物理は内製の 2D 剛体（Registry / PhysicsSystem とは無関係。2026-09-30 作り直し）:
//   1 個 = occupyCells のマスを繋げた複合の四角（本当の形で当たる。十字も L 字も）。
//   マス同士は Box2D-lite と同じ「四角 vs 四角」の切り取りで最大 2 点の接触を作り、
//   逐次インパルス（累積 + クランプ）で法線・摩擦を解く。接触は特徴番号で前ステップと
//   対応付けてインパルスを引き継ぐ（warm start。積んでも震えない）。
//   壁 4 枚は動かない大きな四角。全部が止まったら箱ごと眠る（出し入れで起きる）。
//   固定ステップ 1/120 で積分（コマ落ちでも床を抜けない）。
//
// 掴んだ瞬間に body を消して DragContext へ引き渡す（掴んだ物は正立・等倍でグリッドの上に描かれる。
//   箱の中の角度・縮小から寄せていく演出は無い）。
//
// 見た目は木箱（Assets/Texture/UI/Chest、Tools/BuildChestUI.ps1 が生成）:
//   奥の板 → 中身 → 9 分割の枠（中身の縁に被さる）→ 前板の錠前
// ============================================================
#pragma once
#include "SpellID.h"
#include "UI/DragContext.h"
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include <SimpleMath.h>

class SpriteRenderer;
class Texture;
class Registry;
struct SpellbookComponent;
struct BackpackComponent;

class SpellbookUI
{
public:
    void Initialize(std::shared_ptr<Texture> blockTex);
    void LoadIcons();
    void Layout(float screenW, float screenH);

    void SetDragContext(DragContext* drag) { m_Drag = drag; }

    // グリッドとマス寸を揃える（ドラッグ中に大きさが跳ばないように）
    void SetCellSize(float cellSize) { m_CellSize = cellSize; }
    // グリッドの 1 マスの送り（マス + 隙間）。ドラッグ中の絵の位置から、離した所に物を出す時に使う（2026-10-27）
    void SetCellPitch(float pitch) { m_CellPitch = pitch; }
    // TEMP-TEST：マウスで離したのと同じ落とし所を置く（自動テストはマウスを動かさないので）
    void TestDropHint(ItemID id, const DirectX::SimpleMath::Vector2& center, int rotation)
    {
        m_DropHint.valid = true;
        m_DropHint.id = id;
        m_DropHint.center = center;
        m_DropHint.angle = (float)(rotation & 3) * 1.5707963f;
        m_DropHint.ttl = 0.3f;
    }

    // 同期 → 物理 → 掴み判定。Backpack 層が開いている間だけ呼ばれる
    void Update(const SpellbookComponent& book, const BackpackComponent& bp, float dt);

    void Draw(SpriteRenderer& sprite);

    // 画面の縁が壁 + 蓋の無い木箱（2026-10-27、ユーザー「箱の蓋を取って画面の縁全体で当たるように」→
    // 「箱はやっぱり要る、少し下に置いて、魔法が多い時は箱からこぼれ落ちるように」）。
    // true = 物理の範囲は画面全体（4 辺が壁）。木箱は右下に置き、左の板・右の板・底の 3 枚だけが壁（上は開いている）。
    //        新しい物は箱の真上の画面の上から降ってきて箱に入り、溢れた分は箱の縁から画面へこぼれる。
    //        SetObstacle の四角（バックパックの列）も壁。false = 以前の右の真ん中の閉じた木箱
    bool screenWalls = true;
    // 画面の縁の時に、中身が入らない四角（バックパックの盤面の列）。GameUI が Layout の後に渡す。max <= min なら無し
    void SetObstacle(const DirectX::SimpleMath::Vector2& min, const DirectX::SimpleMath::Vector2& max)
    {
        m_ObstacleMin = min;
        m_ObstacleMax = max;
        m_HasObstacle = (max.x > min.x && max.y > min.y);
        Wake();
    }

    // ---- 見た目の調整（ImGui から触る）----
    float boxScreenRatio = 0.42f;    // 箱の内寸が画面短辺に占める割合
    float marginRatio = 0.02f;    // 画面端からの距離
    float wallRatio = 0.075f;   // 内寸に対する枠の太さ
    float boxScale = 0.85f;    // 箱の中でのブロック縮小率（2026-09-30 0.6 → 0.85：小さすぎて箱の底に薄く溜まるだけだった）
    float lockScale = 1.3f;     // 錠前の幅（枠の太さ比）

    // 木箱の絵に掛ける色（線形。絵は sRGB で読んで線形に戻っている）
    DirectX::SimpleMath::Vector4 frameTint = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::SimpleMath::Vector4 backTint = { 1.0f, 1.0f, 1.0f, 1.0f };
    DirectX::SimpleMath::Vector4 shadowColor = { 0.0f, 0.0f, 0.0f, 0.55f };   // 箱の落ち影

    // ---- 物理の調整 ----
    float gravity = 1800.0f;   // px/s^2
    float restitution = 0.15f;     // 反発（小さめ。跳ねすぎると収納に見えない）
    float friction = 0.55f;     // クーロン摩擦係数
    int   iterations = 20;        // インパルスの反復回数（2026-10-04 10 → 20：多マスの凹んだ形が寄り掛かると 10 回では収まらない）
    bool  allowSleep = true;      // 全部止まったら計算を止める

    int GetBodyCount() const { return (int)m_Bodies.size(); }
    int GetContactCount() const { return m_ContactCount; }
    bool IsAsleep() const { return m_Asleep; }
    float GetMaxSpeed() const { return m_MaxV; }        // 直前のステップの最大速度（px/s）
    float GetMaxAngSpeed() const { return m_MaxW; }     // 同 角速度（rad/s）

    // マウスが乗っている箱の中の物（tooltip 用）。無ければ false
    bool GetHoveredItem(ItemID& out) const { out = m_HoverId; return m_HasHover; }

    // ---- パッド操作（BackpackPadControl から）----
    // false の間はマウスを見ない（掴み・hover をパッドの選択に任せる）
    void SetMouseEnabled(bool on) { m_MouseEnabled = on; }
    // 箱の中の i 番目の物の uid と重心の位置。範囲外は false
    bool GetBodyInfo(int i, uint32_t& uid, DirectX::SimpleMath::Vector2& pos) const;
    // パッドで選んでいる物（0 = 無し）。明るい縁で少し浮かせて描き、tooltip の対象にもなる
    void SetPadSelection(uint32_t uid) { m_PadSelUid = uid; }
    // uid の物を掴んで DragContext へ渡す（マウスの掴みと同じ）。無ければ false
    bool GrabByUid(uint32_t uid);

private:
    // ============================================================
    // 箱の中の1個。真値ではなく所持数の視覚表現。
    // pos は重心。マスの中心は重心からの相対（マス単位）で持ち、
    // 使う時にマス寸を掛ける（画面サイズが変わっても形が崩れない）
    // ============================================================
    struct BodyState
    {
        uint32_t uid = 0;              // 接触の対応付け用（vector の添字は消すとずれる）
        ItemID id = ItemID::Fireball;
        std::vector<DirectX::SimpleMath::Vector2> cells;   // 重心からの相対（マス単位）
        std::vector<uint8_t> openSides;   // マスごとの「隣に同じ物のマスが無い辺」bit0 左 1 右 2 上 3 下
        int iconCell = 0;              // アイコンを描くマス
        float bound = 0.5f;            // 外接円の半径（マス単位）
        float inertia = 1.0f;          // 慣性モーメント / マス寸^2（質量 1 / マス）

        DirectX::SimpleMath::Vector2 pos = { 0, 0 };
        DirectX::SimpleMath::Vector2 vel = { 0, 0 };
        float angle = 0.0f;    // rad（画面は Y 下向きなので正 = 時計回り）
        float angVel = 0.0f;    // rad/s
    };

    // 四角 1 個（マス or 壁）。h は半分の大きさ
    struct Box
    {
        DirectX::SimpleMath::Vector2 pos;
        DirectX::SimpleMath::Vector2 h;
        float c = 1.0f, s = 0.0f;       // 回転の cos / sin
    };

    struct Contact
    {
        DirectX::SimpleMath::Vector2 position;
        DirectX::SimpleMath::Vector2 normal;   // A → B
        float separation = 0.0f;
        uint32_t feature = 0;          // どの辺同士か（前ステップとの対応付け）
        DirectX::SimpleMath::Vector2 r1, r2;   // 重心から接触点
        float Pn = 0.0f, Pt = 0.0f;    // 累積インパルス
        float massNormal = 0.0f, massTangent = 0.0f, bias = 0.0f;
    };

    // 2 つのマス（片方は壁でも良い）の間の接触。キーは uid とマス番号
    struct Arbiter
    {
        int a = -1, b = -1;            // m_Bodies の添字。-1 = 壁（動かない）
        int numContacts = 0;
        Contact contacts[2];
    };

    static int Collide(Contact out[2], const Box& A, const Box& B);

    void SyncBodies(const SpellbookComponent& book, const BackpackComponent& bp);
    void StepPhysics(float dt);
    void TryGrab();
    void GrabBody(int index);   // body を消して DragContext へ引き渡す
    void SpawnBody(ItemID id);
    void Wake() { m_Asleep = false; m_SleepTimer = 0.0f; m_QuietTimer = 0.0f; m_QuietV = 1.0e3f; m_QuietW = 1.0e3f; }
    float CellPx() const { return m_CellSize * boxScale; }
    Box CellBox(const BodyState& b, int cell) const;
    int HitTest(const DirectX::SimpleMath::Vector2& p) const;   // 上に見える物から。無ければ -1

    std::shared_ptr<Texture> m_BlockTex;
    std::shared_ptr<Texture> m_FrameTex, m_BackTex, m_LockTex;
    std::vector<std::pair<ItemID, std::shared_ptr<Texture>>> m_Icons;
    std::shared_ptr<Texture> GetIcon(ItemID id) const;

    DragContext* m_Drag = nullptr;

    std::vector<BodyState> m_Bodies;
    // 接触はキー（uid とマス番号）の昇順に並べた平らな列（2026-10-25。以前は std::map で、1/120 秒ごとに
    // 接触 1 つにつき 1 回確保していた）。解く順は map と同じキー順のまま。
    // 作業用の列・マスの四角も毎ステップ作らず使い回す
    struct KeyedArbiter
    {
        uint64_t key = 0;
        Arbiter  arb;
    };
    std::vector<KeyedArbiter> m_Arbiters;       // 今のステップの接触（key 昇順）
    std::vector<KeyedArbiter> m_NextArbiters;   // 作業用（次の接触を集める）
    std::vector<int> m_FirstBox;                // body ごとの最初のマスの四角（m_CellBoxes の添字）
    std::vector<Box> m_CellBoxes;               // 全部の body のマスの四角
    uint32_t m_NextUid = 16;      // 1〜4 は壁
    float m_PhysAccum = 0.0f;
    bool  m_Asleep = false;
    float m_SleepTimer = 0.0f;
    float m_QuietTimer = 0.0f;   // 全体がゆっくりな時間（静かな収めの判定）
    float m_QuietV = 1.0e3f, m_QuietW = 1.0e3f;   // 均した全体の速さ（px/s・rad/s）
    int   m_ContactCount = 0;
    float m_MaxV = 0.0f, m_MaxW = 0.0f;

    ItemID m_HoverId = ItemID::Fireball;   // m_HasHover の時だけ意味がある
    bool   m_HasHover = false;

    bool     m_MouseEnabled = true;   // SetMouseEnabled
    uint32_t m_PadSelUid = 0;         // SetPadSelection

    // 箱の内寸（Layout で確定。枠の内側）
    DirectX::SimpleMath::Vector2 m_BoxMin = { 0, 0 };
    DirectX::SimpleMath::Vector2 m_BoxMax = { 0, 0 };
    float m_Wall = 12.0f;
    // 物理の範囲（外側の 4 枚の壁）。screenWalls の時は画面全体、そうでなければ箱の内寸と同じ
    DirectX::SimpleMath::Vector2 m_AreaMin = { 0, 0 };
    DirectX::SimpleMath::Vector2 m_AreaMax = { 0, 0 };
    DirectX::SimpleMath::Vector2 m_ObstacleMin = { 0, 0 };   // SetObstacle（画面の縁の時だけ当たる）
    DirectX::SimpleMath::Vector2 m_ObstacleMax = { 0, 0 };
    bool m_HasObstacle = false;
    bool Blocked() const { return screenWalls && m_HasObstacle; }
    // 重心を障害物の外（右）へ出す
    void PushOutOfObstacle(DirectX::SimpleMath::Vector2& p, float r) const
    {
        if (!Blocked()) return;
        if (p.x - r < m_ObstacleMax.x && p.y + r > m_ObstacleMin.y && p.x + r > m_ObstacleMin.x && p.y - r < m_ObstacleMax.y)
            p.x = (std::max)(p.x, m_ObstacleMax.x + r);
    }

    float m_CellSize = 56.0f;   // グリッドと同じマス寸（フルサイズ）
    float m_CellPitch = 60.0f;  // グリッドの 1 マスの送り（SetCellPitch）

    // ---- マウスで離した所から落とす（2026-10-27、ユーザー「マウスで離した時、箱の上に湧くのではなく、その場から落ちるように」）----
    // ドラッグ中は毎フレーム、絵（DrawDragged と同じ = マウス − grabOffset が要のマスの左上）の重心と向きを覚えておき、
    // ドラッグが終わったフレームに「落とし所」にする。直後に同じ物を出す時（SyncBodies）はそこに、その向きで出す
    struct DropHint
    {
        bool   valid = false;
        ItemID id = ItemID::Fireball;
        DirectX::SimpleMath::Vector2 center = { 0, 0 };   // 形の重心（画面 px）
        float  angle = 0.0f;                               // rotation × 90 度
        float  ttl = 0.0f;                                 // 秒。切れたら捨てる（グリッドに置けた時など）
    };
    bool     m_DragSeen = false;   // 前のフレームにマウスでドラッグ中だったか
    DropHint m_DragNow;            // ドラッグ中の今の位置（終わったら m_DropHint へ）
    DropHint m_DropHint;
    void TrackDrag(float dt);
};
