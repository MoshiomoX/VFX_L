// ============================================================
// SandField.h
// 砂漠の砂の面の状態（2026-11-02 ユーザー：「砂地の化学反応が少ない」→ 濡れた砂 / 熱い砂の陽炎 / 砂の跡）。
//   フィールド全体を覆う 512² の 2 枚（草の延焼と同じ作り。2 枚ずつ交互に読み書き）：
//     状態 R32G32B32A32：x = 温度、y = 乾き（1 = 乾いた砂、0 = 水を吸った砂）、z = 陽炎の印（熱くて乾いている）
//     跡   R32：砂の面の凹み（負 = 凹み）
//   毎フレーム Shader/Sand/SandFieldCS が 1 歩：熱は隣へ広がって冷め、濡れた砂は乾いていく（熱いと早い）、
//   凹みは風下へ運ばれ（風上から埋まる）・ぼけて・ゆっくり 0 へ戻る。
//   源 = GPU の範囲：範囲の profile の "heat"（AreaProfile::heat、草の延焼と同じ値）と "wet"（AreaProfile::wet）を
//   VFX レシピ毎の表にして渡す。凍らせる範囲（氷の魔法）は濡らして冷やす。爆発（SWARM_AREA_SHAKE）はクレーター。
//   プレイヤーの足跡は CB で、雑魚の足跡は Shader/Sand/SandTrackCS（同じ跡の図へ min で書く）。
//   見た目：地形の PS（TerrainPS）が状態（t3）と跡（t4）を読み、濡れた砂を暗く、熱い砂を赤く、凹みは法線を曲げる。
//   陽炎：GrassFireEmitCS をもう一度（燃えている草の代わりに陽炎の印）回す（SwarmSystem::SetSandHaze、VFXId::SandHaze）。
//   風に舞う砂（2026-11-03）：風源（竜巻・突風・流沙）と天気の風の一部が g_LiftWind より速い乾いた砂で w に強さ、
//     凹みも早く埋まる。GrassFireEmitCS の channel 1 が w を読んで砂粒と低い霞を出す（SetSandDust、VFXId::SandDust）。
//
// 使い方（シーン）：Initialize → 地形の後 Build(grid)（作り直しの度にも）→ GPU の表の後 BuildTable(vfxTable)
//   SetActive(砂漠か)。毎フレーム：Tick(dt)、SetWind、SetPlayer、FillEmitInput → SwarmSystem::SetSandHaze、
//   描画で地形の前に Step(...) → TerrainSurface::SetSandField(GetStateSRV(), GetTrackSRV(), ...)
// ============================================================
#pragma once
#include "World/WindState.h"
#include "Swarm/SwarmFireEmit.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <cstdint>

class GridWorld;
class ComputeShader;
class SwarmVFXTable;

class SandField
{
public:
    struct Settings
    {
        bool  enabled = true;
        float heatDiffuse = 1.0f;   // 熱が隣へ広がる速さ（マス²/秒。0.25 × 60 より小さく）
        float cool = 0.35f;         // 冷める速さ（1/秒）= 約 3 秒で 1/3
        float dryRate = 0.05f;      // 濡れた砂が乾く速さ（1/秒）= 約 20 秒
        float heatDry = 3.0f;       // 温度 1 で乾く速さが何倍増えるか（火で水たまりが乾く）
        float hazeHeat = 0.45f;     // これより熱くて乾いた砂から陽炎
        float trackFill = 0.04f;    // 凹みが戻る速さ（1/秒）= 約 25 秒
        float trackBlur = 0.08f;    // 凹みがぼける速さ（マス²/秒。0.3 だと足跡が広く滲んで形が無くなった）
        float trackWind = 0.15f;    // 風速の何割で凹みが風下へ運ばれるか
        float playerRadius = 0.35f; // プレイヤーの足跡（m）
        float playerDepth = 0.06f;  // m
        float mobRadius = 0.4f;     // 雑魚の足跡（m。大きい種類は広く）
        float mobDepth = 0.05f;     // m
        float craterDepth = 0.35f;  // 爆発の跡（m、真ん中）
        float sourceMul = 1.0f;     // 範囲の半径 × これ が砂に効く半径
        float hazeRate = 1.0f;      // 陽炎の量（SandHaze.json の率に掛ける）
        float emitRadius = 50.0f;   // カメラからこれより遠い陽炎は出さない
        // 風に舞う砂（2026-11-03、ユーザー：「風は砂を巻き上げられる？」）。乾いた砂の上で風が速い所から砂粒と低い砂の霞
        float dustLiftWind = 3.0f;  // これより速い風（m/s）で乾いた砂が動き出す（普段の風 1.2〜2 では動かない）
        float dustRange = 5.0f;     // そこから何 m/s 速いと最大か
        float dustWorldShare = 0.15f; // 天気の風（砂嵐）の分の重み。竜巻・突風・流沙などの風源は 1
        float dustRate = 1.0f;      // 量（SandDust.json の率に掛ける）
        float dustRadius = 30.0f;   // カメラからこれより遠い所は出さない（砂嵐でフィールド中から出ると重い）
        float trackWindFill = 6.0f; // 砂が舞う所では凹みがこの倍早く埋まる
        // 地面の見た目（TerrainSurface に渡す）
        float wetDark = 0.55f;      // 濡れた砂の明るさ
        float heatTint = 0.6f;      // 熱い砂の赤み（0.35 だと砂の色に紛れて見えなかった）
        float trackStrength = 2.5f; // 凹みの法線の曲がり（トゥーンの 2 段では 1 だと見えない）
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    // 地形の後（作り直しの度にも）：フィールドの位置と大きさ、全部を乾いた平らな砂に
    void Build(const GridWorld& grid);
    void Reset();
    // 範囲の profile の heat / wet を VFX レシピ毎の表に（AreaProfileDB と SwarmVFXTable の後）。陽炎のレシピもここで引く
    void BuildTable(const SwarmVFXTable& table);
    void SetActive(bool on) { m_Active = on; }   // 砂漠だけ

