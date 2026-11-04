// ============================================================
// SwarmEnemyVS.hlsl
// 雑魚メッシュのインスタンス描画。位置 / yaw は敵バッファから取り、
// 死んだスロットはニアプレーンの手前に潰して消す。
//
// ModelCommon の MVPBuffer を b0 で使う（World は単位行列、C++
// 側が書く）。SwarmCommon の cbuffer は b0 からずらしてある。
// enemies / enemyStates は ModelCommon のテクスチャと t0 / t1 を共有するが、
// VS はテクスチャを参照しないので問題ない。
//
// 動き：パーツアニメ表（b4 / t3、SwarmEnemyAnimCB 参照）があれば
// パーツが idle / walk / attack を再生する。無い場合（1 フレームに焼いた
// スキニングモデル、例：KayKit Minion）は旧来のプロシージャルな揺れを使う。
// ============================================================
#define SWARM_FRAME_CB_REG b1
#define SWARM_AI_CB_REG b2
#define SWARM_ORB_CB_REG b3
#define SWARM_BOMBER_CB_REG b5
#include "../Common/ModelCommon.hlsli"
#include "../Common/SwarmCommon.hlsli"

StructuredBuffer<SwarmEnemy> enemies : register(t0);
Buffer<uint> enemyStates : register(t1);
// 生きているスロットの番号（SwarmEnemyCompactCS）。InstanceID はプールではなくこれを引く。
// C++ 側は描画中の種類の一覧（雑魚、次に自爆兵）をバインドする
StructuredBuffer<uint> aliveList : register(t2);
// スロットごとの種類 + 導火線：点火した自爆兵はだんだん速く点滅して膨らみ、
// 突撃兵は溜めの間は赤く点滅する（2026-10-08）
StructuredBuffer<SwarmEnemyExtra> enemyExtra : register(t5);
// スロットごとの減速 + 凍結（z > 0 = アイスランスで凍結中：ポーズが止まり、体が氷色になる）
StructuredBuffer<float4> enemySlow : register(t4);
// スロットごとの溜まった毒素（x = まだ削る残りダメージ、毒沼、2026-10-17）：体が病的な緑になる
StructuredBuffer<float4> enemyPoison : register(t6);
#define SWARM_ICE_MAP_REG t7
#define SWARM_POND_TOP_REG t8
#include "../Common/SwarmIceLift.hlsli"
static const float3 kPoisonTint = float3(0.55, 1.15, 0.45);

// 自爆兵の点滅：1 秒あたりこの回数から始まり、2 つ目の値で終わる
static const float kFuseBlinkStart = 3.0;
static const float kFuseBlinkEnd = 14.0;
// 突撃兵の溜めの点滅、同じ上がり方（溜めの開始 / 終了時の 1 秒あたりの点滅回数）
static const float kChargeBlinkStart = 4.0;
static const float kChargeBlinkEnd = 12.0;
// 凍結：淡い青、g_FreezeTint を掛ける
static const float3 kIceTint = float3(0.75, 1.05, 1.7);
// エリート：共通のゾンビテクスチャに暗い赤を乗せる
static const float3 kEliteTint = float3(1.4, 0.55, 0.5);
// ボス：g_BossTint（BomberCB、ステージごと：草原は紫、砂漠は砂岩色。2026-10-24）
// 幽霊：半透明の青白、アルファブレンドで描く（SwarmSystem がこの一覧を最後に描く）
static const float3 kGhostTint = float3(0.55, 1.25, 1.6);
// 黒幽霊（2026-10-18）：黒に近い紫、より不透明、ゆっくりした紫の脈動付き
static const float3 kDarkGhostTint = float3(0.10, 0.03, 0.24);

// ---- 剛体パーツアニメ（パーツがアニメするノードにぶら下がるモデル、
// 例：Kenney Blocky：剛体パーツ 6 個、スキンの重みなし） ----
// 表の並び：[clip][frame][part]。各要素は行ベクトルのアフィン行列で、
// そのパーツの「焼いたポーズ」の頂点をそのフレームのポーズへ移す。
// なのでメッシュは焼いた 1 フレームのままでよい。C++ 側は
// サブメッシュを 1 つずつ描き、描く前に g_AnimPart を書く。
// クリップ：0 idle、1 walk、2 近接攻撃。C++ 側の対応：SwarmSystem::EnemyAnimCB
cbuffer SwarmEnemyAnimCB : register(b4)
{
    uint g_AnimPart;       // 描画中のサブメッシュ
    uint g_AnimPartCount;
    uint g_AnimEnabled;    // 0 = 表なし：プロシージャルな揺れに戻す
    uint g_AnimMode;       // 0 = パーツ g_AnimPart と一緒に動く、1 = 体の軸の周りを回るボスの飾り（2026-10-24）；2 翼、3 尾 / 首、4 脚、5 虫の脚、6 浮遊する魔神のパーツ（2026-10-29）
    uint4 g_ClipStart;     // 表の中での各クリップの先頭フレーム
    uint4 g_ClipFrames;
    float4 g_ClipLength;   // 秒
    float g_AnimTime;      // idle 用の時計（秒）
    float g_WalkRate;      // 歩きの再生速度
    float g_OrbitSpeed;    // g_AnimMode 1 用の rad/s
    float g_Hunch;         // 狼人の猫背（2026-10-27）：パーツ自身の動きの前に、腰（g_Hinge.xyz）を軸に前へ倒す rad。0 = なし
    float4 g_Flap;         // g_AnimMode 2（石像鬼の翼、2026-10-25）：ヒンジ（焼いたメッシュ空間）xyz、w = +1 右翼 / -1 左翼
    float4 g_Hinge;        // g_AnimMode 4 の第 2 関節（2026-10-27、狼人の膝）：xyz、w = 脚を上げる間の曲げ rad（0 = なし）。g_Hunch がある時：xyz = 腰
};

