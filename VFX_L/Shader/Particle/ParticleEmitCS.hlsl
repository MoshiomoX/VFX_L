    #include "Common/ParticleCommon.hlsli"
    #include "Common/EmitPoint.hlsli"
    #include "Common/EmitSphere.hlsli"
    #include "Common/EmitCone.hlsli"
    #include "Common/EmitBox.hlsli"
    #include "Common/EmitRing.hlsli"
    #include "Common/EmitDisc.hlsli"
    #include "Common/EmitMesh.hlsli"

    // --- Buffer宣言 ---
    StructuredBuffer<GPUEmitter> emitters : register(t0);
    ByteAddressBuffer emitSource : register(t1); // メッシュ発射ソース（g_ActiveSource の生の頂点バッファ）
    Buffer<uint> deadCount : register(t2); // CopyStructureCount のコピー先
    StructuredBuffer<EmitSourceLayout> sourceLayouts : register(t3); // 登録したソース 1 つにつき 1 要素
    StructuredBuffer<uint> edgeIndices : register(t4); // g_ActiveSource のディゾルブ縁の頂点表（EdgeFilterCS）
    Buffer<uint> edgeCount : register(t5); // その表の CopyStructureCount
    ByteAddressBuffer emitIndices : register(t6); // g_ActiveSource の生のインデックスバッファ（三角形サンプリング用）
    RWStructuredBuffer<GPUParticle> particles : register(u0);
    ConsumeStructuredBuffer<uint> deadList : register(u1);

    // --- 発射パス ---
    // 1 回のディスパッチで束縛できるソースバッファは 1 本だけなので、メッシュエミッタは
    // ソースごとに 1 回ずつディスパッチする。g_ActiveSource < 0 = メッシュ以外のパス。
    // GlobalCB (b0) は SwarmEmitCS と共用なので、これは別の cbuffer に置く。
    cbuffer EmitPassCB : register(b2)
    {
        int g_ActiveSource;
        int3 g_EmitPassPad;
    };

    bool InThisPass(GPUEmitter e)
    {
        bool isMesh = (e.emitType == 6);
        if (g_ActiveSource < 0)
            return !isMesh;
        return isMesh && (e.sourceId == g_ActiveSource);
    }

    [numthreads(256, 1, 1)]
    void main(uint3 id : SV_DispatchThreadID)
    {
        uint globalIndex = id.x;

        // ============================================
        //   空き数を超えるスレッドは Consume させない。
        //   CPU 側で発射数を絞っても、Dispatch は 256 スレッド粒度なので必ず溢れる。
        //   溢れた分が Consume するとカウンタがアンダーフロー（uint 回り込み）して
        //   dead list が静かに壊れる。落ちないので気付けない。
        //   ここが唯一の防波堤。
        // ============================================
        if (globalIndex >= deadCount[0])
            return;

        // 各EmitterのemitCountを累積して、自分がどのEmitterの何番目かを特定
        int emitterIndex = -1;
        int localIndex = 0;
        uint accumulated = 0;

        for (int i = 0; i < g_EmitterCount; i++)
        {
            // ※CPU側の判定（isActive > 0.5f）と表記を揃える。
            //   元は !isActive > 0.5 で、bool→float 昇格により結果は同じだったが
            //   読み手を騙す書き方だった。
            if (emitters[i].isActive < 0.5)
                continue;

            // 他のパスのエミッタはこのディスパッチのスレッドを持たない
            if (!InThisPass(emitters[i]))
                continue;

            if (globalIndex < accumulated + (uint) emitters[i].emitCount)
            {
                emitterIndex = i;
                localIndex = globalIndex - accumulated;
                break;
            }
            accumulated += (uint) emitters[i].emitCount;
        }

        // どのEmitterにも属さない → 何もしない
        if (emitterIndex < 0)
            return;

        GPUEmitter e = emitters[emitterIndex];

        // Dead Listから空きインデックスを取得
        uint particleIndex = deadList.Consume();

        // 乱数シード初期化（フレーム毎に異なる + スレッド毎に異なる）
        uint seed = g_BaseSeed + globalIndex * 1099;

        // 形状に応じて発射位置と速度を決定
        float3 pos = e.position;
        float3 vel = float3(0, 0, 0);
        bool emitted = true; // メッシュの縁モードでは発射元が無いことがある

        switch (e.emitType)
        {
            case 0:
                EmitPoint(e, seed, pos, vel);
                break;
            case 1:
                EmitSphere(e, seed, pos, vel);
                break;
            case 2:
                EmitCone(e, seed, pos, vel);
                break;
            case 3:
                EmitBox(e, seed, pos, vel);
                break;
            case 4:
                EmitRing(e, seed, pos, vel);
                break;
            case 5:
                EmitDisc(e, seed, pos, vel);
                break;
            case 6:
                EmitMesh(e, seed, emitSource, emitIndices, sourceLayouts[e.sourceId],
                         edgeIndices, edgeCount[0], pos, vel, emitted);
                break;
        }

        // sweep emit: このフレームにエミッタが通った経路に沿ってばらまく。
        // 専用のシードを使い、下の属性の乱数列は変えない
        uint sweepSeed = seed ^ 0x9E3779B9u;
        pos += e.sweep * Random(sweepSeed);

        // パーティクル初期化
        GPUParticle p = (GPUParticle) 0;

        p.position = pos;
        p.velocity = vel;
        p.acceleration = e.gravity;
        p.drag = e.dragCoeff;

        p.lifetime = RandomRange(seed, e.lifetimeRange.x, e.lifetimeRange.y);
        p.age = 0.0;
        p.isAlive = 1.0;

        // 発射元が無い：dead list のスロットはもう Consume 済みなので、
        // 寿命 0 のパーティクルにする。UpdateCS が次のフレームで
        // 一度も描かずに返す。
        if (!emitted)
            p.lifetime = 0.0;

        p.startColor = RandomColor(seed, e.startColorMin, e.startColorMax);
        p.endColor = RandomColor(seed, e.endColorMin, e.endColorMax);
        p.color = p.startColor;

        p.startSize = RandomRange(seed, e.sizeRange.x, e.sizeRange.y);
        p.endSize = RandomRange(seed, e.sizeRange.z, e.sizeRange.w);
        p.size = p.startSize;

        p.rotation = RandomRange(seed, e.rotationRange.x, e.rotationRange.y);
        p.angularVel = RandomRange(seed, e.angularVelRange.x, e.angularVelRange.y);

        // キューブ：3 軸とも同じ範囲を使う
        p.renderMode = e.renderMode;
        p.rot3.x = RandomRange(seed, e.rotationRange.x, e.rotationRange.y);
        p.rot3.y = RandomRange(seed, e.rotationRange.x, e.rotationRange.y);
        p.rot3.z = RandomRange(seed, e.rotationRange.x, e.rotationRange.y);
        p.angVel3.x = RandomRange(seed, e.angularVelRange.x, e.angularVelRange.y);
        p.angVel3.y = RandomRange(seed, e.angularVelRange.x, e.angularVelRange.y);
        p.angVel3.z = RandomRange(seed, e.angularVelRange.x, e.angularVelRange.y);

        p.seed = seed;

        p.textureIndex = e.textureIndex;
        p.atlasRows = e.atlasRows;
        p.atlasCols = e.atlasCols;
        InitParticleFrame(e.frameMode, e.atlasIndex, e.frameCount,
                          e.atlasRows, e.atlasCols, seed, p.uvFrame, p.atlasAnimate);
        p.colorKeyOffset = e.colorKeyoffset;
        p.colorKeyCount = e.colorKeyCount;

        // 所有者を引き継ぐ。0 = 所有者なし（弾のエフェクト）。
        p.ownerID = e.ownerID;

        // リボントレイル：スタイルはパーティクルが持つ。リングは空から始める
        // （このスロットの前の持ち主の古い点は読まれない）
        p.trailStyle = (uint) max(e.trailStyle, 0);
        p.trailState = 0u;

        particles[particleIndex] = p;
    }