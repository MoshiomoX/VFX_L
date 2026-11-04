// ============================================================
// SandFootprints.h
// 砂漠でプレイヤーの足跡を砂の上に残す（2026-11-03、ユーザー「砂に足跡を付けたい」→ プレイヤーだけ）。
//   SandField の凹み（0.39m / マス）は丸い窪みにしかならないので、足の形は別に描く：
//   一歩ごとに 1 枚の板（靴の形を PS で切り抜き、下の色を暗くする）を地面に置く。左右交互・進む向き。
//   ・古い物から消える（life 秒）。風が砂を動かす所（SandField と同じ式 = 風源 + 天気の風の一部）では早く消える
//   ・濡れた砂（SandField の乾き具合）では濃く、舞っている所（.w）では薄く（VS で状態を読む）
//   ・草の上・水の中・岩の上（足が地面より高い）には付けない（シーンが Add の前に判断する）
// 雑魚と爆発の跡は今まで通り SandField の凹み
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <SimpleMath.h>
#include <memory>
#include <vector>

class VertexShader;
class PixelShader;
class CameraBase;
struct LocalWind;
struct WindState;

class SandFootprints
{
public:
    struct Settings
    {
        bool  enabled = true;
        float length = 0.38f;       // m（靴の長さ。既定のカメラ 14m では 10px ほど）
        float width = 0.17f;        // m
        float strength = 1.0f;      // 全体の効き（0〜1）
        // 見た目（2026-11-03 2 版、ユーザー「砂の質感が無い」→ 黒いスタンプをやめて、砂に押した凹みとして描く）。
        // 凹みと周りの盛り上がりを高さとして持ち、その傾きに太陽を当てる（日の当たる壁は明るく、反対側は影）
        float depth = 0.06f;        // m：凹みの深さ（壁の明暗の強さ）
        float berm = 0.010f;        // m：周りに押し出された砂の高さ（つま先の前が高い）
        float sunGain = 1.6f;       // 壁が日を受ける / 失う量
        float floorDark = 0.18f;    // 踏み固めた底の暗さ
        float grain = 0.30f;        // 砂粒のざらつき（世界の座標で固定、底と縁だけ）
        float life = 14.0f;         // 秒（風が無い時）
        float fadeTime = 4.0f;      // 終わりの何秒で薄れるか
        float windErode = 0.8f;     // 1/秒：風で砂が舞い切る所（lift 1）で消える速さ
        float wetMul = 1.5f;        // 濡れた砂では何倍濃いか
        float lift = 0.035f;        // 地面から浮かせる（m。地面の網目は 1m 刻みで高さ場より粗い）
        int   maxPrints = 160;
    };

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void Clear() { m_Prints.clear(); }

    // 一歩。pos = 足の下の地面、yawDeg = 進む向き、side = 0 右 / 1 左、normal = 地面の法線
    void Add(const DirectX::SimpleMath::Vector3& pos, float yawDeg, int side, const DirectX::SimpleMath::Vector3& normal);
    // 年を取らせる。風の式は SandField と同じ（liftWind / range / worldShare = SandField::Settings の dust*）
    void Update(float dt, const LocalWind* local, const WindState* world, float time,
        float liftWind, float range, float worldShare);
    // 地形の後に描く（深度は読むだけ、下の色に掛け算）。state = SandField::GetStateSRV（無ければ濡れ・舞いを見ない）、
    // sunDir = 太陽の方へ向く向き（LightBuffer の directionalLight.direction の逆）
    void Render(CameraBase& camera, ID3D11ShaderResourceView* state,
        const DirectX::SimpleMath::Vector2& mapOrigin, const DirectX::SimpleMath::Vector2& mapSize,
        const DirectX::SimpleMath::Vector3& sunDir);

    void DrawImGui();
    Settings& GetSettings() { return m_Settings; }
    int Count() const { return (int)m_Prints.size(); }   // TEMP-TEST

private:
    struct Print
    {
        DirectX::SimpleMath::Vector3 pos;
        DirectX::SimpleMath::Vector3 normal;
        float yaw = 0.0f;     // rad
        int   side = 0;
        float age = 0.0f;
        float erode = 0.0f;   // 0..1（風で崩れた分）
    };
    // GPU の 1 枚（SandFootprintVS の FootPrint と同じ並び、48B）
    struct GpuPrint
    {
        DirectX::SimpleMath::Vector3 pos; float yaw;
        DirectX::SimpleMath::Vector3 normal; float alpha;
        float side, length, width, pad;
    };
    bool CreateBuffer(int count);

    Settings m_Settings;
    std::vector<Print> m_Prints;
    std::vector<GpuPrint> m_Upload;

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    std::shared_ptr<VertexShader> m_VS;
    std::shared_ptr<PixelShader> m_PS;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Buffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_SRV;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_Multiply;   // 結果 = 下の色 × 出力
    int m_BufCount = 0;
};