struct PartXform
{
    float4 r0;
    float4 r1;
    float4 r2;
    float4 r3;
};
StructuredBuffer<PartXform> partAnim : register(t3);

float3 XformPoint(PartXform m, float3 p)
{
    return p.x * m.r0.xyz + p.y * m.r1.xyz + p.z * m.r2.xyz + m.r3.xyz;
}
float3 XformDir(PartXform m, float3 d)
{
    return d.x * m.r0.xyz + d.y * m.r1.xyz + d.z * m.r2.xyz;
}

// クリップ c の時刻 t（秒）における隣り合う 2 フレームと、その間の
// ブレンドの重み。ループするクリップは折り返し、攻撃は末尾でクランプする
void SamplePart(uint c, float t, bool loop, out PartXform a, out PartXform b, out float w)
{
    uint frames = max(g_ClipFrames[c], 1u);
    float u = t / max(g_ClipLength[c], 1e-3);
    u = loop ? frac(u) : saturate(u);
    float f = u * (float) (loop ? frames : max(frames - 1u, 1u));
    uint f0 = min((uint) floor(f), frames - 1u);
    uint f1 = loop ? (f0 + 1u) % frames : min(f0 + 1u, frames - 1u);
    w = f - floor(f);
    a = partAnim[(g_ClipStart[c] + f0) * g_AnimPartCount + g_AnimPart];
    b = partAnim[(g_ClipStart[c] + f1) * g_AnimPartCount + g_AnimPart];
}

struct VS_INPUT_INST
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float3 Tangent : TANGENT;
    float2 UV : TEXCOORD;
    float4 Color : COLOR;
    uint InstanceID : SV_InstanceID;
};

// 歩きの揺れの調整値（main 参照）。足はローカル y = -0.9（カプセルの底）
static const float kWalkFreq = 9.0;    // 歩き時計の rad/s -> 約 1.4 歩/秒
static const float kWalkBounce = 0.06; // m
static const float kWalkRoll = 0.10;   // 頭の位置での rad

