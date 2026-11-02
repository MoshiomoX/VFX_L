// ============================================================
// GrassFireField.h
// 草の延焼（2026-10-12。ユーザー：VORTEXEL の「熱伝搬」のうち、見た目だけを先に）。
//   フィールド全体を覆う 512² のテクスチャ（x = 温度、y = 残りの燃料 0〜1）を踏み跡と同じ 2 枚交互で毎フレーム
//   1 歩進める（Shader/Grass/GrassFireCS）：熱は風下へ流れ・隣へ広がり・冷め、燃料のある草のマスは
//   温度が ignite を越えると燃えて熱を出し、燃料を使う。冷えた焦げ跡はゆっくり生え直す。雨は早く消す。
//   熱源 = GPU の範囲（爆発・メテオ・火球の命中）。範囲の profile の "heat"（AreaProfile::heat）を
//   VFX レシピ毎の表（recipeHeat）にして渡す。CPU から出す範囲（vfxType 0）は熱源にならない。
//   見た目：GrassVS が燃料の減りで葉を黒く短くし、熱い所の先を橙に光らせる（GrassRenderer::SetFire）。
//   炎・煙・火の粉は GrassFireEmitCS が燃えているマスから出す（VFXId::GrassFire。発射は SwarmSystem が回す）。
//   2026-11-01 本編へ：枯れ草の帯だけが燃える（燃え方が地形で決まるので必ず止まる）、燃え跡は燃料が戻るまで再び燃えない、
//   燃えている草の上の敵に当たる（SwarmAreaDamageCS。プレイヤーは傷付かない）、冷たい範囲（氷の魔法）は火を消す。
//   遺跡は草が無いので何も燃えない。テクスチャの z = 今燃えている印（x / y は線形で読めるよう別にしてある）。
//
// 使い方（シーン）：Initialize → 地形の後 Build(grid)（作り直しの度にも）→ GPU の表の後 BuildHeatTable(vfxTable)
//   毎フレーム：Tick(dt)（止まっている間は呼ばない）、SetWind / SetRain、FillEmitInput → SwarmSystem::SetGrassFire、
//   描画で草の前に Step(範囲の SRV)、GrassRenderer::SetFire(GetSRV(), ignite)
// ============================================================
#pragma once
#include "World/WindState.h"
#include "Swarm/SwarmFireEmit.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <cstdint>
#include <vector>

class GridWorld;
class ComputeShader;
class SwarmVFXTable;

class GrassFireField
{
public:
    struct Settings
    {
        // 2026-11-01 から本編で既定の入（ユーザー：無限に燃えるので本編に出ていない → 枯れ草の帯で燃える所を限る）。
        // 起動時の VFXL_GRASS_FIRE=0（TEMP-TEST）で切る
        bool  enabled = true;
        // 枯れ草の帯：これが無い緑の草は燃えない = 緑が防火帯になり、火は 1 帯を燃やし切って止まる
        float dryCover = 0.32f;     // フィールドの何割が枯れ草か
        float dryScale = 16.0f;     // 帯の大きさ m（ノイズの波長。1 帯はこの 0.6〜1.2 倍くらい）
        float reburn = 0.7f;        // 燃え跡は燃料がここまで生え戻らないと再び燃えない（約 35 秒）。無いと火がいつまでも回り続ける
        // 燃えている草の上の敵（プレイヤーは傷付かない）
        float fireDps = 16.0f;      // 1 秒あたりのダメージ（魔法威力が掛かる）。立っている敵で火が過ぎるまでに 20 ほど
        float fireTick = 0.5f;      // ダメージの間隔（秒。数字が刻み続けないように）
        float ignite = 0.4f;        // 燃え始める温度
        float cool = 0.6f;          // 冷める速さ（1/秒）
        float diffuse = 1.2f;       // 隣へ広がる速さ（マス²/秒。0.25 × 60 より小さく）
        float downwind = 0.5f;      // 風速の何割で熱が風下へ流れるか
        float burnRate = 0.35f;     // 燃料の減り（1/秒）= 1 マスが燃え尽きるのに約 3 秒
        float release = 2.0f;       // 燃えているマスが出す熱（温度/秒）
        float regrow = 0.02f;       // 冷えた焦げ跡が生え直す速さ（1/秒）= 約 50 秒
        float rainCool = 3.0f;      // 雨 1 で冷めが何倍増えるか
        float sourceMul = 0.9f;     // 範囲の半径 × これ が火の付く半径
        float emitRate = 1.0f;      // 炎・煙・火の粉の量（GrassFire.json の率に掛ける）
        float emitRadius = 60.0f;   // カメラからこれより遠い火は粒子を出さない
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）：フィールドの位置と大きさ、枯れ草の帯（seed で決まる）、火を全部消す
    void Build(const GridWorld& grid, uint32_t seed);
    void Reset();
    // 範囲の profile の heat を VFX レシピ毎の表に（AreaProfileDB と SwarmVFXTable の後）。GrassFire のレシピ番号もここで引く
    void BuildHeatTable(const SwarmVFXTable& table);
    void SetActive(bool on) { m_Active = on; }   // 面に草が無ければ false（計算も粒子も止める）

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind) { m_Wind = wind; }
    void SetRain(float rain) { m_Rain = rain; }
    // 次の Step でこの円を焼く（パネル・自動テスト）
    void Ignite(const DirectX::SimpleMath::Vector3& pos, float radius, float heat = 1.2f);

