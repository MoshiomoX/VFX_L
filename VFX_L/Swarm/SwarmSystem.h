// ============================================================
// SwarmSystem.h
// 雑魚・投射物・経験値オーブを GPU 上で回すシステム。
//
// GPUParticleSystem と同じ形をしている:
//   CPU は「生成したい物」を溜め、Flush で一度だけ GPU へ流す。
//   位置や HP の真実は GPU 上にしか無く、CPU は読み戻さない。
//   例外は counter だけ（16 バイト、ring buffer で非同期にリードバック）。
//
// 粒子との違いは2つ:
//   1. 固定ステップを内部で回す（gameplay の結果が dt に依存しないように）
//   2. 少量のリードバックチャンネルを持つ（湧き制御とプレイヤーの被弾に要る）
//
// 弾は「核」でしかない（位置 + 判定）。見た目は全部粒子。
//   SwarmEmitCS が弾の位置から粒子を発射し、粒子システムが描く。
//   弾自身の描画経路は持たない。
//
// 対象外:
//   プレイヤーとエリート（EliteTag）は CPU の Registry に残る。
//   異種で少数、操作感とデバッグが要る物は GPU に載せない。
//   接点は「プレイヤーの位置を毎フレーム上げる」だけ。
// ============================================================
#pragma once
#include "Swarm/SwarmTypes.h"
#include "Swarm/GPUReadback.h"
#include "Swarm/SwarmVFXTable.h"
#include "Swarm/SwarmFireEmit.h"
#include "Swarm/FlowField.h"
#include "VFX_Editor/VFXId.h"
#include "Graphics/Light/LightTypes.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <future>
#include <memory>
#include <vector>
#include <array>

class Model;
class Material;
class Texture;
class ComputeShader;
class GPUParticleSystem;
class GridWorld;
class VertexShader;
class PixelShader;
class CameraBase;
class SwarmSystem
{
public:
    // 粒子システムを借りる（発射先）。Initialize の前に呼ぶ
    void SetParticleSystem(GPUParticleSystem* ps) { m_Particles = ps; }

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Render(CameraBase* camera, const LightBuffer& light);   // = RenderOpaque + RenderOverlay
    // 2026-10-04 トゥーンのアウトラインを間に挟むため 2 つに分けた：
    //   RenderOpaque  = 雑魚・砕け散り・経験値オーブ（深度を書く。点光源を PS に bind したまま返る）
    //   RenderOverlay = 液面・足元の影・警告の輪・HP バー（深度は読むだけ。最後に点光源を外す）
    void RenderOpaque(CameraBase* camera, const LightBuffer& light);
    void RenderOverlay(CameraBase* camera, const LightBuffer& light);
    void Shutdown();

    // 地形は変わらないので起動時に1回だけ上げる。
    // Regenerate した時はもう一度呼ぶこと
    void UploadTerrain(const GridWorld& grid);
    // 通行マップだけを上げ直してフローフィールドを作り直させる（置物の下を塞ぐ / 開けた箱の下を戻す時）。
    // 寸法は変わらない前提（空間ハッシュ・高さ場・向き表の buffer はそのまま）
    void RefreshWalkable(const GridWorld& grid);

    // VFXDatabase からレシピ表を作る。ItemDatabase / VFXDatabase の後に1回
    bool BuildVFXTable();

    void RenderDebug(CameraBase* camera);

    // GPU の範囲（弾の命中）が出した連番画像（Sprite entry）。不透明物の後・粒子の前に呼ぶ
    void RenderSprites(CameraBase* camera);
    // GPU の弾が引く帯（VFX の Trail entry。2026-10-15）。RenderSprites の直後に呼ぶ（加算、深度は読むだけ）
    void RenderProjTrails(CameraBase* camera);

    // ---- 雑魚の頭上の HP バー（Render の最後、雑魚とオーブの後に描く）----
    // 大きさは世界の寸法（遠いほど小さい。モデルと同じ比率）。全員に常に出す
    struct HpBarStyle
    {
        bool  enabled = true;
        float width = 0.9f;       // m
        float height = 0.1f;      // m
        float offset = 1.0f;      // 位置（カプセルの中心）から上へ m。雑魚は高さ 1.6m（頭頂 +0.7）
        float border = 0.015f;    // 縁の太さ m
        DirectX::SimpleMath::Vector4 fill = { 0.85f, 0.20f, 0.20f, 1.0f };   // HUD の HP と同じ赤
        DirectX::SimpleMath::Vector4 back = { 0.08f, 0.08f, 0.10f, 0.75f };  // 減った分
        DirectX::SimpleMath::Vector4 edge = { 0.0f, 0.0f, 0.0f, 0.9f };
    };
    HpBarStyle hpBar;

    // ---- 雑魚の足元の丸い影（Render の中、雑魚とオーブの後・警告の輪の前）----
    // 雑魚は太陽のシャドウマップ（ShadowMap）に入れないので、代わりに地面を丸く暗くする。
    // 板の四隅はそれぞれ高さ場に載せる（高台の坂でも地面に沿う）
    struct BlobShadowStyle
    {
        bool  enabled = true;
        float radius = 0.65f;     // m（体の幅 0.5m より少し大きく。真下は体に隠れる）
        float strength = 0.6f;    // 中心の暗さ 0..1
        float softness = 0.6f;    // 半径のうち外側の何割でぼかすか
        float lift = 0.03f;       // 地面から浮かせる m（Z ファイト避け）
        float clamp = 0.3f;       // 四隅の高さを足元から何 m までに抑えるか（崖の縁で板が伸びない）
    };
    BlobShadowStyle blobShadow;

    // ---- 点火した自爆兵の足元の警告の輪（Render の中、雑魚とオーブの後・HP バーの前）----
    // 外周 = 爆発半径（BomberCB.blastRadius）、中の円盤が導火線に合わせて中心から育ち、外周に届くと爆発。
    // 色は straight alpha（シェーダーが premultiply する）
    struct BomberRingStyle
    {
        bool  enabled = true;
        float edgeWidth = 0.08f;  // 外周の太さ m
        float lift = 0.03f;       // 地面から浮かせる m（Z ファイト避け）
        DirectX::SimpleMath::Vector4 fill = { 0.90f, 0.12f, 0.08f, 0.35f };  // 育つ円盤
        DirectX::SimpleMath::Vector4 edge = { 0.95f, 0.15f, 0.10f, 0.85f };  // 外周
        DirectX::SimpleMath::Vector4 back = { 0.90f, 0.12f, 0.08f, 0.10f };  // 外周の内側でまだ育っていない所
    };
    BomberRingStyle bomberRing;

    // ---- 隕石（DROP の弾）が落ちる所の警告の輪（自爆兵の輪の後に描く）----
    // 形と動きは自爆兵の輪と同じ（PS も共用）。外周 = 弾の hitArea の半径、
    // 中の円盤が落下（pathT）に合わせて育ち、外周に届くと着弾。
    // 色だけ橙黄: 赤は「敵の危険」、こちらは自分の技
    BomberRingStyle dropRing = { true, 0.08f, 0.03f,
        { 1.00f, 0.60f, 0.10f, 0.35f },    // 育つ円盤
        { 1.00f, 0.78f, 0.25f, 0.85f },    // 外周
        { 1.00f, 0.60f, 0.10f, 0.10f } };  // 外周の内側でまだ育っていない所

    // ---- Boss のスラムの警告の輪（SetWarnCircles。2026-10-03）----
    // 形は同じ。色は赤（ユーザー 10-03「もう少し赤く」：最初は紫を混ぜてピンクに見えた）、自爆兵の輪より濃く外周を太く
    BomberRingStyle warnRing = { true, 0.14f, 0.04f,
        { 0.95f, 0.08f, 0.05f, 0.45f },    // 育つ円盤
        { 1.00f, 0.15f, 0.08f, 0.95f },    // 外周
        { 0.85f, 0.06f, 0.04f, 0.14f } };  // 外周の内側でまだ育っていない所

    // ---- 突撃兵の予告の帯（溜めの間、足元から突進の向きへ。2026-10-08）----
    // 長さ = 突進の距離 + 体の半径、幅 = 体の 2.8 倍。中の塗りが溜めに合わせて先端へ伸び、届くと突進。
    // edgeWidth = 両脇の縁の太さ m。PS は SwarmChargeLinePS（「>」の矢羽が突進の向きを指す）
    BomberRingStyle chargeLine = { true, 0.07f, 0.04f,
        { 1.00f, 0.22f, 0.06f, 0.42f },    // 伸びる塗り
        { 1.00f, 0.35f, 0.10f, 0.90f },    // 縁と矢羽
        { 0.90f, 0.12f, 0.05f, 0.14f } };  // まだ塗られていない所

