#pragma once
// ============================================================
// TitleLogo.h
// タイトルの名前を「名前の絵の小片」の集まりとして描く。
//   Build で名前（後光・縁取り込み）を一度だけ小さな絵に描いて読み戻し、tile px 角の小片に切る。
//   普段は小片が元の所にぴったり並ぶので普通の文字に見える。マウスで掃く・時々横切るそよ風で
//   小片が散って光り、少し漂ってからばねで戻る。Burst = 全部を風下へ吹き飛ばして消す（決定した時）
// ============================================================
#include <SimpleMath.h>
#include <d3d11.h>
#include <functional>
#include <memory>
#include <vector>

class SpriteRenderer;
class Texture;

class TitleLogo
{
public:
    using Vector2 = DirectX::SimpleMath::Vector2;
    using Vector4 = DirectX::SimpleMath::Vector4;
    // origin = 名前の左上（ここへ描く）。この中で TextRenderer の Begin / Draw / End まで済ませる
    using DrawFn = std::function<void(const Vector2& origin)>;

    // size = 名前の大きさ（px）、margin = 後光・縁取りの分の余白（px）、tile = 小片の一辺（px）
    bool Build(ID3D11Device* device, ID3D11DeviceContext* context, const Vector2& size, float margin, float tile,
        const DrawFn& draw);
    bool Ready() const { return m_Texture != nullptr && !m_Tiles.empty(); }
    const Vector2& BuiltSize() const { return m_Size; }

    // origin = 今の名前の左上、mouse / mouseVel = マウスの画面位置（px）と速さ（px/秒）、k = UI の倍率
    void Update(float dt, const Vector2& origin, const Vector2& mouse, const Vector2& mouseVel, float k);
    void Burst();                              // 全部を吹き飛ばす（戻らない）
    bool Bursting() const { return m_BurstAge >= 0.0f; }

    // tint = 名前の色に掛ける色（rgb は HDR の明るさ、a は濃さ）。glow = 散った小片の光の円
    void Draw(SpriteRenderer& sprite, const Vector2& origin, const Vector4& tint, const std::shared_ptr<Texture>& glow) const;

    struct Settings
    {
        float mouseRadius = 70.0f;      // マウスが小片を散らす半径（px × k）
        float mouseMinSpeed = 220.0f;   // これより遅い動きでは散らない（px/秒 × k）
        float mouseKick = 0.55f;        // マウスの速さのどれだけを小片に渡すか
        float holdMin = 0.35f, holdMax = 0.9f;   // 散ってから戻り始めるまでの秒
        float drag = 2.4f;              // 漂う間の減速（1/秒）
        float spring = 38.0f;           // 戻るばね（1/秒²）
        float springDamp = 9.0f;        // 戻る時の減衰（1/秒）
        float breezeMin = 5.0f, breezeMax = 9.0f;   // そよ風の間隔（秒）
        float breezeSweep = 0.9f;       // そよ風が名前の端から端まで横切る秒
        float breezeChance = 0.18f;     // 風が通った所の小片のうち散る割合
        float breezeKick = 320.0f;      // そよ風の速さ（px/秒 × k）
        float burstTime = 0.9f;         // 吹き飛んで消えるまでの秒
        float sparkGain = 0.9f;         // 散った小片の光の強さ
    };
    Settings settings;

private:
    struct Tile
    {
        Vector2 home;     // 絵の中の左上（px）
        Vector2 off;      // 元の所からのずれ（px）
        Vector2 vel;      // px/秒
        float hold = 0.0f;
        float seed = 0.0f;
        int   sweep = -1;  // 最後に当たったそよ風の番号（同じ風で二度散らない）
    };

    std::shared_ptr<Texture> m_Texture;
    std::shared_ptr<Texture> m_Glow;   // 光の粒の柔らかい円
    std::vector<Tile> m_Tiles;
    Vector2 m_Size = { 0.0f, 0.0f };   // 名前の大きさ（Build の引数）
    Vector2 m_TexSize = { 0.0f, 0.0f };
    float m_Margin = 0.0f;
    float m_TileSize = 6.0f;
    float m_K = 1.0f;

    float m_Time = 0.0f;
    float m_BreezeTimer = 3.0f;
    float m_SweepX = -1.0f;            // そよ風の今の位置（名前の幅に対する割合。< 0 = 吹いていない）
    int   m_SweepId = 0;
    float m_BurstAge = -1.0f;
    unsigned m_Rng = 12345u;
    float Rand01();
};