// yaw = atan2(v.x, v.z)：前方 (0,0,1) -> (sin, 0, cos)
float3 RotateY(float3 p, float s, float c)
{
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VS_OUTPUT main(VS_INPUT_INST input)
{
    VS_OUTPUT o = (VS_OUTPUT) 0;

    // InstanceID -> プールのスロット。状態チェックはデバッグ経路 / 古い一覧への
    // 保険として残してある（一覧は毎フレーム作り直される）
    uint slot = aliveList[input.InstanceID];
    if (enemyStates[slot] == SWARM_DEAD)
    {
        o.Position = float4(0, 0, -1, 1); // クリップされる
        return o;
    }

    SwarmEnemy e = enemies[slot];
    float s, c;
    sincos(e.yaw, s, c);

    float3 local = input.Position;
    float3 normal = input.Normal;
    float3 tangent = input.Tangent;
    bool moving = dot(e.velocity.xz, e.velocity.xz) > 0.01;
    float phaseOffset = (float) slot * 0.37; // 群れが足並みを揃えて動かないように
    SwarmEnemyExtra extra = enemyExtra[slot];
    float4 status = enemySlow[slot];
    bool frozen = status.z > 0.0;
    // 眠っている石像鬼は石像：一切動かない（2026-10-25）。目覚めの最初の部分は震えるだけ
    bool statue = (extra.kind == SWARM_KIND_GARGOYLE) && extra.fuse < 0.35 * g_GargoyleWakeTime;
    // ---- 雷鳴の雄鹿の突進（2026-10-26、BossAttacks からの g_BossChargePose）：0..1 溜め、1 突進中、1..1.3 命中時の
    //      頭の振り上げ、その後 1 -> 0。ポーズ > 0 になるのは四足のボスだけ ----
    float cpose = (extra.kind == SWARM_KIND_BOSS) ? g_BossChargePose : 0.0;
    float headDown = min(cpose, 1.0);                 // 頭を下げる（角を前へ）、角が光る
    float headToss = saturate((cpose - 1.0) / 0.3);   // 命中で振り上げる
    float dashK = saturate((cpose - 0.85) / 0.15);    // 突進中は 1：前傾、速い足運び
    bool bracing = (extra.kind == SWARM_KIND_BOSS) && (g_BossChargeOn > 0.5) && (g_BossChargeSpeed <= 0.0);   // 溜めで踏ん張っている
    // 面の固定エリート（2026-10-25）：CPU のフラグ（攻撃ループ、回転、狂暴）と溜めの光
    uint eliteFlags = 0u;
    float eliteGlow = 0.0;
    if (SwarmIsFixedElite(extra.kind))
    {
        int erow = SwarmEliteEntry(slot);
        if (erow >= 0)
        {
            eliteFlags = g_EliteCmd[erow].y;
            eliteGlow = (float) g_EliteCmd[erow].w / 1000.0;
        }
    }
    bool eliteAttack = (eliteFlags & SWARM_ELITE_ATTACK) != 0u;
    if ((eliteFlags & SWARM_ELITE_SPIN) != 0u)
        sincos(e.yaw + g_AnimTime * 15.0, s, c);   // 大斧の回転斬り：体ごと回る
    if (extra.kind == SWARM_KIND_BOSS && g_BossSpin != 0.0)
        sincos(e.yaw + g_BossSpin, s, c);   // 石の守護獣の尾の薙ぎ払い（2026-10-29）：CPU から 1 周分

    // ---- 石像鬼の翼（g_AnimMode 2、2026-10-25）：パーツ自身のアニメの前に、ヒンジを通る前向きの軸で回す。
    //      石像 / 着地中は垂れ、空中では羽ばたき、急降下中は上に畳む ----
    // 狼人の猫背（g_Hunch != 0、2026-10-27）：走っている間は深くなる（手足の振りの後で下で適用）
    float hunchEff = 0.0;
    if (g_Hunch != 0.0)
        hunchEff = g_Hunch * (1.0 + 0.45 * ((frozen || statue) ? 0.0 : saturate(length(e.velocity.xz) / 2.5)));
    // プロシージャルな二足歩行（狼人、2026-10-27）：どのパーツも g_Hunch（上半身）か膝（脚）を持つ。Kenney のパーツ
    // クリップでは動かさない（胴の上下と捻りで猫背の体と脚がぐらついた）。代わりに、軸足の振りで持ち上がる分だけ
    // 体全体を沈め、片足が常に地面に着くようにする（腰は足の 0.43 上）
    bool biped = (g_Hunch != 0.0) || (g_AnimMode == 4u && g_Hinge.w > 0.0);
    float bipedDrop = 0.0;
    if (biped && moving && !frozen)
    {
        float stride0 = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);
        float legAng = 0.65 * (0.5 + 0.25 * dashK) * sin(stride0 * 6.2831853 * (1.0 + 0.8 * dashK));
        bipedDrop = 0.43 * (1.0 - cos(legAng));
    }
    float3 inPos = input.Position;
    float3 inNrm = input.Normal;
    float3 inTan = input.Tangent;
    if (g_AnimMode == 2u)
    {
        float ang = -0.25;
        if (extra.kind == SWARM_KIND_BOSS)
            ang = g_BossWing;   // 石の守護獣の翼（2026-10-29）：羽ばたき / 上げる / 畳むのは CPU が決める
        else if (SwarmGargoyleAirborne(extra))
        {
            if (extra.fuse >= SWARM_GARGOYLE_DIVE)
                ang = SwarmGargoyleDiving(extra) ? 0.95 : 0.6;
            else
                ang = 0.15 + g_GargoyleFlap * sin(g_AnimTime * 9.0 + phaseOffset);
        }
        float as, ac;
        sincos(ang * g_Flap.w, as, ac);
        float2 r = inPos.xy - g_Flap.xy;
        inPos.xy = g_Flap.xy + float2(r.x * ac - r.y * as, r.x * as + r.y * ac);
        inNrm.xy = float2(inNrm.x * ac - inNrm.y * as, inNrm.x * as + inNrm.y * ac);
        inTan.xy = float2(inTan.x * ac - inTan.y * as, inTan.x * as + inTan.y * ac);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- サソリの尾（g_AnimMode 3、2026-10-26）：胴自身のアニメの前に、ヒンジ（g_Flap.xyz）を通る横軸で縦に回す。
    //      ゆっくり揺れ、近接の振りで頭越しに前へしなり（g_Flap.w rad、
    //      攻撃クリップと同じタイミング）、その後戻る ----
    if (g_AnimMode == 3u)
    {
        float ang = 0.12 * sin(g_AnimTime * 1.6 + phaseOffset);
        float sinceStab = g_AttackInterval - e.attackCooldown;
        if (e.attackCooldown > 0.0 && sinceStab < g_ClipLength.z)
        {
            float u = saturate(sinceStab / max(g_ClipLength.z, 0.05));
            ang += g_Flap.w * sin(u * 3.14159);
        }
        // 雄鹿の突進：溜めで頭を下げ、突進中も下げたまま、命中で振り上げる
        ang += g_Flap.w * (headDown - 2.2 * headToss);
        float ts, tc;
        sincos(ang, ts, tc);
        float2 r = inPos.yz - g_Flap.yz;
        inPos.yz = g_Flap.yz + float2(r.x * tc - r.y * ts, r.x * ts + r.y * tc);
        inNrm.yz = float2(inNrm.y * tc - inNrm.z * ts, inNrm.y * ts + inNrm.z * tc);
        inTan.yz = float2(inTan.y * tc - inTan.z * ts, inTan.y * ts + inTan.z * tc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- 四足の脚（g_AnimMode 4、2026-10-26、雄鹿）：自分の付け根（g_Flap.xyz）を通る横軸で、歩き時計に合わせて
    //      振る。位相 g_Flap.w（左前 + 右後が 0、もう一対が pi）。立っている間は
    //      まっすぐ。胴自身のアニメ（上下動）はこの後に掛かる ----
    if (g_AnimMode == 4u)
    {
        float ang = 0.0;
        float bend = 0.0;   // 狼人の膝（g_Hinge.w > 0、2026-10-27）：脚を前へ振る（持ち上げる）間に折れる
        if (moving && !frozen)
        {
            float stride = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);   // 歩きのループ数 = 歩数
            // 突進（dashK）：脚を速く大きく振る（駆け足）
            float ph = stride * 6.2831853 * (1.0 + 0.8 * dashK) + g_Flap.w;
            ang = (0.5 + 0.25 * dashK) * sin(ph);
            if (g_Hinge.w > 0.0)
                ang *= 0.65;   // 二足の脚（狼人）：四足より歩幅を短く
            bend = g_Hinge.w * max(0.0, -cos(ph));
        }
        else if (bracing)
        {
            // 溜め：前脚を前へ、後脚を後ろへ（踏ん張り、g_Flap.z > 0 = 前脚）
            ang = 0.22 * headDown * ((g_Flap.z > 0.0) ? -1.0 : 1.0);
        }
        // 猫背の二足の腕（g_Hunch != 0、狼人）：肩を軸に猫背の半分だけ戻し、手が膝の横に
        // 垂れるようにする（全量戻すと手がずっと前方にぶら下がった、2026-10-27）。歩きの
        // 振りは脚の 0.6（0.3 だと左右の腕が同じに見えた）。近接の振りはクリップの前半で左爪、
        // 後半で右爪を出す（同時だと片腕に見えた）
        if (g_Hunch != 0.0)
            ang = ang * 0.6 - 0.5 * hunchEff;
        if (g_Hunch != 0.0 && e.attackCooldown > 0.0)
        {
            float sinceSwing = g_AttackInterval - e.attackCooldown;
            if (sinceSwing < g_ClipLength.z)
            {
                float u = saturate(sinceSwing / max(g_ClipLength.z, 0.05)) * 2.0 - ((g_Flap.x < 0.0) ? 0.0 : 1.0);
                ang -= 1.6 * sin(saturate(u) * 3.14159);
            }
        }
        if (bend > 0.0 && g_Hinge.w > 0.0 && inPos.y < g_Hinge.y)
        {
            // 膝より下：脛を膝を軸に後ろへ振る（股関節と同じ向き）
            float ks, kc;
            sincos(bend, ks, kc);
            float2 rk = inPos.yz - g_Hinge.yz;
            inPos.yz = g_Hinge.yz + float2(rk.x * kc - rk.y * ks, rk.x * ks + rk.y * kc);
            inNrm.yz = float2(inNrm.y * kc - inNrm.z * ks, inNrm.y * ks + inNrm.z * kc);
            inTan.yz = float2(inTan.y * kc - inTan.z * ks, inTan.y * ks + inTan.z * kc);
        }
        float ls, lc;
        sincos(ang, ls, lc);
        float2 r = inPos.yz - g_Flap.yz;
        inPos.yz = g_Flap.yz + float2(r.x * lc - r.y * ls, r.x * ls + r.y * lc);
        inNrm.yz = float2(inNrm.y * lc - inNrm.z * ls, inNrm.y * ls + inNrm.z * lc);
        inTan.yz = float2(inTan.y * lc - inTan.z * ls, inTan.y * ls + inTan.z * lc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- 虫の脚（g_AnimMode 5、2026-10-26、サソリ）：自分の付け根（g_Flap.xyz）を通る「縦」軸で、歩き時計に合わせて
    //      前後に掃く。位相 g_Flap.w（三脚歩行：左右それぞれ 1 本おき）。前へ振る間（戻りのストローク）は
    //      少し持ち上げる。+x 側の脚は負の角度で前へ掃く ----
    if (g_AnimMode == 5u)
    {
        float ang = 0.0;
        float lift = 0.0;
        if (moving && !frozen)
        {
            float stride = e.animTime * g_WalkRate / max(g_ClipLength.y, 0.05);
            float ph = stride * 6.2831853 * (1.0 + 0.8 * dashK) + g_Flap.w;
            ang = 0.38 * sin(ph) * ((g_Flap.x < 0.0) ? 1.0 : -1.0);
            lift = 0.10 * max(0.0, cos(ph));
        }
        float ls, lc;
        sincos(ang, ls, lc);
        float2 r = inPos.xz - g_Flap.xz;
        inPos.xz = g_Flap.xz + float2(r.x * lc + r.y * ls, -r.x * ls + r.y * lc);
        inPos.y += lift * saturate(abs(r.x) / 0.75);
        inNrm.xz = float2(inNrm.x * lc + inNrm.z * ls, -inNrm.x * ls + inNrm.z * lc);
        inTan.xz = float2(inTan.x * lc + inTan.z * ls, -inTan.x * ls + inTan.z * lc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    // ---- 浮遊する魔神のパーツ（g_AnimMode 6、2026-10-29）：パーツクリップなし（歩かず、砂の渦の上に
    //      浮いている）。g_Flap.w != 0 = 腕：肩（g_Flap.xyz）を通る横軸で縦に回す：
    //      少し揺れ（肘のある腕は揺れない）、詠唱で g_Flap.w rad 上げ（g_BossChargePose 0..1）、叩きつけでその 0.25 まで
    //      振り下ろす = 前下方（pose 1..1.3）、接触攻撃では前へ振る（攻撃クリップのタイミング） ----
    if (g_AnimMode == 6u && g_Flap.w != 0.0)
    {
        // 肘のある腕（g_Hinge.w != 0、骸の王、2026-10-29）は地面に着いている：待機の揺れなし
        // （揺らすとまっすぐな腕がずっと手を振っているように見えた）
        float ang = (g_Hinge.w != 0.0) ? 0.0 : 0.10 * sin(g_AnimTime * 1.3 + phaseOffset + g_Flap.x * 1.7);
        float lift = headDown - 0.75 * headToss;
        ang += g_Flap.w * lift;
        float sinceSwing = g_AttackInterval - e.attackCooldown;
        float swing = 0.0;
        if (e.attackCooldown > 0.0 && sinceSwing < g_ClipLength.z)
            swing = sin(saturate(sinceSwing / max(g_ClipLength.z, 0.05)) * 3.14159);
        ang += 0.5 * g_Flap.w * swing;
        // ---- 肘（2026-10-29、ユーザー：「腕を上げる時は肘を曲げて、まっすぐ振るのは気持ち悪い」）：前腕
        //      （上腕に直交し g_Hinge.xyz を通る面より先）を、腕が上がるにつれて（接触攻撃の振りでも）肘を通る横軸で
        //      g_Hinge.w rad 折る。肩で腕全体を回す前に行う ----
        if (g_Hinge.w != 0.0)
        {
            float3 upper = normalize(g_Hinge.xyz - g_Flap.xyz);
            if (dot(inPos - g_Hinge.xyz, upper) > 0.0)
            {
                float bend = g_Hinge.w * saturate(max(lift, swing));
                float es, ec;
                sincos(bend, es, ec);
                float2 re = inPos.yz - g_Hinge.yz;
                inPos.yz = g_Hinge.yz + float2(re.x * ec - re.y * es, re.x * es + re.y * ec);
                inNrm.yz = float2(inNrm.y * ec - inNrm.z * es, inNrm.y * es + inNrm.z * ec);
                inTan.yz = float2(inTan.y * ec - inTan.z * es, inTan.y * es + inTan.z * ec);
            }
        }
        float as, ac;
        sincos(ang, as, ac);
        float2 r = inPos.yz - g_Flap.yz;
        inPos.yz = g_Flap.yz + float2(r.x * ac - r.y * as, r.x * as + r.y * ac);
        inNrm.yz = float2(inNrm.y * ac - inNrm.z * as, inNrm.y * as + inNrm.z * ac);
        inTan.yz = float2(inTan.y * ac - inTan.z * as, inTan.y * as + inTan.z * ac);
    }

    // ---- 狼人の猫背（g_Hunch != 0、2026-10-27）：パーツ（胴 / 頭 / 腕、上で振り済み）を、パーツ自身の動きの前に
    //      腰（g_Hinge.yz）を軸に前へ倒す。走っている間は深くなる（速度でなめらかに変わる） ----
    if (g_Hunch != 0.0)
    {
        float hs, hc;
        sincos(hunchEff, hs, hc);
        float2 rh = inPos.yz - g_Hinge.yz;
        inPos.yz = g_Hinge.yz + float2(rh.x * hc - rh.y * hs, rh.x * hs + rh.y * hc);
        inNrm.yz = float2(inNrm.y * hc - inNrm.z * hs, inNrm.y * hs + inNrm.z * hc);
        inTan.yz = float2(inTan.y * hc - inTan.z * hs, inTan.y * hs + inTan.z * hc);
        local = inPos;
        normal = inNrm;
        tangent = inTan;
    }

    if (g_AnimMode == 1u)
    {
        // ---- 体の周りを回るボスの飾り（砂の守護者の浮遊岩、2026-10-24）：輪全体を縦軸の周りに
        //      回し、少し上下させる。体と一緒には凍らない：手足ではなく魔法だから ----
        float a = g_AnimTime * g_OrbitSpeed;
        float os, oc;
        sincos(a, os, oc);
        local = float3(input.Position.x * oc + input.Position.z * os, input.Position.y + 0.06 * sin(g_AnimTime * 1.7),
                       -input.Position.x * os + input.Position.z * oc);
        normal = float3(input.Normal.x * oc + input.Normal.z * os, input.Normal.y, -input.Normal.x * os + input.Normal.z * oc);
        tangent = float3(input.Tangent.x * oc + input.Tangent.z * os, input.Tangent.y, -input.Tangent.x * os + input.Tangent.z * oc);
    }
    else if (biped)
    {
        // プロシージャルな二足歩行（上記）：パーツクリップなし、歩幅に合わせて体が沈み、上半身が少し呼吸する
        local = inPos;
        normal = inNrm;
        tangent = inTan;
        local.y -= bipedDrop;
        if (g_Hunch != 0.0)
            local.y += 0.006 * sin(g_AnimTime * 2.2 + phaseOffset);
    }
    else if (g_AnimMode == 6u)
    {
        // 浮遊する魔神（上記）：パーツクリップなし、上半身全体がゆっくり呼吸する
        local = inPos;
        normal = inNrm;
        tangent = inTan;
        local.y += 0.012 * sin(g_AnimTime * 1.6 + phaseOffset);
    }
    else if (g_AnimEnabled != 0u)
    {
        // ---- 剛体パーツアニメ：敵の状態からクリップを選ぶ ----
        //   attack : ContactCS がプレイヤーに当てた直後（attackCooldown が
        //            g_AttackInterval に戻ってカウントダウンする）。なので振りは
        //            ダメージの tick で始まり、1 回だけ再生する
        //   walk   : 移動中。animTime は MoveCS が進める敵ごとの時計
        //   idle   : 立っている（塞がれている、またはプレイヤーの前で次の攻撃待ち）
        //   被弾硬直（animIndex 2）と凍結は歩きの 1 フレームで止める。下のフラッシュは動く
        uint clip = 0u;
        float t = g_AnimTime + phaseOffset;
        bool loop = true;
        float sinceAttack = g_AttackInterval - e.attackCooldown;
        if (statue)
        {
            clip = 0u;
            t = 0.0;
        }
        else if (frozen)
        {
            clip = 1u;
            t = phaseOffset;
        }
        else if (eliteAttack)
        {
            // 固定エリートの技：振りを何度も繰り返す
            clip = 2u;
            t = fmod(g_AnimTime * 1.3 + phaseOffset, max(g_ClipLength.z, 0.05));
            loop = false;
        }
        else if (e.attackCooldown > 0.0 && sinceAttack < g_ClipLength.z)
        {
            clip = 2u;
            t = sinceAttack;
            loop = false;
        }
        else if (e.animIndex == 2u)
        {
            clip = 1u;
            t = phaseOffset;
        }
        else if (moving)
        {
            clip = 1u;
            t = e.animTime * g_WalkRate + phaseOffset;
        }

        PartXform fa, fb;
        float w;
        SamplePart(clip, t, loop, fa, fb, w);
        local = lerp(XformPoint(fa, inPos), XformPoint(fb, inPos), w);
        normal = normalize(lerp(XformDir(fa, inNrm), XformDir(fb, inNrm), w));
        tangent = lerp(XformDir(fa, inTan), XformDir(fb, inTan), w);
    }
    else if (e.animIndex != 2u && moving && !frozen)
    {
        // ---- プロシージャルな歩き：メッシュは焼いた 1 フレームなので足運びをそれらしく見せる ----
        // animTime は敵ごとの歩き時計（MoveCS が移動中に進め、
        // 被弾硬直中は止める）。一歩ごとに跳ね、左右に揺れる。
        // 頭（ローカル y）に近いほど強く揺れる
        float phase = e.animTime * kWalkFreq + phaseOffset;
        float bounce = abs(sin(phase)) * kWalkBounce;
        float roll = sin(phase) * kWalkRoll * saturate(local.y + 0.9); // 足元では 0
        float rs, rc;
        sincos(roll, rs, rc);
        local.xy = float2(local.x * rc - local.y * rs, local.x * rs + local.y * rc);
        local.y += bounce;
    }

    // ---- 雷鳴の雄鹿の突進（2026-10-26）：体全体（飾りもすべてここを通る）が溜めで少し屈み、
    //      突進中は腰を軸に前傾する。腰 = 焼いたメッシュ空間で (y 0.5, z -0.6)
    //      （雄鹿の 1.15 倍スケール後の後脚のヒンジ） ----
    if (cpose > 0.0)
    {
        // 狼人の飛びかかり：高さ g_BossJumpHeight の放物線（ワールドの m。種類のスケールは下で掛かるので割っておく）
        if (g_BossJumpHeight > 0.0)
        {
            float p = min(cpose, 1.0);
            local.y += g_BossJumpHeight * 4.0 * p * (1.0 - p) / max(SwarmKindScale(extra.kind), 1.0);
        }
        local.y -= 0.08 * headDown * (1.0 - dashK);
        // 支点と角度はボスごとに CPU から来る（g_BossLeanPivot* / g_BossLeanAngle：雄鹿の腰 (0.5, -0.6) 0.17、
        // サソリの低い胴 (-0.6, 0) 0.08。0 = 前傾なし）
        float lean = g_BossLeanAngle * dashK;
        float ps, pc;
        sincos(lean, ps, pc);
        float2 hips = float2(g_BossLeanPivotY, g_BossLeanPivotZ);
        float2 r = local.yz - hips;
        local.yz = hips + float2(r.x * pc - r.y * ps, r.x * ps + r.y * pc);
        normal.yz = float2(normal.y * pc - normal.z * ps, normal.y * ps + normal.z * pc);
        tangent.yz = float2(tangent.y * pc - tangent.z * ps, tangent.y * ps + tangent.z * pc);
    }

    // ---- エリート：体を大きく、足元を基点に（SwarmKindCapsule と一致） ----
    float kindScale = SwarmKindScale(extra.kind);
    if (kindScale != 1.0)
    {
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        local.xz *= kindScale;
        local.y = footY + (local.y - footY) * kindScale;
    }

    // ---- 固定エリート：狂暴時は大きく、消える時は地面に縮んで潜る（影の剣士、2026-10-25） ----
    if (SwarmIsFixedElite(extra.kind))
    {
        float hide = saturate(-extra.fuse);
        float grow = (((eliteFlags & SWARM_ELITE_ENRAGED) != 0u) ? g_EliteEnragedScale : 1.0) * (1.0 - hide);
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        local.xz *= grow;
        local.y = footY + (local.y - footY) * grow;
    }

    // ---- 点火した自爆兵：爆発に向けて膨らむ、足元を基点に ----
    bool lit = SwarmInvulnerable(extra);   // 点火した自爆兵 / 突撃爆弾兵（2026-10-18）
    float fuseT = (extra.kind == SWARM_KIND_DASHBOMBER) ? g_DashBomberWindup + g_DashBomberFuse : g_BomberFuseTime;
    float fuseU = lit ? saturate(extra.fuse / max(fuseT, 1e-3)) : 0.0;
    if (lit)
    {
        float footY = -(g_EnemyRadius + g_EnemyCapsuleHalf);
        float grow = 1.0 + g_BomberSwell * fuseU * fuseU;
        local.xz *= grow;
        local.y = footY + (local.y - footY) * grow;
    }

    // ---- 砂潜り：潜っている間は体が砂の下、浮上する時にせり上がり、また沈む（2026-10-17） ----
    if (extra.kind == SWARM_KIND_BURROWER)
    {
        float down = (extra.fuse > 0.0) ? 1.0 - saturate((g_BurrowSurfaceTime - extra.fuse) / SWARM_BURROW_RISE_TIME)
                                        : saturate(-extra.fuse / SWARM_BURROW_RISE_TIME);
        float bodyH = (g_EnemyRadius + g_EnemyCapsuleHalf) * 2.0 * kindScale + 0.6;
        local.y -= bodyH * down;
    }
    // ---- 砂の守護者の潜砂（2026-10-24）：fuse = -burrow 0..1（AICS が g_BossBurrow から書く）、体は同じように沈む ----
    if (extra.kind == SWARM_KIND_BOSS && extra.fuse < 0.0)
    {
        float bodyH = (g_EnemyRadius + g_EnemyCapsuleHalf) * 2.0 * kindScale + 0.6;
        local.y -= bodyH * saturate(-extra.fuse);
    }

    float3 worldPos = RotateY(local, s, c) + e.position;
    if (g_IceOn > 0.5)
        worldPos.y += IceLift(e.position.xz, e.position.y - g_GroundY);
    // 浮いているボスはゆっくり上下する（高さ g_BossHover は位置に入っている：MoveCS）
    if (extra.kind == SWARM_KIND_BOSS && g_BossHover > 0.0)
        worldPos.y += 0.18 * saturate(g_BossHover) * sin(g_AnimTime * 1.3);
    // 幽霊：浮いて上下する（ゲーム上の位置は地面のまま）
    if (SwarmIsGhost(extra.kind))
        worldPos.y += g_GhostHover + 0.15 * sin(g_AnimTime * 3.0 + (float) slot * 0.7);
    // 目覚める石像鬼は石を振り落とすように震える（2026-10-25）
    if (extra.kind == SWARM_KIND_GARGOYLE && extra.fuse > 0.0 && extra.fuse < 0.35 * g_GargoyleWakeTime)
    {
        float k = 0.06 * (1.0 - extra.fuse / (0.35 * g_GargoyleWakeTime + 1e-3));
        worldPos.x += k * sin(g_AnimTime * 61.0 + (float) slot);
        worldPos.z += k * sin(g_AnimTime * 53.0 + (float) slot * 1.7);
    }
    o.WorldPos = worldPos;
    o.Position = mul(mul(float4(worldPos, 1.0), View), Projection);
    o.Normal = RotateY(normal, s, c);
    o.Tangent = RotateY(tangent, s, c);
    o.UV = input.UV;
    o.Color = input.Color;
    if (extra.kind == SWARM_KIND_ELITE)
        o.Color.rgb *= kEliteTint;
    else if (extra.kind == SWARM_KIND_BOSS)
    {
        o.Color.rgb *= g_BossTint;
        // 雷鳴の雄鹿：光る部分（HDR の頂点色 = 角の縁と目）が溜めと突進の間に
        // 強く光る（2026-10-26）
        float glowy = max(max(input.Color.r, input.Color.g), input.Color.b);
        if (glowy > 1.2)
            o.Color.rgb *= 1.0 + 1.0 * headDown;
    }
    else if (extra.kind == SWARM_KIND_GHOST)
    {
        o.Color.rgb *= kGhostTint * g_GhostGlow;
        o.Color.a = g_GhostAlpha;
    }
    else if (extra.kind == SWARM_KIND_DARKGHOST)
    {
        float pulse = 0.75 + 0.25 * sin(g_AnimTime * 2.2 + (float) slot * 1.3);
        o.Color.rgb *= kDarkGhostTint * pulse;
        o.Color.a = g_DarkGhostAlpha;
    }

    // ---- 被弾フラッシュ：硬直中は頂点色を明るくしすぎ、1 へ減衰させる ----
    // PS がアルベドに Color を掛けるので、> 1 は HDR になりブルームが拾う
    if (e.animIndex == 2u)
    {
        float t = saturate(e.animTime / max(g_HitStun, 1e-4));
        o.Color.rgb *= lerp(g_HitFlash, 1.0, t);
    }

    // ---- 点火した自爆兵：赤熱の点滅。点滅の速さは開始から終了まで直線的に
    // 上がるので、位相は導火線の時間でそれを積分したもの ----
    if (lit)
    {
        float T = max(fuseT, 1e-3);
        float f = extra.fuse;
        float phase = kFuseBlinkStart * f + 0.5 * (kFuseBlinkEnd - kFuseBlinkStart) / T * f * f;
        float on = (frac(phase) < 0.5) ? 1.0 : 0.0;
        float g = g_BomberFlashGain;
        o.Color.rgb *= lerp(float3(1, 1, 1), float3(g, g * 0.3, g * 0.2), on);
    }

    // ---- 突撃兵：溜めの間は赤く点滅（突進に近づくほど速く）、突進中は赤みがかる ----
    if (extra.kind == SWARM_KIND_CHARGER)
    {
        uint ph = SwarmChargerPhase(extra.fuse);
        if (ph == SWARM_CHARGE_WINDUP)
        {
            float T = max(g_ChargerWindup, 1e-3);
            float f = extra.fuse;
            float phase = kChargeBlinkStart * f + 0.5 * (kChargeBlinkEnd - kChargeBlinkStart) / T * f * f;
            float on = (frac(phase) < 0.5) ? 1.0 : 0.35;
            float g = g_ChargerGlow;
            o.Color.rgb *= lerp(float3(1, 1, 1), float3(g, g * 0.25, g * 0.2), on);
        }
        else if (ph == SWARM_CHARGE_DASH)
            o.Color.rgb *= float3(1.8, 0.6, 0.5);
    }

    // ---- 砂嵐の術師：溜めの間に砂色の光が強まり、砂旋が出る直前に最後に光る（2026-10-17） ----
    if (extra.kind == SWARM_KIND_SANDMAGE && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_SandMageWindup, 1e-3));
        float pulse = 0.5 + 0.5 * sin(extra.fuse * (12.0 + 30.0 * u));
        float g = 1.0 + (0.6 + 1.6 * u) * pulse;
        o.Color.rgb *= float3(g, g * 0.85, 1.0 + (g - 1.0) * 0.3);
    }

    // ---- 遺跡の敵（2026-10-25） ----
    // 眠っている石像鬼：くすんだ灰色の石像（光る目も消える）、目覚めると色が戻る
    if (statue)
    {
        float l = dot(o.Color.rgb, float3(0.3, 0.59, 0.11));
        o.Color.rgb = min(lerp(o.Color.rgb, l.xxx, 0.8), 0.7);
    }
    // 溜め中の死霊術師：詠唱に向けて病的な緑の光が強まる
    if (extra.kind == SWARM_KIND_NECRO && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_NecroWindup, 1e-3));
        float pulse = 0.5 + 0.5 * sin(extra.fuse * (8.0 + 22.0 * u));
        float g = 1.0 + (0.5 + 1.5 * u) * pulse;
        o.Color.rgb *= float3(1.0 + (g - 1.0) * 0.45, g, 1.0 + (g - 1.0) * 0.6);
    }
    // 投げの溜め中の鎖の番人：橙色の光（地面の帯と一緒に）
    if (extra.kind == SWARM_KIND_WARDEN && extra.fuse > 0.0)
    {
        float u = saturate(extra.fuse / max(g_WardenWindup, 1e-3));
        float g = 1.0 + 1.6 * u * (0.6 + 0.4 * sin(extra.fuse * 30.0));
        o.Color.rgb *= float3(g, 1.0 + (g - 1.0) * 0.45, 1.0 + (g - 1.0) * 0.1);
    }
    // 蘇った呪いの鎧：ゆっくりした紫の脈動（一度蘇った印）
    if (extra.kind == SWARM_KIND_ARMOR2)
        o.Color.rgb *= float3(1.15, 0.8, 1.45) * (0.9 + 0.25 * sin(g_AnimTime * 3.0 + (float) slot));
    // 固定エリート（2026-10-25）：狂暴 = 赤い脈動、技の溜め = 命中に向けて明るくなる熱いちらつき
    if ((eliteFlags & SWARM_ELITE_ENRAGED) != 0u)
        o.Color.rgb *= float3(1.55, 0.6, 0.5) * (0.9 + 0.2 * sin(g_AnimTime * 7.0 + (float) slot));
    if (eliteGlow > 0.0)
    {
        float g = 1.0 + eliteGlow * (1.3 + 0.7 * sin(g_AnimTime * 28.0));
        o.Color.rgb *= float3(g, 1.0 + (g - 1.0) * 0.55, 1.0 + (g - 1.0) * 0.35);
    }

    // ---- 凍結（アイスランス）：淡い青の体 ----
    if (frozen)
        o.Color.rgb *= lerp(float3(1, 1, 1), kIceTint, saturate(g_FreezeTint));
    // ---- 毒（毒素がまだ削っている）：病的な緑、溜まった毒素が多いほど強い ----
    float toxin = enemyPoison[slot].x;
    if (toxin > 0.0)
        o.Color.rgb *= lerp(float3(1, 1, 1), kPoisonTint, saturate(toxin * 0.5) * 0.85);
    return o;
}