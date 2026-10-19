// ============================================================
// DamageNumbers.h
// ダメージの数字（2026-10-17、ユーザー「伤害字体」）。敵の頭の上に受けたダメージを浮かべて消す。
//   ・敵の分は GPU（SwarmDamageTextCS）が槽毎の hp の減りを数え、短い間隔（merge）で 1 つにまとめて
//     環に書く → SwarmSystem がリードバック（2〜3 フレーム遅れ）→ 場面が Add する
//   ・プレイヤーの分（受けたダメージ、赤）は場面が TrackPlayerHpLoss から Add する
// 描くのは GameUI::Render（HUD と同じ画面座標。viewProj で世界 → 画面）。
// 見た目：出た瞬間少し大きく、上へ昇りながら最後の 35% で薄れる。倒した一撃は大きく金色、毒は緑
// ============================================================
#pragma once
#include <SimpleMath.h>
#include <vector>
#include <random>

class TextRenderer;

class DamageNumbers
{
public:
    enum class Kind { Normal, Poison, Kill, Player };

    // element = Swarm::DamageElement（0 = 無し → 種類の色。それ以外は elementColors の色）
    void Add(const DirectX::SimpleMath::Vector3& worldPos, float amount, Kind kind, int element = 0);
    void Update(float dt);          // gameplay の dt（一時停止中は止まる）
    void Draw(TextRenderer& text, const DirectX::SimpleMath::Matrix& viewProj, float screenW, float screenH);
    void Clear() { m_Items.clear(); }
    int  ActiveCount() const { return (int)m_Items.size(); }
    uint32_t TotalAdded() const { return m_TotalAdded; }   // 自動テスト用
    uint32_t ElementCount(int e) const { return (e >= 0 && e < 9) ? m_ElementCounts[e] : 0; }   // 自動テスト用：属性毎の累計
    void DrawImGui();

    struct Style
    {
        bool  enabled = true;
        float life = 0.85f;          // 秒
        float rise = 1.1f;           // 上へ昇る m/s
        float scale = 0.62f;         // 文字の倍率（画面短辺 900px、行の高さ 46.34px の字体の時。Draw が今の字体の行の高さで正規化する）
        float killScale = 0.88f;     // 倒した一撃
        float playerScale = 0.72f;
        float bigAt = 50.0f;         // この量以上は scale × bigMul
        float bigMul = 1.3f;
        float pop = 0.35f;           // 出た瞬間の膨らみ（1 + pop から 0.12 秒で戻る）
        float jitter = 0.35f;        // 出る位置の横のばらつき m
        float fadeFrom = 0.65f;      // 寿命のこの割合から薄れる
        int   maxCount = 160;        // これ以上は古い物から消す
        DirectX::SimpleMath::Vector4 normalColor = { 1.0f, 0.96f, 0.80f, 1.0f };
        DirectX::SimpleMath::Vector4 poisonColor = { 0.55f, 1.0f, 0.35f, 1.0f };
        DirectX::SimpleMath::Vector4 killColor = { 1.0f, 0.80f, 0.25f, 1.0f };
        DirectX::SimpleMath::Vector4 playerColor = { 1.0f, 0.30f, 0.25f, 1.0f };
        DirectX::SimpleMath::Vector4 shadowColor = { 0.0f, 0.0f, 0.0f, 0.75f };
        // 縁取り（8 方向に同じ文字を描く。px は画面短辺 900 の時）。0 = 影だけ
        float outlinePx = 1.8f;      // 太い字体に合わせて 1.6 → 1.8（2026-10-18）
        DirectX::SimpleMath::Vector4 outlineColor = { 0.05f, 0.03f, 0.08f, 1.0f };
        bool  shadow = false;        // 縁取りの上に右下の影も（縁取りが 0 の時の代わり）
        // 属性の色（Swarm::DamageElement の順：none / fire / ice / lightning / arcane / earth / poison / wind / light）
        DirectX::SimpleMath::Vector4 elementColors[9] = {
            { 1.0f, 0.96f, 0.80f, 1.0f }, { 1.0f, 0.55f, 0.18f, 1.0f }, { 0.60f, 0.90f, 1.0f, 1.0f }, { 0.75f, 0.85f, 1.0f, 1.0f },
            { 0.70f, 0.60f, 1.0f, 1.0f }, { 0.85f, 0.70f, 0.45f, 1.0f }, { 0.55f, 1.0f, 0.35f, 1.0f }, { 0.75f, 1.0f, 0.88f, 1.0f },
            { 1.0f, 0.88f, 0.40f, 1.0f } };
    };
    Style style;

private:
    struct Item
    {
        DirectX::SimpleMath::Vector3 pos;
        float amount = 0.0f;
        Kind kind = Kind::Normal;
        int element = 0;
        float age = 0.0f;
        float drift = 0.0f;   // 横へ流れる m/s（少し散らす）
    };
    std::vector<Item> m_Items;
    std::mt19937 m_Rng{ 4242u };
    uint32_t m_TotalAdded = 0;
    uint32_t m_ElementCounts[9] = {};
};
