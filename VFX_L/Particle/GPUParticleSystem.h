#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include <string>
#include "Particle/GPUParticleEmitter.h"

#include "Particle/ParticleDeadList.h"
#include "Graphics/Shader/VertexShader.h"
#include "Graphics/Shader/PixelShader.h"
#include "Graphics/Shader/ComputeShader.h"
#include "Camera/CameraBase.h"
#include "Graphics/Material/Texture.h"
#include "Graphics/Renderer/RenderStates.h"
#include "Graphics/Light/LightTypes.h"
using Microsoft::WRL::ComPtr;

class Model;
class Mesh;

// ============================================
// 溶解の縁フィルタの入力（発射源ごと、1 フレーム有効）
// VFXMeshPS の溶解と同じ noise / tiling / scroll / threshold / edge を渡す
// ============================================
struct EdgeFilterParams
{
    ID3D11ShaderResourceView* noiseSRV = nullptr;
    DirectX::SimpleMath::Vector2 noiseTiling = { 1, 1 };
    DirectX::SimpleMath::Vector2 noiseScroll = { 0, 0 };
    float threshold = 0.0f;
    float edge = 0.05f;
};

class GPUParticleSystem
{
public:
    GPUParticleSystem() = default;
    ~GPUParticleSystem() = default;

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context, uint32_t maxParticles);

    void Render();
    void ResetSystem();

    // --- 2段階方式 ---
    // 各 VFXEffect が自分の emitter を積む（1フレームに何度でも呼べる）
    void SubmitEmitters(const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);

    // 1フレームに1度だけ呼ぶ。積まれた全 emitter で Emit + Update を実行
    void Flush(float dt, float totalTime);

    void SetCamera(CameraBase* camera) { m_Camera = camera; }
    void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
    // 立方体粒子の Lambert 用。Render の前に毎フレーム渡す（渡さなければ既定の上方光）
    void SetLight(const LightBuffer& light) { m_Light = light; }

    void RegisterStaticColorKeys(const std::vector<ColorKey>& keys) { m_StaticColorKeys = keys; }
    size_t GetStaticColorKeyCount() const { return m_StaticColorKeys.size(); }
    ID3D11UnorderedAccessView* GetParticleUAV()  const { return m_ParticleUAV.Get(); }
    ID3D11UnorderedAccessView* GetDeadListUAV() { return m_DeadList.GetUAV(); }
    ID3D11ShaderResourceView* GetDeadCountSRV() const { return m_DeadCountSRV.Get(); }
    void RefreshDeadCount(ID3D11DeviceContext* ctx)
    {
        ctx->CopyStructureCount(m_DeadCountBuffer.Get(), 0, m_DeadList.GetUAV());
    }

    size_t GetPendingEmitterCount() const { return m_PendingEmitters.size(); }
    size_t GetMaxEmitters()         const { return MAX_EMITTERS; }
    size_t GetDroppedEmitterCount() const { return m_DroppedEmitters; }
    uint32_t GetMaxParticles()      const { return m_MaxParticles; }

    // ============================================
    // emitter を積む余地があるか
    //
    // 呼び出し側が重い収集処理（CollectAndDispatch）に入る前に
    // これを確認することで、積んでも捨てられるだけの計算を省ける。
    //
    // 計測根拠：投射物 4000 のとき Dropped が 2976 に達していた。
    // つまり VFX 収集の 3/4 は完全な無駄だった。
    // FPS は VFX ON で 27.6 / OFF で 84.2（投射物数は同じ 4000）。
    // ============================================
    bool HasEmitterSpace() const { return m_PendingEmitters.size() < MAX_EMITTERS; }

    // ============================================
    // 暫定実装（第5段階の ownerID 方式で置き換える）
    //
    // 毎フレームの ReadDeadCount（Map READ = GPU 待ち）を廃止したため、
    // 正確な生存数は GPU 上にしか無い。CPU は知らない。
    //
    // 0 を返すと VFXState_Finishing が即 Stopped へ飛んでしまい、
    // 粒子が空中に残ったまま演出が終わる。
    // よって「まだ居るかもしれない」= 1 を返し、Finishing の終了判定は
    // VFXStates 側の時間兜底（timeInState > 3.0f）に任せる。
    // 
    // この 2 つは必ずセットで扱う。片方だけ変えると状態機が壊れる。
    // ============================================
    uint32_t GetAliveCount() const { return 1; }

    // 初期化/リセット直後の値のまま。ImGui の目安表示にのみ使う。
    uint32_t GetDeadCount() const { return m_CurrentDeadCount; }

    // ============================================
    // Mesh 発射源の登録
    //
    // 発射源 = GPU 上に既にある頂点 buffer の raw SRV。
    // 静的 Mesh なら Mesh::GetVertexSRV()、骨格なら SkinningCS の出力。
    // 返る id を GPUEmitter::sourceId に入れる。-1 は失敗（枠が無い / SRV が null）。
    // SRV は参照を保持する（登録中に元 buffer が消えても落ちない）。
    // 使い終わったら Unregister で枠を返す
    // ============================================
    static const int MAX_EMIT_SOURCES = 16;
    // rawIndexSRV を渡すと三角形を選んで面上の点から出す（頂点の無い面にも出る）。
    // 無ければ頂点だけから出す。indexBytes は 2 か 4
    int  RegisterEmitSource(ID3D11ShaderResourceView* rawSRV, uint32_t vertexCount,
        const EmitSourceLayout& layout,
        ID3D11ShaderResourceView* rawIndexSRV = nullptr, uint32_t indexCount = 0, uint32_t indexBytes = 4);
    void UnregisterEmitSource(int id);
    bool IsEmitSourceValid(int id) const
    {
        return id >= 0 && id < (int)m_EmitSources.size() && m_EmitSources[id].used;
    }

    // よく使う頂点レイアウト。オフセットは .cpp の static_assert で実型と照合している
    // 三角形の欄（triangleCount / indexBytes）は RegisterEmitSource が埋めるので 0 のまま
    static constexpr EmitSourceLayout kLayoutStatic = { 60, 0, 12, 36, 0, 0, 0, 0 }; // VERTEX_3D
    static constexpr EmitSourceLayout kLayoutSkinned = { 64, 0, 16, 48, 0, 0, 0, 0 }; // SkinnedVertexOut（pos/normal/tangent/uv 各 16B）

    // ============================================
    // 溶解の縁の頂点表を今フレーム作る依頼（edgeMode == 1 の発射器を積む側が毎フレーム呼ぶ）
    // Flush 内で EdgeFilterCS を回し、数は GPU 上に置いたまま EmitCS が読む
    // ============================================
    void SetSourceEdgeParams(int sourceId, const EdgeFilterParams& params);

    // ============================================
    // 粒子の軌跡（帯）の見た目の登録
    //
    // 返る id + 1 を GPUEmitter::trailStyle に入れると、その発射器から出た粒子が
    // 1 個ずつ帯を引く。位置の記録も帯への展開も GPU 上で完結する。
    // -1 は失敗（枠が無い / 帯の資源が作れていない）。
    // 登録を解除すると、その style の帯は生きている粒子の分も含めて描かれなくなる
    // （特効の帯は自分の分の参照を持つので、消え終わるまで style が残る）
    // ============================================
    static const int MAX_TRAIL_STYLES = 32;
    int  RegisterTrailStyle(const ParticleTrailStyle& style);
    void UpdateTrailStyle(int id, const ParticleTrailStyle& style);
    void UnregisterTrailStyle(int id);
    bool IsTrailAvailable() const { return m_TrailReady; }

    // ============================================
    // 特効の位置で動く帯（VFX の Trail entry）
    //
    // 粒子の帯と違い、先頭は呼び出し側が毎フレーム渡す位置（特効の位置）。
    // CPU が上げるのは帯ごとに位置 1 つだけで、点の追加（minDistance ごと）・
    // 寿命切れ・帯への展開は GPU（EffectTrailCS / EffectTrailVS）。
    // 見た目は粒子の帯と同じ style 表（RegisterTrailStyle の id）。
    //
    // Release した帯はその場に残り、style の lifetime を使い切ってから枠が空く
    // （止めた時・瞬間移動した時に帯が急に消えない）。
    // Create の戻り値 -1 = 枠が無い / style が無効
    // ============================================
    static const int MAX_EFFECT_TRAILS = 256;
    int  CreateEffectTrail(int styleId, const DirectX::SimpleMath::Vector3& pos, float minDistance);
    void MoveEffectTrail(int id, const DirectX::SimpleMath::Vector3& pos, float minDistance);
    void ReleaseEffectTrail(int id);
    int  GetEffectTrailCount() const;   // 使用中の枠（追従中 + 消えかけ）。ImGui 用

    // ============================================
    // メッシュ粒子の模型表（GPUParticleMesh.cpp）
    //
    // 0 番は組み込みの立方体（常にある）。1.. はファイルの模型を登録して使う。
    // 模型は包囲ボックスで「最長辺 = 1、中心 = 原点」に揃えて描くので、
    // 粒子の size がそのまま最長辺の長さ（m）になる（ファイルの単位は気にしない）。
    // 同じ path は同じ番号を返し、参照を数える。
    // Release で 0 になっても、生き残った粒子が別の模型で描かれないよう
    // 数秒は番号を空けない（同じ path なら即座に戻れる）。
    // 戻り値は番号（ParticleRenderMode::Pack に渡す）。"" は 0（立方体）、枠が無い・読めない時も 0
    // ============================================
    int  RegisterParticleMesh(const std::string& path);
    void ReleaseParticleMesh(int slot);
    int  GetParticleMeshCount() const;   // 使用中の番号（立方体を含む）。ImGui 用

