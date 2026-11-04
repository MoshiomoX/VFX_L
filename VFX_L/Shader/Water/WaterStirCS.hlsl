// ============================================================
// WaterStirCS.hlsl
// 池の中を歩く敵（World/WaterSurface、2026-10-28。
// ユーザー:「モンスターが水の中を歩いたら波紋を出して」）。GPU の群れの
// 敵スロット 1 つに 1 スレッド: 池の中に立って動いている生きた敵は
// 短いリスト（g_Stirs）に押し込みを 1 つ足す。WaterRippleCS はフレームの
// 最初のステップで、範囲の押し込みと同じようにリストを読む。プレイヤーが
// 水を歩くのと同じ: g_Stride 歩くごとに足音の押し込み + その間のかすかな
// 航跡（速度と前回のパスからの時間で強くなる）なので、
// 立ち止まった敵は水を静かなままにする。
// 草の足跡（GrassStampCS）と同じものを除外する: 最終ウェーブの幽霊 2 種、
// 砂嵐の術師の砂旋、埋まっている砂潜り / 砂の守護者、
// 消えた影の剣士、足が下の地面から明らかに浮いているもの
// （池の上を飛ぶ石像ガーゴイル）。
// リストは最大 g_StirMax 個。それを超えた分は捨てる。
// 氷の上（WaterIceCS、2026-11-01）では敵は波紋を出さず、代わりに
// 減速の範囲と同じ enemySlow を通して減速する。
// ============================================================
// SwarmCommon の cbuffer はここでは使わない。b0 から退ける（WaterStirCB）
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#include "../Common/SwarmCommon.hlsli"

// WaterSurface::StirCB と揃えること
cbuffer WaterStirCB : register(b0)
{
    float2 g_StirOrigin;     // マップの最小隅のワールド xz（= 水のマップ）
    float2 g_StirSize;       // メートル
    float g_StirStrength;    // （速度 1 m/s あたり）1 秒あたりに押し込む m
    float g_StirRadius;      // m、普通の雑魚
    float g_StirMinSpeed;    // m/s。これより遅い = 波紋なし
    float g_StirDt;          // 前回のパスからの秒
    uint g_StirMax;          // g_Stirs の容量
    uint g_HasHeight;        // 1 = g_Height がバインドされている
    float g_StepStrength;    // 足音 1 回で押し込む m
    float g_Stride;          // 足音 1 回あたりに歩く m
    float g_StirTime;        // 秒（水の時計、ポーズ中は止まる）
    float g_StirGroundY;     // 敵の position.y = 足 + これ（群れの AICB の groundY、0.9）
    float g_IceSlow;         // 氷の上で失う速度 0..1（エリートとボスはその半分）
    float _stirPad;
};

StructuredBuffer<SwarmEnemy> g_Enemies : register(t0);
Buffer<uint> g_EnemyStates : register(t1);         // 0 = 死亡
StructuredBuffer<SwarmEnemyExtra> g_EnemyExtra : register(t2);
Texture2D<float> g_Mask : register(t3);            // 1 = 池（波紋の場の texel）
Texture2D<float> g_Height : register(t4);          // GridWorld の高さ、texel = 高さセル（GrassRenderer::GetHeightSRV）
Texture2D<float> g_Ice : register(t5);             // 氷（WaterIceCS）、マスクの 4 分の 1 の大きさ
RWStructuredBuffer<float4> g_Stirs : register(u0); // x, z, 半径, 強さ（m、+ = 真ん中を押し下げる）
RWBuffer<uint> g_StirCount : register(u1);         // [0] = このパスで書いた押し込みの数（CPU がクリアする）
RWStructuredBuffer<float4> g_EnemySlow : register(u2); // 群れの enemySlow: x / y = 減速（残り秒、強さ）