    // 描画の中、草を描く前：温度・燃料を 1 歩進める。
    // areas / areaStates = SwarmSystem::GetAreaSRV / GetAreaStateSRV、ground = GrassRenderer::GetGroundSRV（a = 草）
    void Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates, ID3D11ShaderResourceView* ground);

    // 今の温度 / 燃料 / 燃えている印（読む側）。無効なら null
    ID3D11ShaderResourceView* GetSRV() const;
    // 枯れ草の帯（1 = 枯れ草）。草の色と CS が読む
    ID3D11ShaderResourceView* GetDrySRV() const { return m_DrySRV.Get(); }
    // レシピ毎の熱（負 = 冷やす。水面の氷も読む）
    ID3D11ShaderResourceView* GetHeatSRV() const { return m_HeatSRV.Get(); }
    // (x, z) は枯れ草か（自動テストが帯を探す）
    bool IsDry(float x, float z) const;
    DirectX::SimpleMath::Vector2 MapOrigin() const { return m_MapOrigin; }
    DirectX::SimpleMath::Vector2 MapSize() const { return m_MapSize; }
    // 燃えているマスからの粒子の入力（SwarmSystem::SetGrassFire）。出さない時 false
    bool FillEmitInput(GrassFireEmitInput& out, const DirectX::SimpleMath::Vector3& camPos,
        ID3D11ShaderResourceView* ground, ID3D11ShaderResourceView* height) const;

    // Terrain パネルの後の「Grass Fire」。player があれば「Ignite at player」ボタンが出る
    void DrawImGui(const DirectX::SimpleMath::Vector3* player);
    Settings& GetSettings() { return m_Settings; }
    bool Active() const { return m_Active && m_Settings.enabled && m_Ready; }

    // TEMP-TEST: 読み戻し（Map で GPU を待つ。自動テストだけ）：燃えているマス / 焦げたマス（燃料 < 0.5）の数
    bool DebugReadStats(uint32_t& burning, uint32_t& charred) const;

    static constexpr int kSize = 512;   // 辺（200m のフィールドで 0.39m / texel。踏み跡と同じ）

    // HLSL の GrassFireCB と同じ並び（96B）
    struct FireCB
    {
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector2 windDir; float windSpeed, dt;
        float ignite, cool, diffuse, downwind;
        float burnRate, release, regrow, rain;
        DirectX::SimpleMath::Vector3 stamp; float stampHeat;
        float sourceMul, texel, rainCool, reburn;
    };

private:
    void BuildDryMask(uint32_t seed);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_StepCS;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Tex[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV[2];
    int m_Cur = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_DryTex;              // 枯れ草の帯（R8。kSize²）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_DrySRV;
    std::vector<uint8_t> m_DryMask;                                // 同じ物の CPU 側
    uint32_t m_DrySeed = 0;
    float m_DryCoverBuilt = -1.0f, m_DryScaleBuilt = -1.0f;        // 同じ帯なら作り直さない（ステージ選択の切替）
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_HeatBuffer;             // レシピ毎の heat（StructuredBuffer<float>）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_HeatSRV;
    uint32_t m_FireVfx = 0;                                        // VFXId::GrassFire のレシピ番号（0 = 表に無い）
    mutable Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Staging;     // DebugReadStats 用

    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;
    bool  m_Ready = false;     // Build 済み
    bool  m_Active = true;
    float m_PendingDt = 0.0f;
    float m_Rain = 0.0f;
    WindState m_Wind;
    DirectX::SimpleMath::Vector3 m_Stamp;   // Ignite の円（xz, 半径）
    float m_StampHeat = 0.0f;               // 0 = 無し。Step で 1 回使って消す
    bool  m_ShowMap = false;                // パネルに温度 / 燃料の絵を出す

    Settings m_Settings;
};