    // ---- 盾兵の盾（特効モデル dun01.FBX を身体の前に。2026-10-08）----
    struct ShieldLookStyle
    {
        bool  enabled = true;
        float height = 1.15f;     // 盾の高さ m（体格 1 の時）
        DirectX::SimpleMath::Vector3 hold = { -0.08f, 0.80f, 0.52f };   // 盾の中心: 右 / 足元から上 / 前 m
        float yawDeg = 0.0f;      // モデルの向きを回す（飾りの面を前へ）
        float bob = 0.04f;        // 歩く時の上下 m
        DirectX::SimpleMath::Vector4 tint = { 0.42f, 0.48f, 0.60f, 1.0f };   // 鋼の青灰（linear）
    };
    ShieldLookStyle shieldLook;

    // ---- トゲ玉の体（コードで作った球 + トゲ。SwarmSpikeBallVS。2026-10-17）----
    struct SpikeBallLookStyle
    {
        bool  enabled = true;
        float radius = 0.6f;      // 体格 1 の時の球の半径 m（トゲは別に +45%）。中心 = 足元 + これ
        float rollRate = 1.0f;    // 転がる速さの倍率（1 = 進んだ距離どおり）
        DirectX::SimpleMath::Vector4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };   // 頂点色（砂色の球 + 褐色のトゲ）に掛ける
    };
    SpikeBallLookStyle spikeBallLook;

    // ---- 砂潜りの砂の山（コードで作った球を潰した物。SwarmMoundVS。2026-10-17）----
    struct MoundLookStyle
    {
        bool  enabled = true;
        float radius = 1.0f;      // 体格 1 の時の半径 m（草原の草に埋もれないよう大きめ）
        float height = 0.45f;     // 盛り上がりの高さ m
        float bob = 0.05f;        // 進んでいる時の上下 m
        DirectX::SimpleMath::Vector4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };   // 頂点色（砂色）に掛ける
    };
    MoundLookStyle moundLook;

    // ---- 砂嵐の術師の砂旋の見た目（2026-10-17。10-20 筒のモデルを廃止：ユーザー「筒は要らない、空間の歪み + 薄い白で足りる」）----
    // 砂旋そのものは描かない。見えるのは足元の砂埃（SwarmTornadoEmitCS が TornadoDust.json を出す：屈折 + 薄い白の煙団と砂粒）と、
    // 風源の表（Swarm::WindSource）に書いた風で草 / 落ち葉 / 粒子が巻かれる所だけ（Swarm/SwarmSystemTornado.cpp）
    struct TornadoLookStyle
    {
        float growTime = 0.3f;    // 生まれて風が強まる / 消える前に弱まる秒（風源の fade）
        bool  dust = true;        // 足元の砂埃（SwarmTornadoEmitCS、TornadoDust.json）
        float dustRate = 1.0f;    // 配方の発射率に掛ける
        float dustSwirl = 6.0f;   // 砂埃に足す軸回りの速さ m/s（砂と煙団が巻き上がって見える）
        // 風源（2026-10-19 ユーザー：「風そのものが風の係数を持って世界の風に影響する」）。砂旋 1 本 = Swarm::WindSource 1 つ：
        // 届く距離 = 当たりの半径 × windRadiusMul、縁の接線の速さ windSwirl、芯の上昇 windUpdraft、縁の吸い込み windInflow。
        // 草が輪になってなびき、フィールドの落ち葉 / 砂屑が巻き上がり、煙が周りを回る（GrassVS / DebrisUpdateCS / ParticleUpdateCS）。
        // 10-20 筒を無くしたので風源を強めた（8 / 10 / 3 / 1.2 → 11 / 13 / 4.5 / 1.8）：見えるのは環境の反応だけ
        bool  windSource = true;
        float windRadiusMul = 11.0f;  // 当たりの半径 0.48m × 11 ≒ 5.3m
        float windSwirl = 13.0f;      // m/s
        float windUpdraft = 4.5f;     // m/s（落ち葉の終端の落下 2.5 m/s より速い = 芯の近くは浮く）
        float windInflow = 1.8f;      // m/s
    };
    TornadoLookStyle tornadoLook;

    // ---- 凍った敵の氷（特効モデル bingci_02.FBX を足元に前後 2 つ。2026-10-08）----
    struct IceLookStyle
    {
        bool  enabled = true;
        float height = 1.25f;     // 氷の塊の高さ m（体格 1 の時）
        float spread = 0.22f;     // 体の中心から塊までの距離 m
        float sink = 0.1f;        // 足元より下へ埋める m
        DirectX::SimpleMath::Vector4 color = { 0.55f, 0.85f, 1.30f, 1.0f };   // linear（1 を超えると bloom）
        float alpha = 0.55f;      // 透け具合（premultiplied の alpha blend）
    };
    IceLookStyle iceLook;

    // ---- 経験値オーブの見た目（SwarmOrbVS / SwarmOrbPS / SwarmOrbEmitCS）----
    // 自発光の宝石（双角錐）。待機中は浮き沈み・自転・脈動（スロット毎に位相をずらす）。
    // 吸い寄せられると長軸をプレイヤーへ傾けて引き伸ばし、pullColor へ寄って明るくなり、
    // ExpOrbTrail.json の粒子を尾に出す。色は linear HDR（Bloom の閾値は 1）
    struct OrbLookStyle
    {
        float scale = 1.0f;             // 網の倍率（網は赤道の半径 0.13m、高さ 0.4m）
        float bobHeight = 0.08f;        // 浮き沈みの振幅 m
        float bobSpeed = 2.5f;          // rad/s
        float spinSpeed = 2.0f;         // 長軸まわりの自転 rad/s
        float pulseAmount = 0.08f;      // 大きさの脈動 1 ± これ
        float pulseSpeed = 4.0f;        // rad/s
        float fullPullSpeed = 12.0f;    // 吸い寄せの速さがこれ（m/s）で見た目の変化が最大
        float stretchPerSpeed = 0.06f;  // 1 m/s 毎に伸びる割合（体積は保つ）
        float stretchMax = 0.8f;        // 伸びの上限（1 + これ 倍）
        float tiltMax = 1.3f;           // 長軸をプレイヤーへ傾ける最大角 rad
        float pullGlow = 1.8f;          // 吸い寄せ中の明るさの倍率
        DirectX::SimpleMath::Vector4 idleColor = { 0.20f, 0.65f, 1.00f, 1.0f };
        DirectX::SimpleMath::Vector4 pullColor = { 0.55f, 0.95f, 1.00f, 1.0f };
        float emissive = 1.6f;          // 本体の明るさ（色 × これ）
        float facet = 0.55f;            // 面の陰影（0 = 一様に光る / 1 = 太陽の向きで面毎に明暗）
        float rimGain = 1.2f;           // 縁の光（fresnel）
        float rimPower = 2.5f;
        float glintGain = 1.5f;         // 太陽の鋭いきらめき
        float glintPower = 24.0f;
        DirectX::SimpleMath::Vector4 rimColor = { 0.60f, 0.90f, 1.00f, 1.0f };
        bool  trail = true;             // 吸い寄せ中の尾（粒子）
        float trailMinSpeed = 3.0f;     // m/s。これより速く吸い寄せられている物だけ尾を出す
    };
    OrbLookStyle orbLook;

    // ---- 死んだ敵の砕け散り（2026-10-02、ユーザーが選んだ「ブロックが砕けて飛び散る」）----
    // 部品（頭・胴・腕・脚）がプレイヤーから離れる向きへ飛び、回りながら 1 回跳ねて、life 秒で縮んで消える。
    // 足元に土煙（範囲 MobDeath、威力 0 の見た目だけ。deathArea は MobSpawner::Init が名前で引く）
    struct CorpseStyle
    {
        bool  enabled = true;
        float life = 0.9f;          // 秒
        float gravity = 18.0f;      // m/s^2（重力 25 より軽くして少し長く宙に）
        float fling = 3.5f;         // プレイヤーから離れる向きの初速 m/s
        float up = 4.5f;            // 上向きの初速 m/s
        float spin = 9.0f;          // 回転 rad/s（着地後は 1/3）
        float bounce = 0.35f;       // 着地で残る速さの割合
        float flash = 3.0f;         // 死んだ瞬間の明るさ（頂点色の倍率。1 を超えると bloom）
        float flashTime = 0.08f;    // その明るさが戻るまで 秒
        float shrinkStart = 0.55f;  // life のこの割合から縮み始める
        uint32_t deathArea = 0;     // 土煙の AreaDef。0 = 出さない
    };
    CorpseStyle corpse;

    // ---- 液溜まり（Liquid entry を持つレシピの範囲。Render の中、オーブの後・丸い影の前）----
    // 見た目は VFX の json の Liquid entry（VFXLiquidDef）。ここは描くかどうかだけ
    bool liquids = true;
    // CPU の経路（VFXLiquidRenderer）に地形を渡す用。戦闘シーンが毎フレーム SetTerrain に入れる
    ID3D11ShaderResourceView* GetHeightSRV() const { return m_HeightSRV.Get(); }
    const Swarm::FrameCB& GetFrameCB() const { return m_CachedFrameCB; }

    // 雑魚の歩きアニメの再生速度（部品アニメがある時だけ効く。移動速度と足の運びを合わせる調整用）
    float enemyWalkAnimRate = 1.0f;

    // ---- CPU 側から生成を依頼する（Flush でまとめて反映）----
    // kind: Swarm::kEnemyKindMob / kEnemyKindBomber
    // yaw : 向き（rad、+Z が 0）。砂旋（kEnemyKindTornado）はこの向きへ飛ぶ。他の種類は AI が上書きする（2026-10-17）
    void SpawnEnemy(const DirectX::SimpleMath::Vector3& pos, float hp, float moveSpeed,
        uint32_t kind = Swarm::kEnemyKindMob, float yaw = 0.0f);
    // motion  : SetMotions で上げた表の番号（0 = 直進）
    // mirror  : 曲線を左右反転して撃つ（交互撃ち・乱数撃ちは呼ぶ側が決める）
    // 曲線の型では vel の「速さ」だけが使われ、向きは曲線が決める。
    // 捕捉する敵はプレイヤーに一番近い 1 体（武器が狙っているのと同じ相手）
    // triggerTag : この弾が消えたら誘発できる上級魔法（杖の spells の添字の bit）。0 = 無し
    // spawnAtPos : Drop 型だけ。pos を着弾点にする（誘発の隕石。最寄りの敵を捕捉しない）
    // areaDamageMul / areaDurationMul : この弾が命中・着弾で出す範囲の威力 / 持続に掛ける
    //   （魔法威力の能力アップ・魔力解放中の弾。持続は「持続する範囲」だけ伸びる。Swarm::PackSpawnBoost）
    void SpawnProjectile(VFXId vfx,
        const DirectX::SimpleMath::Vector3& pos,
        const DirectX::SimpleMath::Vector3& vel,
        float damage, float radius, float lifetime,
        uint32_t motion = 0, bool mirror = false,
        uint32_t triggerTag = 0, bool spawnAtPos = false,
        float areaDamageMul = 1.0f, float areaDurationMul = 1.0f,
        uint32_t roll = 0,   // 曲線の横ずれの回転（0〜127 = 0〜360 度。ProjectileProfileDB::NextRoll）
        uint32_t chainCount = 0);   // 連鎖のルーン: 当たった後に近くの別の敵へ跳ぶ回数（依頼の pathT に乗せる。2026-10-16）

    // リードバックで届いた誘発（タグ付きの弾が消えた場所）を全部取り出す。2〜3 フレーム古い
    void ConsumeTriggerEvents(std::vector<Swarm::TriggerEvent>& out)
    {
        out.insert(out.end(), m_TriggerEvents.begin(), m_TriggerEvents.end());
        m_TriggerEvents.clear();
    }
    // リードバックで届いたスプリッターの死（MobSpawner が分裂体を湧かせる）を全部取り出す。2〜3 フレーム古い
    void ConsumeSplitEvents(std::vector<Swarm::SplitEvent>& out)
    {
        out.insert(out.end(), m_SplitEvents.begin(), m_SplitEvents.end());
        m_SplitEvents.clear();
    }
    // リードバックで届いたダメージの数字（UI/DamageNumbers が描く）を全部取り出す。2〜3 フレーム遅れ（2026-10-17）
    void ConsumeDamageTexts(std::vector<Swarm::DamageText>& out)
    {
        out.insert(out.end(), m_DamageTexts.begin(), m_DamageTexts.end());
        m_DamageTexts.clear();
    }
    // ダメージの数字の出し方（SwarmDamageTextCS）。merge = 同じ敵への連続ヒットをこの秒数まとめて 1 つに
    struct DamageTextSettings { bool enabled = true; float merge = 0.12f; float poisonMerge = 0.5f; };   // poisonMerge = 毒素の削り（細かい）はこの秒数まとめる
    DamageTextSettings damageText;

    // ---- 地面の警告の輪（CPU が置く。Boss のスラム。2026-10-03）----
    struct WarnCircle
    {
        DirectX::SimpleMath::Vector3 center;   // 地面の高さ（y = 地形）
        float radius = 3.0f;
        float progress = 0.0f;                 // 0..1：中の円が広がり、1 で縁に届く（= 爆発）。負 = 縁だけ（中を塗らない）
        // > 0：半径 radius・幅 band m の帯として描く（Boss の衝撃波。2026-10-07）。帯は一点ずつ地面に沿う
        // （普通の輪は中心の高さ ±0.6m に抑えるので、半径 20m を超えると丘で浮く / 埋まる）
        float band = 0.0f;
        float _pad[2] = {};
    };
    static constexpr int kMaxWarnCircles = 64;   // 2026-10-07：投石雨・突進の道筋で増えた（32 → 64）
    // 今フレームの輪を丸ごと差し替える（毎フレーム呼ぶ。数 0 で消える）
    void SetWarnCircles(const WarnCircle* circles, int count);

    // 運動表を丸ごと差し替える。添字がそのまま SpawnProjectile の motion。
    // 飛んでいる弾も次のステップから新しい値で動く（エディタで調整中の反映用）
    void SetMotions(const std::vector<Swarm::Motion>& motions);

    // ---- 範囲攻撃（爆発・法環）----
    // CPU から 1 個出す（Flush でまとめて反映）。
    // tickTimer = 0 なら出た最初のステップで 1 回目のダメージが入る。
    // 単発は tickInterval を duration より長くしておけば 1 回しか tick しない。
    // vfxType は 0 のままにする：CPU から出した範囲の見た目は CPU 側で VFX を再生する
    void SpawnArea(const Swarm::Area& area);

    // 雛形の表を丸ごと差し替える。添字が Motion::hitArea。0 番は「無し」なので中身は使われない。
    // 弾が命中した場所に GPU が自分で範囲を出す時に引く
    void SetAreaDefs(const std::vector<Swarm::AreaDef>& defs);

    // 範囲を全部消す
    void ClearAreas();

    // ---- 毎フレーム ----
    // UpdateGameplay の末尾、粒子の Flush より前に呼ぶ。
    // 中で固定ステップを回すので、渡すのは実 dt でよい。
    // totalTime は発射の乱数 seed 用
    void Flush(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive, float dt, float totalTime);

    // 雑魚の見た目（焼いた静的メッシュ。デバッグ表示用）
    std::shared_ptr<Model> GetEnemyModel() const { return m_EnemyModel; }

    // 弾・範囲の点光源を PointLightManager のリストへ追記する。
    // CPU 側の光（VFX の Light entry）を積み終えた後、描画の前に呼ぶ
    void CollectLights();

    // ---- リードバック結果（1〜2 フレーム古い。用途上それで困らない）----
    const SwarmCounters& GetCounters() const { return m_Readback.Latest(); }
    // Boss の数・HP・位置（描画の CompactCS が書くので 2〜3 フレーム古い）
    const Swarm::BossInfo& GetBossInfo() const { return m_BossInfo; }
    Swarm::AICB& GetAIParams() { return m_CachedAICB; }
    // フローフィールド（探索範囲などの定数。次にプレイヤーのマスが変わった時の作り直しから効く）
    FlowField& GetFlowField() { return m_Flow; }
    // プレイヤーが同じマスに居ても次のフレームで作り直す（定数を変えた時）
    void RequestFlowRebuild() { m_FlowRequestX = m_FlowRequestZ = -1; }
    // 自爆兵の定数（次の固定ステップ / 次の描画から効く）。blastArea は呼ぶ側が AreaProfileDB から入れる
    Swarm::BomberCB& GetBomberParams() { return m_CachedBomberCB; }
    // プレイヤーが受けた累計ダメージを取り出して 0 に戻す
    float ConsumePlayerDamage();
    // プレイヤーが受けた打撃（ノックバック用）を取り出して 0 に戻す。
    // dir = 「敵 / 爆心 → プレイヤー」の単位ベクトルの和（XZ）、count = 回数。リードバックなので 2〜3 フレーム古い
    struct PlayerHits { DirectX::SimpleMath::Vector2 meleeDir, blastDir, spikeDir, windDir; uint32_t melee = 0, blasts = 0, spikes = 0, winds = 0; };   // spikes = トゲ玉、winds = 砂旋（2026-10-17）
    PlayerHits ConsumePlayerHits();
    // 前回からの間に出た「カメラを揺らす範囲」（爆発・光線。AreaProfile::cameraShake）の数（2〜3 フレーム遅れ）
    uint32_t ConsumeShakeAreas();
    // 前回からの間に GPU で生まれた範囲の数を VFX レシピ（Area::vfxType）毎に out へ足して 0 に戻す（音用。2〜3 フレーム遅れ）
    void ConsumeAreaBirths(std::array<uint32_t, Swarm::kAreaBirthKinds>& out);
    // 磁石: seconds の間、場の経験値オーブを全部吸い寄せ始める（OrbCB の吸い寄せ半径を場全体にする）
    void MagnetAllOrbs(float seconds = 0.3f) { m_MagnetTimer = (std::max)(m_MagnetTimer, seconds); }
    // TEMP-TEST: 敵の池と状態を丸ごと読み戻す（Map で止まる。自動テストの検証だけ。毎フレーム呼ばない）
    bool DebugReadEnemies(std::vector<Swarm::Enemy>& outEnemies, std::vector<uint32_t>& outStates,
        std::vector<Swarm::EnemyExtra>* outExtras = nullptr,    // outExtras: 種類（soak 自動テスト）
        std::vector<DirectX::SimpleMath::Vector4>* outStatus = nullptr);   // outStatus: 減速・凍結（enemySlow）
    // 光線（カプセル型の範囲）: チャンネル ch の起点 / 終点 / 半径を次の固定ステップから効かせる。
    // 範囲そのものは SpawnArea（flags に kAreaCapsule | ch << kAreaBeamShift）で出す。
    // active = false にすると GPU 側の範囲が次のステップで消える
    void SetBeam(uint32_t ch, const DirectX::SimpleMath::Vector3& start, const DirectX::SimpleMath::Vector3& end,
        float radius, bool active);
    // 光線の標的（SwarmBeamTargetCS）。cmd = Swarm::kBeamTarget*。毎フレーム呼ぶ（次の Flush で効く）。
    // serial はその光線の通し番号（チャンネルは使い回すので、答えがどの光線の物かを見分ける）
    void SetBeamTarget(uint32_t ch, uint32_t cmd, const DirectX::SimpleMath::Vector3& origin,
        const DirectX::SimpleMath::Vector3& dir, float length, const DirectX::SimpleMath::Vector3& seek, uint32_t serial);
    // リードバックした標的の位置（2〜3 フレーム古い）。serial が違う / 標的が無ければ false
    bool GetBeamTarget(uint32_t ch, uint32_t serial, DirectX::SimpleMath::Vector3& pos) const;
    // 同上、答えの有無も見分ける（連鎖の跳ぶ先。2026-10-20）: -1 = まだ答えが来ていない（serial が違う）、
    // 0 = 答えは来たが敵が居ない、1 = 居る（pos / slot に入る）
    int QueryBeamTarget(uint32_t ch, uint32_t serial, DirectX::SimpleMath::Vector3& pos, uint32_t& slot) const;

    // 燃えている草からの粒子（2026-10-12、GrassFireField）。毎フレーム Flush の前に渡す。null = 出さない。
    // 発射は弾・範囲の直後（Swarm/SwarmSystemFire.cpp）
    void SetGrassFire(const GrassFireEmitInput* in);
    // 範囲の池（GrassFireField が熱源として読む。SRV なので CS の UAV と同じフレームで使わない所で）
    ID3D11ShaderResourceView* GetAreaSRV() const { return m_AreaSRV.Get(); }
    ID3D11ShaderResourceView* GetAreaStateSRV() const { return m_AreaStateSRV.Get(); }
    // 同上（2026-10-13、草の踏み跡と落ち葉）：カプセル（光線）の終点、雑魚の池 / 状態 / 種類
    ID3D11ShaderResourceView* GetAreaEndSRV() const { return m_AreaEndSRV.Get(); }
    // 局所の風源の表（Swarm::WindSource × kMaxWindSources。草・落ち葉・粒子が Flush の後に読む。2026-10-19）
    ID3D11ShaderResourceView* GetWindSourceSRV() const { return m_WindSourceSRV.Get(); }
    // CPU の風源（2026-10-20、突風を風源にする）：life 秒だけ表に載る渦（swirl / updraft / inflow）+ 一方向の押し（dir × push）。
    // fade は出入りで滑らかに（sin）。一度に kMaxWindSources − 砂旋の数まで。Flush の中で歳を進める
    struct WindBurst
    {
        DirectX::SimpleMath::Vector3 pos;
        float radius = 2.5f;
        float swirl = 8.0f, updraft = 2.0f, inflow = 0.8f;
        DirectX::SimpleMath::Vector2 dir;   // 押す向き（xz 単位。0 = 押さない）
        float push = 0.0f;
        float life = 0.4f;
        float age = 0.0f;
    };
    void AddWindBurst(const WindBurst& b) { if (m_WindBursts.size() < Swarm::kMaxWindSources) m_WindBursts.push_back(b); }
    ID3D11ShaderResourceView* GetEnemySRV() const { return m_EnemySRV.Get(); }
    ID3D11ShaderResourceView* GetEnemyStateSRV() const { return m_EnemyStateSRV.Get(); }
    ID3D11ShaderResourceView* GetEnemyExtraSRV() const { return m_EnemyExtraSRV.Get(); }

    // ---- ImGui 表示用 ----
    const SwarmVFXTable& GetVFXTable() const { return m_VFX; }
    int GetPendingEnemySpawns() const { return (int)m_PendingEnemies.size(); }
    int GetPendingProjSpawns()  const { return (int)m_PendingProjectiles.size(); }
    int GetLastSubSteps()       const { return m_LastSubSteps; }
    double GetFlushMs()         const { return m_FlushMs; }
    uint32_t GetTotalRequested()  const { return m_TotalRequested; }
    uint32_t GetTotalDispatched() const { return m_TotalDispatched; }
    uint32_t GetTotalSteps()      const { return m_TotalSteps; }
    // 溢れ分の湧き。GPU が遠い雑魚を1体選んでこの内容へ上書きする（枠を消費しない）


    void RecycleEnemy(const Vector3& pos, float hp, float moveSpeed,
        uint32_t kind = Swarm::kEnemyKindMob);
    void SetRecycleMinDist(float d) { m_RecycleMinDist = d; }
    float ConsumeExp();

    // GPU 上の雑魚・弾・オーブを全部消す（地形の作り直し用）。
    // state を DEAD にするだけ。counter は触らない（累積値の差分が狂う）
    void KillAll();

    // 弾だけ消す（負荷テストのリセット用）
    void ClearProjectiles();

    // プレイヤーに一番近い雑魚（リードバックなので 1〜2 フレーム古い）。無ければ false
    bool GetNearestEnemy(Vector3& pos, Vector3& vel, float& dist) const
    {
        const auto& c = GetCounters();
        if (c.nearestDist >= 1e29f) return false;
        pos = { c.nearestPos[0], c.nearestPos[1], c.nearestPos[2] };
        vel = { c.nearestVel[0], c.nearestVel[1], c.nearestVel[2] };
        dist = c.nearestDist;
        return true;
    }