// 中心の下の生の高さセルから足がこれ以上浮いている = 空中（= GrassStampCS）
static const float STIR_AIR_HEIGHT = 0.8;
// これより速く走る雑魚も押し込みは強くならない（突撃兵の突進）
static const float STIR_MAX_SPEED = 8.0;
// 氷による減速が最後に見てからどれだけ残るか（このパスは多くてもフレームに 1 回）
static const float STIR_ICE_LINGER = 0.2;
static const float STIR_ICE_SOLID = 0.05;   // = WaterRippleCS の RIPPLE_ICE_SOLID
static const uint STIR_ICE_SCALE = 4u;       // 氷の texel 1 つあたりのマスクの texel 数

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint j = id.x;
    uint count, stride;
    g_Enemies.GetDimensions(count, stride);
    if (j >= count || g_EnemyStates[j] == SWARM_DEAD)
        return;
    SwarmEnemyExtra ex = g_EnemyExtra[j];
    uint kind = ex.kind;
    if (SwarmIsGhost(kind) || kind == SWARM_KIND_TORNADO || SwarmBuried(ex)
        || (SwarmIsFixedElite(kind) && ex.fuse < -0.5))
        return;

    SwarmEnemy e = g_Enemies[j];
    float speed = length(e.velocity.xz);
    if (speed < g_StirMinSpeed)
        return;

    // 池の中か?
    uint mw, mh;
    g_Mask.GetDimensions(mw, mh);
    int2 mc = int2(floor((e.position.xz - g_StirOrigin) / g_StirSize * float2(mw, mh)));
    if (any(mc < int2(0, 0)) || any(mc >= int2((int) mw, (int) mh)) || g_Mask.Load(int3(mc, 0)) < 0.5)
        return;

    // 足が下の地面の近くにあるか
    if (g_HasHeight != 0u)
    {
        uint hw, hh;
        g_Height.GetDimensions(hw, hh);
        int2 hc = int2(floor((e.position.xz - g_StirOrigin) / g_StirSize * float2(hw, hh)));
        hc = clamp(hc, int2(0, 0), int2((int) hw - 1, (int) hh - 1));
        // position.y はカプセルの中心: 先に groundY を引く（引かないと 0.9 上にある全部の敵が浮いて見えた）
        if (e.position.y - g_StirGroundY - g_Height.Load(int3(hc, 0)) > STIR_AIR_HEIGHT)
            return;
    }
    else if (kind == SWARM_KIND_GARGOYLE)
        return;   // 飛んでいるかどうか分からない

    // 氷の上: 波紋なし、遅くなる
    if (g_Ice.Load(int3(int2(uint2(mc) / STIR_ICE_SCALE), 0)) >= STIR_ICE_SOLID)
    {
        float amount = g_IceSlow;
        if (SwarmIsBigElite(kind) || kind == SWARM_KIND_BOSS)
            amount *= SWARM_SLOW_BIG_MUL;
        float4 cur = g_EnemySlow[j];
        if (cur.x <= 0.0)
            cur.y = 0.0;
        cur.xy = float2(max(cur.x, STIR_ICE_LINGER), max(cur.y, amount));
        g_EnemySlow[j] = cur;
        return;
    }

    // 大きい敵は広い輪で押す（= GrassStampCS の足跡）
    float r = g_StirRadius;
    if (kind == SWARM_KIND_BOSS) r *= 3.0;
    else if (SwarmIsBigElite(kind)) r *= 2.2;
    else if (kind == SWARM_KIND_BRUTE) r *= 1.3;

    // 足音: g_Stride 歩くごとに 1 回、敵ごとに拍をずらす（スロットのハッシュ）。水量を変えずに敵と一緒に
    // 滑っていく押し込みはほとんど打ち消し合うので、一定の航跡だけではかすかな跡しか残らなかった。
    // 見えるのはプレイヤーの足音（PlayerVFXSystem）なので、敵にも足音をつける
    float rate = min(speed, STIR_MAX_SPEED) / max(g_Stride, 0.1);
    float beat = frac((float) (j * 2654435761u >> 8) / 16777216.0);
    bool stepped = floor(g_StirTime * rate + beat) != floor((g_StirTime - g_StirDt) * rate + beat);
    float s = stepped ? g_StepStrength : g_StirStrength * min(speed, STIR_MAX_SPEED) * g_StirDt;

    uint slot;
    InterlockedAdd(g_StirCount[0], 1u, slot);
    if (slot < g_StirMax)
        g_Stirs[slot] = float4(e.position.xz, r, s);
}