    void Tick(float dt) { m_PendingDt += dt; }
    void SetWind(const WindState& wind, float time);
    // プレイヤーの足の位置。grounded = 地面に着いている（足跡を付ける）
    void SetPlayer(const DirectX::SimpleMath::Vector3& feet, bool grounded) { m_Player = feet; m_PlayerGrounded = grounded; }
    // 次の Step でこの円を熱く / 濡らす（パネル・自動テスト）。heat 0 = 温度はそのまま、wet 0 = 乾きはそのまま
    void Stamp(const DirectX::SimpleMath::Vector3& pos, float radius, float heat, float wet);

    // 描画の中、地形を描く前に 1 歩。areas / areaStates = SwarmSystem::GetAreaSRV / GetAreaStateSRV、
    // enemies / enemyStates / enemyExtra = 雑魚の足跡、height = GrassRenderer::GetHeightSRV、groundY = AICB.groundY
    // windSources = SwarmSystem::GetWindSourceSRV（風に舞う砂）
    void Step(ID3D11ShaderResourceView* areas, ID3D11ShaderResourceView* areaStates,
        ID3D11ShaderResourceView* enemies, ID3D11ShaderResourceView* enemyStates, ID3D11ShaderResourceView* enemyExtra,
        ID3D11ShaderResourceView* height, float groundY, ID3D11ShaderResourceView* windSources);

    ID3D11ShaderResourceView* GetStateSRV() const;   // 無効なら null
    ID3D11ShaderResourceView* GetTrackSRV() const;
    DirectX::SimpleMath::Vector2 MapOrigin() const { return m_MapOrigin; }
    DirectX::SimpleMath::Vector2 MapSize() const { return m_MapSize; }
    // 陽炎の入力（SwarmSystem::SetSandHaze）。出さない時 false
    bool FillEmitInput(GrassFireEmitInput& out, const DirectX::SimpleMath::Vector3& camPos, ID3D11ShaderResourceView* height) const;
    // 風に舞う砂の入力（SwarmSystem::SetSandDust、channel 1）。出さない時 false
    bool FillDustInput(GrassFireEmitInput& out, const DirectX::SimpleMath::Vector3& camPos, ID3D11ShaderResourceView* height) const;

    void DrawImGui(const DirectX::SimpleMath::Vector3* player);
    Settings& GetSettings() { return m_Settings; }
    bool Active() const { return m_Active && m_Settings.enabled && m_Ready; }

    // TEMP-TEST: 1 点の読み戻し（Map で GPU を待つ。自動テストだけ）
    bool DebugReadAt(const DirectX::SimpleMath::Vector3& pos, float& temp, float& dry, float& depth, float* lift = nullptr) const;

    static constexpr int kSize = 512;

    // HLSL の SandCB と同じ並び（144B）
    struct SandCB
    {
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        DirectX::SimpleMath::Vector2 wind; float dt, texel;
        float heatDiffuse, cool, dryRate, heatDry;
        float hazeHeat, trackFill, trackBlur, trackWind;
        DirectX::SimpleMath::Vector3 player; float playerOn;
        float playerRadius, playerDepth, craterDepth, sourceMul;
        DirectX::SimpleMath::Vector4 stamp;
        float stampWet, liftWind, liftRange, worldLift;
        float trackWindFill, pad[3];
    };
    // HLSL の SandTrackCB（32B）
    struct TrackCB
    {
        DirectX::SimpleMath::Vector2 mapOrigin, mapSize;
        float radius, depth, groundY, on;
    };

private:
    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<ComputeShader> m_StepCS;
    std::shared_ptr<ComputeShader> m_TrackCS;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Tex[2], m_TrackTex[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV[2], m_TrackSRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV[2], m_TrackUAV[2];
    int m_Cur = 0;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_TableBuffer;            // レシピ毎の (heat, wet)
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_TableSRV;
    uint32_t m_HazeVfx = 0;                                        // VFXId::SandHaze のレシピ番号
    uint32_t m_DustVfx = 0;                                        // VFXId::SandDust のレシピ番号
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_WaterSRV;   // 池のマス（R8、1 = 水。濡れたまま・舞わない）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_WhiteSRV;   // 陽炎の「草がある」の代わり（1×1 の白）
    mutable Microsoft::WRL::ComPtr<ID3D11Texture2D> m_Staging, m_TrackStaging;

    DirectX::SimpleMath::Vector2 m_MapOrigin, m_MapSize;
    bool  m_Ready = false;
    bool  m_Active = false;
    bool  m_NeedClear = false;
    float m_PendingDt = 0.0f;
    DirectX::SimpleMath::Vector2 m_Wind;
    DirectX::SimpleMath::Vector3 m_Player;
    bool  m_PlayerGrounded = false;
    DirectX::SimpleMath::Vector4 m_Stamp;   // xz, 半径, 熱
    float m_StampWet = 0.0f;
    bool  m_StampOn = false;
    bool  m_ShowMap = false;

    Settings m_Settings;
};
