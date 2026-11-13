// ============================================================
// WindGrid.h
// プレイヤーに付いて動く 3D の風の速さの格子（64×8×64、1 マス 1m）。
//   風の 3 層：天気の一様な風（WindState、外で足す）＋ この格子（動的な風）＋ 物の速さを引く（マントの相対風など）
//   毎フレーム（SwarmSystem::Flush の中、風源の表を作った直後）:
//     WindGridStepCS    : 移流 → 風源の表をモーターとして足す → 風源の無い所は減衰
//     WindGridDiffuseCS : diffuseIters 回ぼかす
//   読み手（草・落ち葉・粒子・水面・瘴気 / 砂塵・砂の面・タンポポ・草の踏み跡）は Shader/Common/WindGrid.hlsli の
//   WindGridBlend で式の風（WindSourceAir）を格子の風に置き換える（格子の外・縁では式の風へ戻る）。
//   CPU（マント・転がり草・草屑・F8 の流れ図）は下の数層を毎フレームリードバックした物を SampleCPU で引く（2〜3 フレーム遅れ）。
// 地形は見ない（風は地面の起伏・崖に遮られない）
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>

class ComputeShader;
class Shader;

class WindGrid
{
public:
    static constexpr int kX = 64, kY = 8, kZ = 64;   // 1 マス = settings.cell m
    static constexpr int kReadY = 5;                 // CPU へ写す下の層の数（足元 −2〜+3m）
    static constexpr int kStaging = 3;

    struct Settings
    {
        bool  enabled = true;
        float cell = 1.0f;            // 1 マスの m（変えたら Rebuild）
        float feetBelow = 2.0f;       // 格子の底 = 足元のこの m 下
        float decayTime = 0.35f;      // 風源の無い所の減衰の時定数（秒。e^-3 = 約 1 秒でほぼ 0）
        float relax = 20.0f;          // 風源の効く所で式の風へ寄せる速さ（1/秒）
        float motorGain = 1.0f;       // 風源の強さの倍率
        int   diffuseIters = 3;
        float diffuse = 0.12f;        // 1 回で 6 マスの平均へ寄せる割合
        float heightFull = 2.5f;      // 足元からこの m までは風源をそのまま
        float heightZero = 7.0f;      // この m で 0
        float advectWindShare = 0.5f; // 移流に足す天気の風の割合（残った風が風下へ流れる）
        float edgeFade = 6.0f;        // 読む側：格子の縁のこの m で式の風へ戻す
        float topFadeCells = 1.5f;    // 読む側：上の方のこのマス数で式の風へ戻す
        bool  cpuReadback = true;
    };
    Settings settings;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    bool Ready() const { return m_Ready; }

    // プレイヤーの足元と天気の風（PushWind の中、Flush の前）
    void SetCenter(const DirectX::SimpleMath::Vector3& feet, const DirectX::SimpleMath::Vector2& worldWindXZ);
    // 1 歩進める（風源の表を作った直後）。windSources = SwarmSystem の風源の表
    void Step(ID3D11ShaderResourceView* windSources, float dt);
    // 地形の作り直し・場面の切り替え：残っている風を捨てる
    void Reset() { m_PrevValid = false; }

    // 読み手のシェーダーへ g_WindGrid / g_WindGridInfo を結ぶ（g_WindSources を結ぶ所で一緒に呼ぶ）。
    // 格子が無い・切ってある時は無効の印（読み手は式の風のまま）
    void BindTo(Shader& shader, ID3D11DeviceContext* context) const;
    static void BindCurrent(Shader& shader, ID3D11DeviceContext* context);   // Current() が無ければ未バインド扱い

    // CPU：位置 p の格子の風。weight = 格子の値を使う割合（0 = 格子の外・まだ読めていない）
    DirectX::SimpleMath::Vector3 SampleCPU(const DirectX::SimpleMath::Vector3& p, float& weight) const;
    // CPU で式の風 formula を格子の風に置き換える（WindGridBlend の CPU 版）
    DirectX::SimpleMath::Vector3 BlendCPU(const DirectX::SimpleMath::Vector3& formula, const DirectX::SimpleMath::Vector3& p) const;
    // 足元の高さ（読む側が高さを持たない時に使う。CPU の 2D の読み手）
    float FeetY() const { return m_Feet.y; }

    ID3D11ShaderResourceView* GetSRV() const { return m_SRV[m_Cur].Get(); }
    DirectX::SimpleMath::Vector3 Origin() const { return m_Origin; }
    float LastStepMs() const { return m_LastStepMs; }

    void DrawImGui();

    // 今動いている格子（読み手のシステムへいちいち渡さないため。シーンが Initialize したもの）
    static WindGrid* Current() { return s_Current; }

private:
    void UpdateInfo();
    void CopyForCPU();
    void ReadCPU();

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    bool m_Ready = false;

    Microsoft::WRL::ComPtr<ID3D11Texture3D> m_Tex[2];
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV[2];
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_UAV[2];
    int m_Cur = 0;   // 最後に書いた方（読み手が読む方）

    Microsoft::WRL::ComPtr<ID3D11Buffer> m_InfoBuffer;   // float4 × 3（DYNAMIC）
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_InfoSRV;

    std::shared_ptr<ComputeShader> m_StepCS, m_DiffuseCS;

    DirectX::SimpleMath::Vector3 m_Feet = { 0.0f, 0.0f, 0.0f };
    DirectX::SimpleMath::Vector2 m_WorldWind = { 0.0f, 0.0f };
    DirectX::SimpleMath::Vector3 m_Origin = { 0.0f, 0.0f, 0.0f };
    DirectX::SimpleMath::Vector3 m_PrevOrigin = { 0.0f, 0.0f, 0.0f };
    bool m_PrevValid = false;
    bool m_HasCenter = false;
    float m_LastStepMs = 0.0f;

    // CPU の写し（下 kReadY 層、R16G16B16A16F）
    Microsoft::WRL::ComPtr<ID3D11Texture3D> m_Staging[kStaging];
    DirectX::SimpleMath::Vector3 m_StagingOrigin[kStaging];
    bool m_StagingUsed[kStaging] = {};
    int m_StagingNext = 0;
    std::vector<uint16_t> m_Cpu;   // kX × kReadY × kZ × 4（半精度のまま。引く時に float へ。毎フレーム全部変換すると Debug で 1ms を超えた）
    DirectX::SimpleMath::Vector3 m_CpuOrigin = { 0.0f, 0.0f, 0.0f };
    bool m_CpuValid = false;

    static WindGrid* s_Current;
};