private:
    // --- 生成 ---
    bool CreateBuffers(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);
    // 雑魚の見た目: 骨付き FBX の 1 フレームを焼いた静的メッシュ（駄目ならカプセル）
    std::shared_ptr<Model> BuildEnemyModel(ID3D11Device* device);

    // --- Flush の内訳 ---
    void UploadFrameCB(const DirectX::SimpleMath::Vector3& playerPos,
        float playerRadius, bool playerAlive);
    void UploadSpawns();          // 溜めた生成依頼を GPU へ
    void DispatchStep();          // 固定ステップ 1 回分の CS 群
    void DispatchEmit(float dt, float totalTime);   // 弾から粒子を発射
    void DispatchFireEmit(float dt, float totalTime);   // 燃えている草から粒子を発射（SwarmSystemFire.cpp）
    GrassFireEmitInput m_GrassFire;
    bool m_HasGrassFire = false;
    void DispatchSprites(float dt);                  // 範囲の連番画像：古い物を進めて、生まれた範囲の分を始める
    void DispatchProjTrails();                       // 弾の帯：槽ごとの位置の環を進める（固定ステップ、弾の積分の直後。SwarmSystemTrail.cpp）
    void RequestReadback();       // counter の copy を発行

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    GPUParticleSystem* m_Particles = nullptr;

    // ============================================================
    // 本体バッファ（UAV で CS が書き、SRV で他の CS が読む）
    // ※同じ資源を UAV と SRV に同時に繋げない。
    //   各 dispatch の後で必ず UAV を外すこと
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemySRV;

    // 雑魚の種類と自爆兵の導火線（Swarm::EnemyExtra、スロットと同じ添字）。
    // SpawnEnemyCS / RecycleCS が書き、ContactCS が導火線を進める。AI・Compact・VS が読む
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyExtraBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyExtraUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyExtraSRV;

    // 敵の状態（float4、スロットと同じ添字。Swarm::kAnimFreezeRequest の説明）。
    // xy = 毒の池の減速（残り秒・強さ。2026-10-01）：AreaDamageCS が tick の度に書き、AICS が速度に掛けて数え下げる。
    // zw = 凍結（2026-10-08、アイスランス）：AICS が HitCS の依頼から入れて数え下げる。VS / ContactCS が読む。
    // Spawn / Recycle で 0 に戻す
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemySlowBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemySlowUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemySlowSRV;
    // 吹き飛ばし（突風。2026-10-12）：float4 = 速度 xz・残り秒。AreaDamageCS が書き、AICS が足して減らす。本体の横に持つ
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyPushBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyPushUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyPushSRV;
    // 毒素（毒沼。2026-10-17）：float4 = 残りダメージ・毎秒の削り・最後に盛られてからの秒。
    // AreaDamageCS が範囲の tick で盛って毎ステップ削る（倒した時の扱いは範囲ダメージと同じ）。VS が緑に染める。Spawn / Recycle で 0
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyPoisonBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyPoisonUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyPoisonSRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjSRV;

    // --- 投射物の運動 ---
    // path   : 投射物と同じ添字。GPU が組んだベジェ（CPU は触らない）
    // motion : 運動表。CPU から上げる（読み取り専用）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_PathBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_PathUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_PathSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_MotionBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_MotionSRV;

    // --- 範囲攻撃 ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaStateSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaEndBuffer;      // 槽ごとのカプセルの終点（float4。AreaTickCS が書く）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaEndUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaEndSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaDefBuffer;      // 雛形の表（CPU から書く）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaDefSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnAreaBuffer;    // 生成依頼
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_SpawnAreaSRV;
    std::vector<Swarm::Area> m_PendingAreas;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_OrbBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_OrbUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_OrbSRV;

    // ============================================================
    // 生死フラグ（本体とは別バッファ）
    // SM5.0 の原子操作は RWBuffer<uint> にしか使えないため、
    // スロットの取り合いに要る state だけを切り出している。
    // 本体と同じ index が同じ個体を指す
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EnemyStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyStateSRV;

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjStateSRV;

    // 弾スロット毎の誘発タグ（Swarm::TriggerEvent の説明）。SpawnProjCS が書き、ProjEndCS が消す
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjTagBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjTagUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjTagSRV;

    // 弾スロット毎の「出す範囲への倍率」（威力 12bit | 持続 12bit、Swarm::PackSpawnBoost の >> 8。2026-10-02）。
    // SpawnProjCS が書き、HitCS / ProjMoveCS が範囲を出す時に読む
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ProjBoostBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjBoostUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjBoostSRV;

    // 誘発の環（ProjEndCS が書く）→ staging 3 枚でリードバック。総数は GPU 上で永久に累積、CPU は読んだ所まで覚える
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_TriggerBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TriggerUAV;
    static constexpr int kTriggerStaging = 3;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TriggerStaging[kTriggerStaging];
    bool m_TriggerStagingFilled[kTriggerStaging] = {};
    int  m_TriggerStagingWrite = 0;
    uint32_t m_TriggerRead = 0;                         // 読み終えた総数
    std::vector<Swarm::TriggerEvent> m_TriggerEvents;   // 読んだが、まだ誰も取り出していない物
    void ReadTriggerEvents();   // 一番古い staging を読めたら新しい分を m_TriggerEvents へ（Flush の頭）

    // 分裂の環（CorpseTrackCS が書く）→ 誘発の環と同じく staging 3 枚でリードバック
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SplitBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SplitUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SplitStaging[kTriggerStaging];
    bool m_SplitStagingFilled[kTriggerStaging] = {};
    int  m_SplitStagingWrite = 0;
    uint32_t m_SplitRead = 0;
    std::vector<Swarm::SplitEvent> m_SplitEvents;
    void ReadSplitEvents();

    // ダメージの数字の環（SwarmDamageTextCS が書く）→ 同じく staging 3 枚でリードバック（2026-10-17）
    std::shared_ptr<ComputeShader> m_DamageTextCS;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_EnemyPrevHpBuffer;     // 槽毎に前フレームの hp（死んだ槽は 0）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyPrevHpUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyPrevHpSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_DamagePendingBuffer;   // float4 = 直接 / 毒素 / 経過秒 / 前フレームの毒素
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_DamagePendingUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_DamagePendingSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_DamageTextBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_DamageTextUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DamageTextStaging[kTriggerStaging];
    bool m_DamageTextStagingFilled[kTriggerStaging] = {};
    int  m_DamageTextStagingWrite = 0;
    uint32_t m_DamageTextRead = 0;
    std::vector<Swarm::DamageText> m_DamageTexts;
    struct DamageTextCB { float step; float merge; uint32_t on; float poisonMerge; };
    void DispatchDamageText(float dt);   // Flush の砕け散りの後
    void ReadDamageTexts();

    // 地面の警告の輪（CPU の WarnCircle を毎フレーム書く動的 buffer → SwarmWarnRingVS + SwarmBomberRingPS）
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_WarnCircleBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_WarnCircleSRV;
    int m_WarnCircleCount = 0;
    std::shared_ptr<VertexShader> m_WarnRingVS;
    void RenderWarnRings(CameraBase* camera);

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_OrbStateBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_OrbStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_OrbStateSRV;

    // --- counter（CS が InterlockedAdd で書き、CPU がリードバック）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CounterBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CounterUAV;

    // --- 発射予約（RAW UAV。毎フレーム 0 に戻す）---
    // 1スレッドが k 個発射する時、スレッド番号では deadCount と比べられないので
    // 原子的に予約して超過分を諦める。粒子 EmitCS のガードと同じ思想、別の形
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_EmitBudget;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EmitBudgetUAV;

    // ---- 範囲の連番画像（SwarmSprite.hlsli）----
    // 再生中の表は環（満杯なら一番古い物から上書き）。範囲が消えても再生は続く。
    // areaSeen = 範囲の槽ごとに前のフレームの timeLeft（asuint）。0xFFFFFFFF = 空・未見
    static constexpr uint32_t kMaxSprites = 1024;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SpriteBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SpriteUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_SpriteSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AreaSeenBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaSeenUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaSeenSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SpriteHead;       // 環の書き込み位置（RAW）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SpriteHeadUAV;
    std::shared_ptr<ComputeShader> m_SpriteCS;
    std::shared_ptr<VertexShader>  m_SpriteVS;
    std::shared_ptr<PixelShader>   m_SpritePS;

    // ---- 弾の帯（Trail entry、2026-10-15。SwarmSystemTrail.cpp）----
    // 弾の槽ごとに kTrailPoints 個の位置の環（粒子の帯と同じ ParticleTrail.hlsli）。
    // trailState = 環の書き込み位置 / 有効な点の数、trailAge = 槽が生きてからの秒（記録の時計）
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_ProjTrailPoints;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjTrailPointsUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjTrailPointsSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_ProjTrailState;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjTrailStateUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjTrailStateSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_ProjTrailAge;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_ProjTrailAgeUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_ProjTrailAgeSRV;
    std::shared_ptr<ComputeShader> m_ProjTrailCS;
    std::shared_ptr<VertexShader>  m_ProjTrailVS;
    std::shared_ptr<PixelShader>   m_ProjTrailPS;   // 粒子の帯と同じ ParticleTrailPS
    std::shared_ptr<Texture>       m_TrailWhite;    // テクスチャの無い style 用

    // ---- 液溜まり（Liquid entry、2026-10-02）----
    // areaDirs   : 範囲を出した物が飛んでいた向き（xy = 単位 xz、w = 1 未読）。ProjMoveCS が書き、追跡が読んで w を消す
    // liquidTrack: 槽ごとの (向き xz, 最初に見た時の残り時間, 前フレームの残り時間)。w <= 0 = 空。SwarmLiquidTrackCS
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AreaDirBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaDirUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AreaDirSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_LiquidTrackBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_LiquidTrackUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_LiquidTrackSRV;
    std::shared_ptr<ComputeShader> m_LiquidTrackCS;
    std::shared_ptr<VertexShader>  m_LiquidVS;   // SwarmLiquidVS
    std::shared_ptr<PixelShader>   m_LiquidPS;   // VFXLiquidPS（CPU の経路と同じ）
    void DispatchLiquidTrack();
    void RenderLiquids(CameraBase* camera, const LightBuffer& light);

    // ---- 死んだ敵の砕け散り（2026-10-02）----
    // prevAlive : 槽毎の前フレームの生死（SwarmCorpseTrackCS が「生 → 死」を見つける）
    // corpses   : 環（Swarm::kMaxCorpses 枠）。corpseHead の [0] = 今までに書いた数
    // 一覧      : 見た目毎（雑魚 / 自爆兵 / 幽霊）の砕け中の環の番号（SwarmCorpseListCS、append）
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_EnemyPrevAliveBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyPrevAliveUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyPrevAliveSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_CorpseBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CorpseUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_CorpseSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_CorpseHead;   // RAW
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CorpseHeadUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_CorpseListBuffer[Swarm::kEnemyKinds];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CorpseListUAV[Swarm::kEnemyKinds];   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_CorpseListSRV[Swarm::kEnemyKinds];
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_CorpseDrawArgs[Swarm::kEnemyKinds];  // submesh 毎
    std::shared_ptr<ComputeShader> m_CorpseTrackCS;
    std::shared_ptr<ComputeShader> m_CorpseListCS;
    std::shared_ptr<VertexShader>  m_CorpseVS;
    DirectX::SimpleMath::Vector4   m_PartPivots[8] = {};   // 部品の付け根（焼いた姿勢のノードの原点。BuildEnemyPartAnim）
    void DispatchCorpses();
    void RenderCorpses(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);

    // --- 地形（起動時に1回。読み取り専用）---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TerrainBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TerrainSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HeightBuffer;      // 高さ場（GridWorld::Heights）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeightSRV;

    // --- 雑魚の空間ハッシュ（地形と同じ格子。毎ステップ BinCS が詰め直す）---
    // cellCount[cell] = そのマスの活き数、cellItems[cell*CAP + k] = スロット番号。
    // AI の分離と PushCS の押し出しは 3x3 マスしか見ない（全対全をやめた）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CellCountBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CellCountUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CellCountSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_CellItemsBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_CellItemsUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_CellItemsSRV;
    static constexpr uint32_t kBucketCap = 32;   // = SwarmCommon.hlsli の SWARM_BUCKET_CAP

    // --- 巡路（流れ場）---
    // CPU の FlowField がプレイヤーのマスへの向きをマス毎に持ち、GPU の AI が読む。
    // プレイヤーのマスが変わった時だけ作り直して Map で上げる（地形は静的）。
    // 作り直しは別スレッド（std::async）で作業用の場 m_FlowWorker に作らせ、出来たら m_Flow が結果を貰う
    // （300m のフィールドの全域は Debug で約 13ms。主スレッドで回すとマスを跨ぐ度に引っ掛かる）。
    // 走っている間にプレイヤーが更にマスを跨いだら、終わった後で最新のマスでもう一度作る
    FlowField m_Flow;
    FlowField m_FlowWorker;
    std::future<bool> m_FlowJob;
    int m_FlowRequestX = -1, m_FlowRequestZ = -1;   // 最後に作らせた目標マス
    void WaitFlowJob();                             // 走っている作り直しを待って捨てる（地形の差し替え・終了の前）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FlowBuffer;      // float2 × マス数（DYNAMIC）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_FlowSRV;
    void UpdateFlowField(const DirectX::SimpleMath::Vector3& playerPos);

    // --- VFX レシピ表（起動時に1回。読み取り専用）---
    SwarmVFXTable m_VFX;

    std::shared_ptr<VertexShader> m_DebugVS;
    std::shared_ptr<PixelShader>  m_DebugPS;
    std::shared_ptr<VertexShader> m_DebugEnemyVS;
    struct DebugCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
    };
    // ============================================================
    // 生成キュー
    // CPU が空きスロットを管理する。
    // ※consume buffer を使わない理由:
    //   CopyStructureCount は命令キューをフラッシュするため
    //   約 0.076ms の固定コストが乗る（粒子で計測済み）
    // ============================================================
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnEnemyBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnEnemySRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnProjBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnProjSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnProjExtraBuffer;   // 依頼と同じ添字の uint2（誘発タグ, kSpawnAtPos）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SpawnProjExtraSRV;
    std::vector<uint32_t> m_PendingProjExtra;                      // 2 個ずつ（m_PendingProjectiles と並ぶ）

    std::vector<Swarm::Enemy>      m_PendingEnemies;
    std::vector<Swarm::Projectile> m_PendingProjectiles;

    // --- 定数バッファ ---
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_FrameCB;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_SpawnCB;
    Swarm::FrameCB m_CachedFrameCB;
    Swarm::AICB m_CachedAICB;
    Swarm::OrbCB m_CachedOrbCB;
    Swarm::BomberCB m_CachedBomberCB;
    Swarm::BeamCB m_CachedBeamCB = {};   // 光線の起点 / 終点（SetBeam）

    // ============================================================
    // CS 群
    // 順序は DispatchStep / DispatchEmit が決める。宣言順に意味は無い
    // ============================================================
    std::shared_ptr<ComputeShader> m_ClearCountersCS;  // 子ステップ頭で counter を 0 に
    std::shared_ptr<ComputeShader> m_SpawnProjCS;      // 生成依頼を投射物の空きスロットへ
    std::shared_ptr<ComputeShader> m_ProjMoveCS;       // 投射物の積分
    std::shared_ptr<ComputeShader> m_ProjEndCS;        // タグ付きの弾が消えた場所を誘発の環へ（命中の直後）
    std::shared_ptr<ComputeShader> m_BeamTargetCS;     // 光線の標的（捕まえた敵 → 死んだら向きに近い敵へ）
    std::shared_ptr<ComputeShader> m_EmitCS;           // 弾から粒子を発射
    std::shared_ptr<ComputeShader> m_SpawnEnemyCS;     // Phase 3
    std::shared_ptr<ComputeShader> m_EnemyAICS;        // Phase 3: seek + separation + 回避
    std::shared_ptr<ComputeShader> m_HitCS;            // Phase 4: 弾 vs 敵
    std::shared_ptr<ComputeShader> m_ContactCS;        // Phase 4: 敵 vs プレイヤー
    std::shared_ptr<ComputeShader> m_OrbCS;            // Phase 4: 経験値オーブの吸引・取得（DispatchStep の第 7 段）
    std::shared_ptr<ComputeShader> m_EnemyMoveCS;
    // ---- 雑魚描画 ----
    std::shared_ptr<VertexShader> m_EnemyVS;      // SwarmEnemyVS（buffer から位置と向きを読む）
    std::shared_ptr<PixelShader>  m_EnemyPS;      // Shader/PS.hlsl をそのまま使う
    std::shared_ptr<Material>     m_EnemyMaterial;// VS/PS + 既定テクスチャの束ね役
    std::shared_ptr<Model>        m_EnemyModel;   // 雑魚共通のカプセル

    // --- 経験値オーブの本描画 ---
    std::shared_ptr<VertexShader> m_OrbVS;
    std::shared_ptr<PixelShader>  m_OrbPS;         // 自発光の宝石（SwarmOrbPS）
    std::shared_ptr<Material>     m_OrbMaterial;
    std::shared_ptr<Model>        m_OrbModel;      // 双角錐（PrimitiveBuilder::CreateBipyramid）
    // 吸い寄せ中のオーブから粒子（SwarmEmitCS と同じ発射。レシピは VFXId::ExpOrbTrail 固定）
    std::shared_ptr<ComputeShader> m_OrbEmitCS;
    uint32_t m_OrbTrailVfx = 0;                    // レシピ表の番号。0 = 無し（json が読めなかった）

    std::shared_ptr<ComputeShader> m_RecycleCS;
    std::vector<Swarm::Enemy> m_PendingRecycles;
    std::vector<Swarm::Enemy> m_EnemyUpload;     // 新規 + 転送を連結した一時領域
    float m_RecycleMinDist = 35.0f;

    // ---- 範囲攻撃 ----
    std::shared_ptr<ComputeShader> m_SpawnAreaCS;    // CPU の依頼を空きスロットへ
    std::shared_ptr<ComputeShader> m_AreaTickCS;     // 時計を進める・プレイヤーに追従・tick の判定（命中の直後）
    std::shared_ptr<ComputeShader> m_AreaDamageCS;   // tick した範囲の中の雑魚へダメージ
    std::shared_ptr<ComputeShader> m_AreaEmitCS;     // GPU が出した範囲（弾の命中）から粒子を発射
    std::shared_ptr<ComputeShader> m_FireEmitCS;     // 燃えている草から粒子を発射（Shader/Grass/GrassFireEmitCS）
    std::shared_ptr<ComputeShader> m_LightCollectCS;     // 弾の点光源を PointLightManager へ追記
    std::shared_ptr<ComputeShader> m_AreaLightCollectCS; // 範囲の分
    std::shared_ptr<ComputeShader> m_EnemyCompactCS;     // 活きスロットの一覧（描画の instance 数）
    std::shared_ptr<ComputeShader> m_EnemyBinCS;         // 空間ハッシュ詰め（ステップ先頭）
    std::shared_ptr<ComputeShader> m_EnemyPushCS;        // 重なり解消（積分の後）

    // --- 雑魚描画の間接引数 ---
    // 4096 槽を毎フレーム全部 DrawInstanced すると頂点数がモデル × 4096 になる
    // （Minion 8.6k 頂点で 3500 万）。活きスロットだけ描くために
    // CompactCS → 種類毎の一覧、CopyStructureCount → args[種類][submesh].InstanceCount。
    // 種類はメッシュが同じでテクスチャだけ違うので、一覧毎に 1 回ずつ描く。
    // aliveList（全種類）は HP バー用
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AliveListBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AliveListUAV;   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_AliveListSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_KindListBuffer[Swarm::kEnemyKinds];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_KindListUAV[Swarm::kEnemyKinds];   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_KindListSRV[Swarm::kEnemyKinds];
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_EnemyDrawArgs[Swarm::kEnemyKinds];  // submesh 毎（IndexCount が違う）
    bool CreateEnemyDrawArgs(ID3D11Device* device);
    std::shared_ptr<Texture> m_BomberAlbedo;   // 自爆兵のテクスチャ（雑魚と同じメッシュ用）。null = 雑魚と同じテクスチャ
    std::shared_ptr<Texture> m_SplitterAlbedo; // スプリッター・分裂体のテクスチャ（黄色い衝突試験人形）。null = 雑魚と同じテクスチャ
    std::shared_ptr<Texture> m_BruteAlbedo;    // 重装兵のテクスチャ（緑のオーク。2026-10-07 夜）。null = 雑魚と同じテクスチャ
    std::shared_ptr<Texture> m_ChargerAlbedo;  // 突撃兵（黒い忍者。2026-10-08）
    std::shared_ptr<Texture> m_ShieldAlbedo;   // 盾兵（紺の警官。2026-10-08）
    std::shared_ptr<Texture> m_BurrowerAlbedo; // 砂潜り（2026-10-17）
    std::shared_ptr<Texture> m_SandMageAlbedo; // 砂嵐の術師（2026-10-17）
    std::shared_ptr<Texture> m_DashBomberAlbedo; // 突撃爆弾兵（灰色ロボット。2026-10-18）
    // 描画リストのテクスチャ（null = 雑魚の材質の物）
    Texture* ListAlbedo(uint32_t list) const
    {
        if (list == Swarm::kDrawListBomber) return m_BomberAlbedo.get();
        if (list == Swarm::kDrawListSplitter) return m_SplitterAlbedo.get();
        if (list == Swarm::kDrawListBrute) return m_BruteAlbedo.get();
        if (list == Swarm::kDrawListCharger) return m_ChargerAlbedo.get();
        if (list == Swarm::kDrawListShield) return m_ShieldAlbedo.get();
        if (list == Swarm::kDrawListBurrower) return m_BurrowerAlbedo.get();
        if (list == Swarm::kDrawListSandMage) return m_SandMageAlbedo.get();
        if (list == Swarm::kDrawListDashBomber) return m_DashBomberAlbedo.get();
        return nullptr;
    }

    // ---- 突撃兵・盾兵・凍結の見た目（2026-10-08。中身は SwarmSystemKinds.cpp）----
    // CompactCS は UAV が 8 本で埋まっているので、突撃兵・盾兵の一覧は SwarmEnemyKindListCS が別に作る
    std::shared_ptr<ComputeShader> m_KindListCS;
    // 突撃兵の予告の帯: DrawInstancedIndirect { 24 区間 x 6 頂点, InstanceCount = 突撃兵の一覧の長さ, 0, 0 }
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_ChargeLineArgs;
    std::shared_ptr<VertexShader> m_ChargeLineVS;
    std::shared_ptr<PixelShader>  m_ChargeLinePS;
    // 盾兵の盾 / 凍った敵の氷: 特効モデルを雑魚の PS（既定の白テクスチャ × 頂点色）で描く
    std::shared_ptr<VertexShader> m_ShieldVS;
    std::shared_ptr<VertexShader> m_IceVS;
    std::shared_ptr<Material>     m_ShieldMaterial;
    std::shared_ptr<Material>     m_IceMaterial;
    std::shared_ptr<Model>        m_ShieldModel;
    std::shared_ptr<Model>        m_IceModel;
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_ShieldDrawArgs;   // submesh 毎（InstanceCount = 盾兵の一覧）
    // トゲ玉（2026-10-17）：一覧は m_KindList* の外（雑魚のメッシュを描かないので描画リストの表に入れない）
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_SpikeBallListBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_SpikeBallListUAV;   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_SpikeBallListSRV;
    std::shared_ptr<VertexShader> m_SpikeBallVS;
    std::shared_ptr<Material>     m_SpikeBallMaterial;
    std::shared_ptr<Model>        m_SpikeBallModel;     // PrimitiveBuilder::CreateSpikeBall（半径 1）
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_SpikeBallDrawArgs;
    // 砂潜りの砂の山（2026-10-17）：一覧は m_KindList[kDrawListBurrower]（体は雑魚のメッシュで描くので表の中）
    std::shared_ptr<VertexShader> m_MoundVS;
    std::shared_ptr<Material>     m_MoundMaterial;
    std::shared_ptr<Model>        m_MoundModel;      // PrimitiveBuilder::CreateSphere（半径 1）
    std::vector<Microsoft::WRL::ComPtr<ID3D11Buffer>> m_MoundDrawArgs;
    // 砂嵐の術師の砂旋（2026-10-17）：一覧は m_KindList* の外（雑魚のメッシュを描かない。10-20 から砂旋は何も描かず、砂埃と風源だけ）
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_TornadoListBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_TornadoListUAV;   // APPEND
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_TornadoListSRV;
    std::shared_ptr<ComputeShader> m_TornadoEmitCS;  // 足元の砂埃（TornadoDust の配方）
    uint32_t m_TornadoDustVfx = 0;                   // レシピ表の番号。0 = 無し
    void DispatchTornadoEmit(float dt, float totalTime);   // Flush の中、草の火の発射の直後
    // 局所の風源（2026-10-19）：砂旋 → Swarm::WindSource の表（kMaxWindSources 枠）。Flush の中、砂埃の直後
    std::shared_ptr<ComputeShader> m_WindSourceCS;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_WindSourceBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_WindSourceUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_WindSourceSRV;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_WindSourceCount;   // RAW 4B（InterlockedAdd の枠取り）
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_WindSourceCountUAV;
    // CPU の風源（AddWindBurst）：毎フレーム生きている物を Swarm::WindSource にして DYNAMIC の buffer へ（CS の t3）
    std::vector<WindBurst> m_WindBursts;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_WindBurstBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_WindBurstSRV;
    void DispatchWindSources(float dt);
    // SwarmShieldVS / SwarmIceVS の b4
    struct PropMeshCB
    {
        DirectX::SimpleMath::Vector3 anchor;   // モデル座標でこの点を hold に置く
        float scale;                           // モデルの単位 → m
        DirectX::SimpleMath::Vector3 hold;
        float yaw;
        DirectX::SimpleMath::Vector4 tint;
        float bob, alpha, _pad[2];
    };
    static_assert(sizeof(PropMeshCB) == 64, "PropMeshCB layout mismatch");
    bool LoadKindAssets(ID3D11Device* device);       // LoadShaders の最後（雑魚のモデルの後）
    void DispatchKindLists();                        // CompactCS の直後（同じ描画の前準備）
    void RenderShields(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);
    void RenderSpikeBalls(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);   // 盾の後（2026-10-17）
    void RenderMounds(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);   // トゲ玉の後（2026-10-17）
    void RenderIce(const DirectX::SimpleMath::Matrix& view, const DirectX::SimpleMath::Matrix& proj);
    void RenderChargeLines(CameraBase* camera);

    // --- Boss の様子（CompactCS が書く 32B → staging 3 枚でリードバック。counter と同じ流儀で待たない）---
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_BossInfoBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_BossInfoUAV;
    static constexpr int kBossStaging = 3;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BossStaging[kBossStaging];
    bool m_BossStagingFilled[kBossStaging] = {};
    int  m_BossStagingWrite = 0;
    Swarm::BossInfo m_BossInfo;
    float m_MagnetTimer = 0.0f;   // MagnetAllOrbs の残り秒（> 0 の間 OrbMoveCS の吸い寄せ半径を場全体に）
    void ReadBossInfo();   // 一番古い staging を読めたら m_BossInfo を更新（Flush の頭）

    // --- プレイヤーが受けた打撃の向き（ContactCS u6 が永久に累積する 32B → staging 3 枚、CPU は差分）---
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_PlayerHitBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_PlayerHitUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_PlayerHitStaging[kBossStaging];
    bool m_PlayerHitStagingFilled[kBossStaging] = {};
    int  m_PlayerHitStagingWrite = 0;
    Swarm::PlayerHitInfo m_LastPlayerHits;   // 前リードバックんだ累計（差分の基準。counters と同じく戻さない）
    PlayerHits m_PendingPlayerHits;          // 読んだ差分の未消費ぶん
    void ReadPlayerHits();

    // 生まれた範囲の数の累計（2026-10-03）。SwarmLiquidTrackCS が新しい範囲を見つけた時に足す。
    //   [0] = カメラを揺らす範囲（kAreaShake）、[1 + vfxType] = GPU の VFX レシピ毎（命中の火花・爆発・土煙・毒の池 → 音）
    // 永久に累積・CPU は差分（counters と同じ。SwarmCounters は変えない）
    static constexpr uint32_t kAreaBirthSlots = 1 + Swarm::kAreaBirthKinds;
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_AreaBirthBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_AreaBirthUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_AreaBirthStaging[kBossStaging];
    bool     m_AreaBirthStagingFilled[kBossStaging] = {};
    int      m_AreaBirthStagingWrite = 0;
    std::array<uint32_t, kAreaBirthSlots> m_LastAreaBirths = {};
    uint32_t m_PendingShakes = 0;
    std::array<uint32_t, Swarm::kAreaBirthKinds> m_PendingAreaBirths = {};
    void ReadAreaBirths();

    // --- 光線の標的（SwarmBeamTargetCS が毎フレーム書く 32B × kMaxBeams → staging 3 枚でリードバック）---
    Swarm::BeamTargetCB m_CachedBeamTargetCB = {};
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_BeamTargetBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_BeamTargetUAV;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BeamTargetStaging[kBossStaging];
    bool m_BeamTargetStagingFilled[kBossStaging] = {};
    int  m_BeamTargetStagingWrite = 0;
    Swarm::BeamTarget m_BeamTargets[Swarm::kMaxBeams];   // 最後に読めた答え
    void DispatchBeamTargets();   // Flush の固定ステップの後（敵の位置が決まってから）
    void ReadBeamTargets();

    // --- 雑魚の部品アニメ（部品をノードで動かすモデルだけ。Kenney Blocky）---
    // 表: [クリップ][フレーム][部品] の行列。行列は「焼いた姿勢の部品 → そのフレームの部品」の差分
    //     （頂点は焼いた姿勢で入っているので、VS はこれを掛けるだけで動く）。
    // クリップ: 0 待機 / 1 歩き / 2 近接攻撃。どれを出すかは VS が敵の状態から決める
    struct EnemyAnimCB
    {
        uint32_t part = 0;           // 今描いている submesh（描画毎に書き換える）
        uint32_t partCount = 0;
        uint32_t enabled = 0;        // 0 = 表が無い → VS は従来の procedural な揺れ
        uint32_t _pad = 0;
        uint32_t clipStart[4] = {};  // 表の中の最初のフレーム番号
        uint32_t clipFrames[4] = {};
        float    clipLength[4] = {}; // 秒
        float    time = 0.0f;        // 待機に使う時計（秒）
        float    walkRate = 1.0f;    // 歩きの再生速度
        float    _pad2[2] = {};
    };
    static_assert(sizeof(EnemyAnimCB) == 80, "EnemyAnimCB layout mismatch");
    EnemyAnimCB m_EnemyAnim;
    const char* m_AnimClips[3] = { "", "", "" };   // 待機 / 歩き / 攻撃 のクリップ名（BuildEnemyModel が入れる）
    Microsoft::WRL::ComPtr<ID3D11Buffer>             m_PartAnimBuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_PartAnimSRV;
    float m_AnimClock = 0.0f;
    // 焼いた姿勢（bakeClip の bakeFrac）と rootTransform から表を作る。失敗したら enabled = 0 のまま
    bool BuildEnemyPartAnim(ID3D11Device* device, const char* modelPath,
        const char* bakeClip, float bakeFrac, const DirectX::SimpleMath::Matrix& rootTransform,
        size_t partCount);

    // --- 雑魚の HP バー ---
    // 生成時の hp（固定小数、スロット毎）。SpawnEnemyCS / RecycleCS が書き、バーの VS が割る。
    // Enemy 本体（48B）に場所が無いので横に持つ
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_EnemyMaxHpBuffer;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_EnemyMaxHpUAV;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>  m_EnemyMaxHpSRV;
    // DrawInstancedIndirect: { 6 頂点, InstanceCount = 活き数（CopyStructureCount）, 0, 0 }
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HpBarArgs;
    std::shared_ptr<VertexShader> m_HpBarVS;
    std::shared_ptr<PixelShader>  m_HpBarPS;
    void RenderHpBars(CameraBase* camera);

    // --- 雑魚の足元の丸い影（間接引数は HP バーと同じ: 6 頂点 x 活き数）---
    std::shared_ptr<VertexShader> m_BlobShadowVS;
    std::shared_ptr<PixelShader>  m_BlobShadowPS;
    void RenderBlobShadows(CameraBase* camera);

    // --- 自爆兵の警告の輪 ---
    // DrawInstancedIndirect: { 6 頂点, InstanceCount = 自爆兵の一覧の長さ, 0, 0 }。点火していない分は VS が捨てる
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_BomberRingArgs;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_DashBomberRingArgs;   // 同じ輪を突撃爆弾兵の一覧からもう一度（2026-10-19）
    std::shared_ptr<VertexShader> m_BomberRingVS;
    std::shared_ptr<PixelShader>  m_BomberRingPS;
    void RenderBomberRings(CameraBase* camera);

    // --- 隕石の警告の輪 ---
    // DrawInstanced(6, kMaxProjectiles): 弾の全スロット。DROP でない・死んだ分は VS が捨てる。
    // PS は m_BomberRingPS を共用（b0 の並びが同じ）
    std::shared_ptr<VertexShader> m_DropRingVS;
    void RenderDropRings(CameraBase* camera);

    std::shared_ptr<ComputeShader> m_AimResolveCS;
   // Phase 4: 最寄りの雑魚をリードバック用に書き出す
    // 転送の「誰が何番目を取ったか」用。dispatch 前に 0 にする
    Microsoft::WRL::ComPtr<ID3D11Buffer>              m_RecycleClaim;
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_RecycleClaimUAV;
    struct EnemyRenderCB
    {
        DirectX::SimpleMath::Matrix world = DirectX::SimpleMath::Matrix::Identity;
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
    };
    // SwarmEnemyHpBarVS / PS の b0（row_major なので Transpose しない）
    struct HpBarCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        float width, height, offset, border;
        DirectX::SimpleMath::Vector4 fill;
        DirectX::SimpleMath::Vector4 back;
        DirectX::SimpleMath::Vector4 edge;
    };
    static_assert(sizeof(HpBarCB) == 192, "HpBarCB layout mismatch");
    // SwarmBlobShadowVS / PS の b0（row_major なので Transpose しない）
    struct BlobShadowCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        float radius, strength, lift, softness;
        float clamp, _pad[3];
    };
    static_assert(sizeof(BlobShadowCB) == 160, "BlobShadowCB layout mismatch");
    // SwarmCorpseVS の b4（砕け散り）
    struct CorpseCB
    {
        uint32_t part = 0, partCount = 0;
        float time = 0.0f, life = 0.9f;
        float gravity = 18.0f, fling = 3.5f, up = 4.5f, spin = 9.0f;
        float bounce = 0.35f, flash = 3.0f, flashTime = 0.08f, shrinkStart = 0.55f;
        DirectX::SimpleMath::Vector4 pivot[8] = {};
    };
    static_assert(sizeof(CorpseCB) == 176, "CorpseCB layout mismatch");
    // SwarmCorpseTrackCS / SwarmCorpseListCS の b4
    struct CorpseTrackCB { float time; uint32_t on; uint32_t deathArea; uint32_t _pad; };
    struct CorpseListCB { float time; float life; float _pad[2]; };
    // SwarmBomberRingVS / PS の b0（row_major なので Transpose しない）
    struct BomberRingCB
    {
        DirectX::SimpleMath::Matrix view;
        DirectX::SimpleMath::Matrix proj;
        DirectX::SimpleMath::Vector4 fill;
        DirectX::SimpleMath::Vector4 edge;
        DirectX::SimpleMath::Vector4 back;
        float edgeWidth, lift, _pad[2];
    };
    static_assert(sizeof(BomberRingCB) == 192, "BomberRingCB layout mismatch");
    // SwarmOrbVS の b4（orbLook の動きの分）
    struct OrbLookCB
    {
        float time, scale, bobHeight, bobSpeed;
        float spinSpeed, pulseAmount, pulseSpeed, fullPullSpeed;
        float stretchPerSpeed, stretchMax, tiltMax, pullGlow;
        DirectX::SimpleMath::Vector4 idleColor;
        DirectX::SimpleMath::Vector4 pullColor;
    };
    static_assert(sizeof(OrbLookCB) == 80, "OrbLookCB layout mismatch");
    // SwarmOrbPS の b1（b0 は LightBuffer）
    struct OrbShadeCB
    {
        float emissive, facet, rimGain, rimPower;
        float glintGain, glintPower, _pad[2];
        DirectX::SimpleMath::Vector4 rimColor;
    };
    static_assert(sizeof(OrbShadeCB) == 48, "OrbShadeCB layout mismatch");
    // SwarmOrbEmitCS の b3
    struct OrbEmitCB
    {
        uint32_t vfx;
        float    minSpeed;
        uint32_t _pad[2];
    };
    static_assert(sizeof(OrbEmitCB) == 16, "OrbEmitCB layout mismatch");
    // --- 固定ステップ ---
    float m_Accumulator = 0.0f;
    int   m_LastSubSteps = 0;
    uint32_t m_FrameSeed = 0;

    // --- リードバック ---
    GPUReadback m_Readback;
    float    m_PendingPlayerDamage = 0.0f;   // リードバックした分の未消費ぶん
    uint32_t m_LastKillCount = 0;            // 累計 counter の前回値（差分用）
    uint32_t m_LastDamageTotal = 0;


    uint32_t m_LastExpTotal = 0;
    float    m_PendingExp = 0.0f;


    // --- 計測 ---
    double   m_FlushMs = 0.0;
    uint32_t m_TotalRequested = 0;
    uint32_t m_TotalDispatched = 0;
    uint32_t m_TotalSteps = 0;
};