private:
    void Update(float deltaTime, float totalTime,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);
    bool CreateParticleBuffer(ID3D11Device* device);
    bool CreateEmitterBuffer(ID3D11Device* device);
    bool LoadShaders(ID3D11Device* device);
    bool CreateRenderStates(ID3D11Device* device);
    bool CreateColorKeyBuffer(ID3D11Device* device);
    bool CreateDrawIndirectBuffer(ID3D11Device* device);
    bool CreateAliveListBuffer(ID3D11Device* device, uint32_t maxParticles);
    bool CreateDeadCountBuffer(ID3D11Device* device);

    // 引数名は requestedEmit。
    // 「撃ちたい数」であって「撃てる数」ではない。
    // 空き数に合わせた clamp は shader 側が deadCount で行う。
    // requestedPlain = Mesh 以外の合計、requestedPerSource[i] = 発射源 i の合計
    void DispatchEmit(ID3D11DeviceContext* context, uint32_t requestedPlain,
        const uint32_t* requestedPerSource);
    // 1 pass = 1 Dispatch。activeSource < 0 は Mesh 以外の pass
    void DispatchEmitPass(ID3D11DeviceContext* context, int activeSource, uint32_t requestedEmit);
    void DispatchUpdate(ID3D11DeviceContext* context);

    bool CreateSourceLayoutBuffer(ID3D11Device* device);
    void UploadSourceLayouts(ID3D11DeviceContext* context);
    bool CreateEdgeBuffers(int sourceId);
    void DispatchEdgeFilter(ID3D11DeviceContext* context, int sourceId);
    // メッシュ粒子（GPUParticleMesh.cpp）
    bool CreateMeshResources(ID3D11Device* device);
    bool BuildMeshSlot(int slot, std::shared_ptr<Model> model, const std::string& path);
    void RenderMeshes(ID3D11DeviceContext* context);   // 光を受ける束 → 発光の束
    bool CreateTrailResources(ID3D11Device* device);
    void UploadTrailStyles(ID3D11DeviceContext* context);
    void DispatchTrail(ID3D11DeviceContext* context);   // UpdateCS の直後
    void RenderTrails(ID3D11DeviceContext* context);
    void AddRefTrailStyle(int id);

    // 特効の帯（GPUParticleEffectTrail.cpp）
    bool CreateEffectTrailResources(ID3D11Device* device);
    void DispatchEffectTrail(ID3D11DeviceContext* context);   // DispatchTrail の直後
    void RenderEffectTrails(ID3D11DeviceContext* context);    // RenderTrails の直後

    void UploadExternalEmitters(ID3D11DeviceContext* context,
        const std::vector<GPUEmitter>& emitters,
        const std::vector<ColorKey>& colorKeys);

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;

    uint32_t m_MaxParticles = 0;
    ComPtr<ID3D11Buffer>              m_ParticleBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_ParticleUAV;
    ComPtr<ID3D11ShaderResourceView>  m_ParticleSRV;

    // AliveList（DrawIndirect 用、存活粒子の index を格納）
    ComPtr<ID3D11Buffer>              m_AliveListBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_AliveListUAV;
    ComPtr<ID3D11ShaderResourceView>  m_AliveListSRV;

    // 空き数の GPU 内受け渡し（CopyStructureCount の受け皿。CPU は Map しない）
    ComPtr<ID3D11Buffer>              m_DeadCountBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_DeadCountSRV;

    static const int MAX_EMITTERS = 1024;
    ComPtr<ID3D11Buffer>               m_EmitterBuffer;
    ComPtr<ID3D11ShaderResourceView>   m_EmitterSRV;

    ParticleDeadList m_DeadList;

    // 初期化/リセット時にだけ更新される。毎フレームの回読は廃止した。
    uint32_t m_CurrentDeadCount = 0;

    std::shared_ptr<ComputeShader>   m_InitDeadListCS;
    std::shared_ptr<ComputeShader>   m_EmitCS;
    std::shared_ptr<ComputeShader>   m_UpdateCS;
    std::shared_ptr<ComputeShader>   m_EdgeFilterCS;
    std::shared_ptr<VertexShader>    m_RenderVS;
    std::shared_ptr<PixelShader>     m_RenderPS;

    // ---- メッシュ粒子（renderMode の下位 8bit != 0。GPUParticleMesh.cpp）----
    // UpdateCS が束（番号 × 光を受ける / 発光）ごとに aliveMesh の区画へ振り分け、
    // meshCounts[束] に数を積む。描画は束 × submesh ごとの DrawIndexedInstancedIndirect。
    // 数は GPU 上で各 submesh の args へ写す（CPU は数を知らない）
    std::shared_ptr<VertexShader>     m_MeshVS;          // ParticleMeshVS
    std::shared_ptr<PixelShader>      m_MeshLitPS;       // Shader/PS.hlsl（Lambert）
    std::shared_ptr<PixelShader>      m_MeshGlowPS;      // ParticleMeshGlowPS（加算）
    std::shared_ptr<Texture>          m_WhiteTexture;    // 貼图の無い submesh の albedo（色は粒子から）
    ComPtr<ID3D11Buffer>              m_AliveMeshBuffer; // kParticleMeshBuckets × kParticleMeshBucketCap
    ComPtr<ID3D11UnorderedAccessView> m_AliveMeshUAV;
    ComPtr<ID3D11ShaderResourceView>  m_AliveMeshSRV;
    ComPtr<ID3D11Buffer>              m_MeshCountBuffer; // kParticleMeshBuckets 個の uint
    ComPtr<ID3D11UnorderedAccessView> m_MeshCountUAV;
    LightBuffer                       m_Light;

    struct MeshSlot
    {
        bool  used = false;
        int   refs = 0;              // 0 番（立方体）は数えない
        float freeAt = 0.0f;         // refs が 0 になった後、この時刻を過ぎたら別の模型に使える
        std::string path;
        std::shared_ptr<Model> model;
        DirectX::SimpleMath::Vector3 center = { 0, 0, 0 };
        float invExtent = 1.0f;
        // submesh ごとの args（5 uint）。[0] = 光を受ける束 / [1] = 発光の束
        std::vector<ComPtr<ID3D11Buffer>> args[2];
    };
    MeshSlot m_MeshSlots[kParticleMeshSlots];

    // ---- Mesh 発射源（id = 添字）----
    struct EmitSource
    {
        ComPtr<ID3D11ShaderResourceView> srv;        // 頂点の raw view（ByteAddressBuffer）
        ComPtr<ID3D11ShaderResourceView> indexSRV;   // index の raw view。null なら頂点発射のみ
        uint32_t         vertexCount = 0;
        EmitSourceLayout layout = {};
        bool             used = false;

        // ---- 溶解の縁の頂点表（容量 = 頂点数）と、その数（CopyStructureCount の受け皿）----
        ComPtr<ID3D11Buffer>              edgeBuffer;
        ComPtr<ID3D11UnorderedAccessView> edgeUAV;      // Append
        ComPtr<ID3D11ShaderResourceView>  edgeSRV;
        ComPtr<ID3D11Buffer>              edgeCountBuffer;
        ComPtr<ID3D11ShaderResourceView>  edgeCountSRV;
        EdgeFilterParams                  edgeParams;
        bool                              edgeRequested = false;   // 今フレーム表を作るか
    };
    std::vector<EmitSource>          m_EmitSources;
    ComPtr<ID3D11Buffer>             m_SourceLayoutBuffer;   // StructuredBuffer<EmitSourceLayout>
    ComPtr<ID3D11ShaderResourceView> m_SourceLayoutSRV;
    bool                             m_SourceLayoutDirty = false;

    // フレーム内の積み上げ用（GPU バッファではなく CPU 側の一時領域）
    std::vector<GPUEmitter> m_PendingEmitters;
    std::vector<ColorKey>   m_PendingColorKeys;
    size_t                  m_DroppedEmitters = 0;   // 上限超過で捨てた数
    GlobalCB m_CachedGlobalCB = {};

    // ComPtr<ID3D11BlendState>        m_BlendState;
    // ComPtr<ID3D11DepthStencilState> m_DepthStencilState;
    // ComPtr<ID3D11RasterizerState>   m_RasterizerState;

    ComPtr<ID3D11Buffer> m_ColorKeyBuffer;
    ComPtr<ID3D11ShaderResourceView> m_ColorKeySRV;
    static const int MAX_COLOR_KEYS_TOTAL = 1024;

    // DrawIndirect 用
    ComPtr<ID3D11Buffer>              m_DrawIndirectBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_DrawIndirectUAV;

    // ---- 粒子の軌跡（帯）----
    // trailPoints : 粒子 1 個につき kTrailPoints 個の float3（固定対応）
    // trailAlive  : 帯を持つ生存粒子の index。TrailCS が積み、TrailVS が instance として引く
    // trailArgs   : DrawInstancedIndirect 用。[0] = 2 * (kTrailPoints + 1)、[1] = TrailCS が累加
    bool                              m_TrailReady = false;
    std::shared_ptr<ComputeShader>    m_TrailCS;
    std::shared_ptr<VertexShader>     m_TrailVS;
    std::shared_ptr<PixelShader>      m_TrailPS;
    ComPtr<ID3D11Buffer>              m_TrailPointsBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_TrailPointsUAV;
    ComPtr<ID3D11ShaderResourceView>  m_TrailPointsSRV;
    ComPtr<ID3D11Buffer>              m_TrailAliveBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_TrailAliveUAV;
    ComPtr<ID3D11ShaderResourceView>  m_TrailAliveSRV;
    ComPtr<ID3D11Buffer>              m_TrailArgsBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_TrailArgsUAV;
    ComPtr<ID3D11Buffer>              m_TrailStyleBuffer;   // StructuredBuffer<TrailStyle>（dynamic）
    ComPtr<ID3D11ShaderResourceView>  m_TrailStyleSRV;
    struct TrailStyleSlot
    {
        ParticleTrailStyle style;
        bool               used = false;
        int                refs = 0;       // 登録者 1 + それを使う特効の帯の数
    };
    std::vector<TrailStyleSlot>       m_TrailStyles;        // id = 添字
    bool                              m_TrailStylesDirty = false;

    // ---- 特効の帯 ----
    // anchors : 帯ごとの錨（dynamic、毎フレーム全枠を上げる）
    // states  : 帯ごとの状態（先頭・環の位置・今フレームの尾）。GPU だけが書く
    // points  : 帯ごとに kEffectTrailPoints 点の環
    // alive / args : 見えている帯の list と DrawInstancedIndirect の引数
    bool                              m_EffectTrailReady = false;
    std::shared_ptr<ComputeShader>    m_EffectTrailCS;
    std::shared_ptr<VertexShader>     m_EffectTrailVS;     // PS は m_TrailPS を共用
    ComPtr<ID3D11Buffer>              m_EffectAnchorBuffer;
    ComPtr<ID3D11ShaderResourceView>  m_EffectAnchorSRV;
    ComPtr<ID3D11Buffer>              m_EffectStateBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_EffectStateUAV;
    ComPtr<ID3D11ShaderResourceView>  m_EffectStateSRV;
    ComPtr<ID3D11Buffer>              m_EffectPointBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_EffectPointUAV;
    ComPtr<ID3D11ShaderResourceView>  m_EffectPointSRV;
    ComPtr<ID3D11Buffer>              m_EffectAliveBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_EffectAliveUAV;
    ComPtr<ID3D11ShaderResourceView>  m_EffectAliveSRV;
    ComPtr<ID3D11Buffer>              m_EffectArgsBuffer;
    ComPtr<ID3D11UnorderedAccessView> m_EffectArgsUAV;
    struct EffectTrailSlot
    {
        enum class State { Free, Attached, Fading };
        State   state = State::Free;
        int     styleId = -1;
        DirectX::SimpleMath::Vector3 position = { 0, 0, 0 };
        float   minDistance = 0.2f;
        bool    reset = false;       // 次の upload で環を空にして position から始める
        bool    moved = false;       // 今フレーム Move された
        float   fadeEnd = 0.0f;      // Fading：この時刻を過ぎたら枠を返す
    };
    std::vector<EffectTrailSlot>      m_EffectTrails;       // id = 添字
    bool                              m_EffectTrailDrawn = false;   // 今フレーム描く帯がある

    CameraBase* m_Camera = nullptr;
    std::shared_ptr<Texture> m_Texture;
    std::vector<ColorKey> m_StaticColorKeys;
    //   ComPtr<ID3D11SamplerState> m_SamplerState;
};