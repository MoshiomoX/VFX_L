# Mesh 粒子实施计划（B：顶点源发射 / A：立方体渲染）

VFX_L — 2026-09-18 定稿。目标：**粒子从模型身体上冒出来**（受击火星、燃烧、死亡消散），以及**粒子画成小方块**。
不做体素化、不读三角形、不烤点表。发射源 = GPU 上已经存在的顶点 buffer。

---

## 0. 约束（全程有效）

- C++ 注释日语，`/utf-8`。HLSL 纯 ASCII 无 BOM
- 结构体 C++ ↔ HLSL 同步改，每个加 `static_assert(sizeof == N)`
- 新 `.hlsl` 固定动作：vcxproj → ShaderType / Model 5.0 → 输出路径 `$(OutDir)Shader/<dir>/%(Filename).cso` → Rebuild → 日志 `OK`
- 改 `.hlsli` 一律 Rebuild
- 每个 CS `Dispatch` 后必有 `UnbindSRVs` + `UnbindUAVs`
- 每一步 Build 通过、验证通过再做下一步

---

## 1. 设计一览

```
发射源（顶点 buffer，已在 GPU）
  ├ 静态 Model : Mesh 的 VB（加 SRV 绑定）
  └ 骨骼 Model : SkinnedModelGPU 的 skinnedVerts（每帧 SkinningCS 输出，已是 SRV）
        │
        ▼
GPUParticleSystem::RegisterEmitSource(srv, stride, count, layout) → sourceId
        │
        ▼
GPUEmitter { emitType = Mesh, sourceId, sourceCount, world(float4x4), edgeMode … }
        │
        ▼
EmitCS：按 sourceId 分组 dispatch，EmitMesh 随机挑顶点 → pos = mul(v.pos, world)
        │
        ├ 普通模式：所有顶点
        └ 边缘模式：EdgeFilterCS 先算 |noise(uv) - threshold| < edge 的顶点表 → 从表里挑
```

**顶点格式不统一**（`VERTEX_3D` 和 `SkinnedVertexOut` 布局不同）→ EmitCS 用 `ByteAddressBuffer` 按 `layout` 描述读位置 / 法线 / uv 的偏移，不用为每种格式写一个 CS。

---

## 2. 步骤

### ① 静态 Mesh 加 SRV + 源登记 + Emitter 加世界矩阵

**`Graphics/Mesh/Mesh.h/.cpp`**
- `Create` 的 VB：`BindFlags |= D3D11_BIND_SHADER_RESOURCE`，`MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS`
- 建一个 raw SRV（`DXGI_FORMAT_R32_TYPELESS`，`D3D11_BUFFEREX_SRV_FLAG_RAW`），`GetVertexSRV()`、`GetVertexCount()`
- `SkinnedModelGPU` 的 `skinnedSRV` 现在是 structured SRV，另建一个 raw SRV 给发射用（同一个 buffer 两种 view 可以共存；buffer 创建时加 `ALLOW_RAW_VIEWS`）

**`Particle/GPUParticleEmitter.h` / `ParticleCommon.hlsli`** —— `GPUEmitter` 扩：
```
float4x4 world;        // 64B  モデルの世界行列（回転・縮尺込み）
int  sourceId;         //      RegisterEmitSource が返す番号。-1 = 未設定
int  sourceCount;      //      頂点数
int  edgeMode;         //      0 = 全頂点 / 1 = 溶解の縁の頂点だけ
int  _padS;
```
- 现有的 `meshVertexOffset / meshVertexCount` 删掉，用上面的替代
- `static_assert(sizeof(GPUEmitter) == 原来 + 80)`
- `Update`（emitter 数×stride）的 Map/memcpy 不用改，`MAX_EMITTERS` 缓冲区按 `sizeof` 建的自动跟上

**`Particle/GPUParticleSystem.h/.cpp`** —— 源登记：
```cpp
struct EmitSourceLayout { uint32_t stride, posOffset, normalOffset, uvOffset; };
int  RegisterEmitSource(ID3D11ShaderResourceView* rawSRV, uint32_t vertexCount, const EmitSourceLayout& layout);
void UnregisterEmitSource(int id);
```
- 内部 `std::vector<EmitSource>`，id = 下标；`layout` 写进一个小 CB 或 `StructuredBuffer<EmitSourceLayout>`（后者，一次绑全部）
- 预置两种 layout 常量：`kLayoutStatic`（`VERTEX_3D`：stride 60？按实际 `sizeof(VERTEX_3D)` 填）、`kLayoutSkinned`（`SkinnedVertexOut` 48B：pos 0 / normal 16 / uv 32）

**`ParticleEmitCS.hlsl` + `EmitMesh.hlsli`**
- `meshVertices` 从 `StructuredBuffer<EmitMeshVertex>` 改成 `ByteAddressBuffer emitSource : register(t1)`，另加 `StructuredBuffer<EmitSourceLayout> sourceLayouts : register(t3)`
- `EmitMesh`：`idx = Random * sourceCount` → 按 layout 的偏移 `Load3` 位置 / 法线 → `pos = mul(float4(p,1), e.world).xyz`，`dir = normalize(mul(n, (float3x3)e.world))`
- **一次 dispatch 只能绑一个源** → `DispatchEmit` 改成：先跑一次"非 Mesh 的 emitter"，再按 `sourceId` 分组各跑一次（CB 里加 `g_ActiveSource`，EmitCS 里 `sourceId != g_ActiveSource` 的 Mesh emitter 跳过）。玩家 + 精英个位数，多几次 dispatch 无所谓
- `SwarmEmitCS` 不动（GPU 弹路径不支持 Mesh 发射，保持 `default: break`）

**验证**：CollisionTestScene 里临时给玩家挂一个 Mesh 发射的 emitter（sourceId = 玩家胶囊 Mesh），粒子应该从胶囊表面冒，玩家转身粒子跟着转。

### ② Editor：Shape = Mesh 时选源

**`VFX_Editor/VFXParticleEntry`**
- `emitterData` 加 `std::string sourceModelPath;`（json 字段 `"source"`）
- Inspector：Shape=Mesh 时显示 `VFXFileList::Combo("Source Model", Res::Dir::VFXMesh …)` + `Edge Mode` checkbox
- `OnPlay`：按路径 `LoadModel` → 取第 0 个 SubMesh 的 `GetVertexSRV/Count` → `RegisterEmitSource` → 填 `sourceId/sourceCount`；`world` 先用 effect 的 `worldOffset` 平移（旋转缩放 Identity）
- `OnStop`：`UnregisterEmitSource`

**游戏内的接法**（不在这步做，先记）：`ProjectileVFXSystem::AttachVFX` 那种"把 VFX 挂到实体"的地方，实体有 `ModelComponent` 就把它的 Mesh 登记为源、每帧把 `TransformComponent` 的世界矩阵写进 emitter。

**验证**：VFXEditor 里选 `Assets/VFX/Mesh/Slash.fbx` 当源，粒子沿 Slash 表面冒。

### ③ 骨骼源（每帧跟随姿势）

**`SkinnedModelGPU`**
- `CreateSubMeshBuffers` 的 skinned buffer 加 `ALLOW_RAW_VIEWS`，多建一个 raw SRV：`GetSkinnedRawSRV(i)`
- 4 个 submesh → 4 个源，或者拼成一个：先 4 个，Editor 里选 submesh

**`VFXEditorScene`**
- Reference Model 面板加 "Use as emit source" 按钮：把 Paladin 的 submesh 登记进 `m_ParticleSystem`，得到 id，写进当前选中的 Mesh entry
- `world` 每帧 = `m_ModelTransform.GetWorldMatrix()`——需要 entry 有"跟随目标"的概念：`VFXParticleEntry` 加 `const Matrix* followWorld`（非所有权指针），`ToGPU` 时若非空就用它

**验证**：Paladin 播动画，粒子从身体表面冒并跟着手臂动。关 Anim Play 粒子位置定住。

### ④ 溶解边缘顶点表（`EdgeFilterCS`）

**`Shader/Particle/EdgeFilterCS.hlsl`**（新）
- 输入：源 `ByteAddressBuffer`、layout、`Texture2D noise`、`g_Threshold / g_Edge / g_NoiseTiling / g_NoiseScroll`（和 `VFXMeshPS` 的溶解一模一样的式子）
- 输出：`AppendStructuredBuffer<uint> edgeIndices`（顶点下标）
- 一个线程一个顶点：`n = noise.SampleLevel(uv * tiling + scroll)`，`abs(n - threshold) < edge` → Append

**`GPUParticleSystem`**
- 每个源配一个 `edgeIndices` buffer（容量 = 顶点数）+ counter；`Flush` 里对 `edgeMode == 1` 且本帧有 emitter 引用的源跑一次 `EdgeFilterCS`，`CopyStructureCount` 把数量写进 `edgeCount` buffer（GPU 读，不回 CPU）
- `EmitMesh`：`edgeMode == 1` 时 `idx = edgeIndices[Random * edgeCount]`，`edgeCount == 0` 就不发

**溶解参数从哪来**：
- Editor：Mesh entry 的 dissolve 参数 + Particle entry 的 `edgeMode` 指向同一个 threshold——`VFXEffect` 里加一个"共享溶解进度"（`float dissolveProgress`，effect 级别，两个 entry 都读它）
- 游戏内：敌人死亡时走 `Material` 的溶解（还没做，属于"死亡表现"那条），到时 `EdgeFilterCS` 的参数从那里来

**验证**：VFXEditor 里 Mesh entry 溶解 + Particle entry edgeMode，粒子沿着燃烧边缘冒，边缘往上走粒子跟着走。

### ⑤ A：粒子画成立方体

**`GPUParticle`**（`ParticleCommon.hlsli` + C++）
- `rotation / angularVel` 从 1 轴扩成 3 轴：`float3 rot3; float3 angVel3;`（原来的两个 float 保留给广告牌用，新加 6 个 float，结构体 +32B，`_pad1` 那个空位一并用掉）
- `ParticleEmitCS` / `SwarmEmitCS`：`rot3 = RandomRange ×3`，`angVel3 = RandomRange ×3`（emitter 加 `angularVel3Range`，或先复用 `angularVelRange` 三轴同分布）
- `ParticleUpdateCS`：`rot3 += angVel3 * dt`

**`GPUEmitter`** 加 `int renderMode;`（0 billboard / 1 cube）→ `GPUParticle` 加 `uint renderMode`

**`ParticleUpdateCS` 的 aliveList 分桶**
- 现在 `aliveList` 一个 Append + `g_DrawArgs` 一份
- 改成两个：`aliveBillboard`、`aliveCube`，各自 `DrawArgs`；`Update` 末尾按 `renderMode` Append 到对应的
- `DrawIndirectBuffer` 建两份

**`Shader/Particle/ParticleCubeVS.hlsl`**（新）
- 顶点输入：单位立方体 Mesh（`PrimitiveBuilder::CreateCube`，VB/IB 常规）+ `SV_InstanceID`
- `p = particles[aliveCube[instanceId]]` → `world = Scale(size) × RotXYZ(rot3) × Translate(pos)` → 输出 `ModelCommon` 的 `VS_OUTPUT`，`Color = p.color`
- PS 用 `PS.hlsl`（Lambert，方块要有明暗）。`albedoTexture` 绑白
- `DrawIndexedInstancedIndirect` 一次（`DrawArgs` 布局改成 `IndexCountPerInstance / InstanceCount / …`）

**`GPUParticleSystem::Render`**：先广告牌（现有），再方块。方块 depth write 开还是关：**开**（它是实体），blend 关

**验证**：Editor 里 emitter `renderMode = Cube`，粒子是有明暗、会转的小方块。和 Mesh 发射叠加 = "身体碎成方块"。

---

## 3. 每步结束的状态

| 步 | 能看到什么 |
|---|---|
| ① | 游戏里玩家胶囊冒粒子，转身跟着转 |
| ② | Editor 里任意模型当源 |
| ③ | Paladin 动着冒粒子 |
| ④ | 粒子只沿溶解边缘冒 |
| ⑤ | 粒子是方块 |

---

## 4. 明确不做的

- 体素化、三角形面积加权、点表烤制（砍掉）
- `SwarmEmitCS`（GPU 弹）的 Mesh 发射
- 杂魚（4096 只）的骨骼源——它们没有 `SkinningCS`，将来 VAT 动画时另议
- 顶点分布均匀化（脸密腿稀先接受）
- 每帧从 GPU 回读任何数量（`edgeCount` 留在 GPU）

---

## 5. 待确认的数字

- `sizeof(VERTEX_3D)` 和各字段偏移（`kLayoutStatic` 要填对）
- `GPUEmitter` 扩容后的总大小（`MAX_EMITTERS` × stride 的上传量翻一点，无所谓）
- `MAX_EMIT_SOURCES`：先 16

---

## 6. 实施记录（2026-09-18，①〜⑤ 全部完成，Debug x64 构建通过）

与上文设计不一致、或上文未定的地方，以实际代码为准：

- **`kLayoutSkinned` = `{64, 0, 16, 48}`**，不是 48/32。`SkinnedVertexOut` 含 tangent，四段各 16B。`VFXEditorScene.cpp` 里有 `static_assert` 对着 `offsetof` 校验，写错直接编不过。`kLayoutStatic` = `{60, 0, 12, 36}`（`GPUParticleSystem.cpp` 同样校验）。
- **骨骼源不是「同一 buffer 两种 view」**：`skinnedBuffer` 是 STRUCTURED，不能再带 `ALLOW_RAW_VIEWS`。改为每个 submesh 配一个同尺寸的 raw 双子 `emitRawBuffer`，`SkinSubmesh` 末尾 `CopyResource`（仅 `emitSourceEnabled` 的 submesh）。粒子比骨骼晚一帧。
- **pass 号没有塞进 `GlobalCB`**（b0 与 `SwarmEmitCS`/`SwarmSystem` 共用，那边用自己的 16B 局部结构体写 b0，扩了会越界读）。EmitCS 单独开 `EmitPassCB : b2`，`EdgeFilterCS` 同样用 b2。
- **每个 Emit pass 前都重新 `CopyStructureCount`**：前一 pass 已 Consume，不刷新 deadCount 的话防波堤按旧值判定会下溢。
- **`Shader.cpp` 反射补了 `D3D_SIT_BYTEADDRESS`**，否则 `ByteAddressBuffer` 无法按名字 `SetSRV`。
- **Mesh 发射位置 = `mul(顶点, world) + position`**：`position` 仍然当额外平移用，所以 `VFXEffect` 的 `worldOffset` 对文件源照常生效；`followWorld` 非空（跟随参考模型）时不再加 `worldOffset`，否则双重平移。
- **溶解参数来源**：没有另加 effect 级 `dissolveProgress`，Particle entry 的 edgeMode 直接读同一 effect 里第一个 Mesh entry 的当前 threshold / edge / noise / tiling / scroll（`VFXMeshEntry::GetEdgeFilterParams`），时间轴本来就共享。
- **缘的判定与 `VFXMeshPS` 一致，是单侧带** `threshold <= n < threshold + edge`（PS 只画这一侧的燃烧边），不是 `abs(n - threshold) < edge`。
- **edgeCount == 0 时**：线程已经 Consume 了 dead list 槽位，无法退回，改为把该粒子 `lifetime = 0`，UpdateCS 下一帧回收、不会被画。
- **立方体的 DrawArgs 是 5 uint**，每帧用 `UpdateSubresource` 整体复位（不用 `ClearUnorderedAccessViewUint`，那个会把 5 个元素都写成同一个值）。PS 复用 `Shader/PS.hlsl`（Lambert），光照由 `GPUParticleSystem::SetLight` 每帧从 Renderer 传入。`rot3 / angVel3` 三轴共用 `rotationRange / angularVelRange`。
- 结构体尺寸：`GPUParticle` 192B、`GPUEmitter` 320B、`EmitSourceLayout` 16B、`EmitPassCB` 16B、`EdgeFilterCB` 32B。
- 顺手把 `GPUParticle.h / GPUParticleEmitter.h / .cpp / Mesh.h` 从 CP932 转成了 UTF-8（工程已全局 `/utf-8`）。
- 仮设验证：CollisionTestScene 的 Stress 面板「Emit from Player Mesh」（把玩家胶囊 Mesh 登记为源）。Editor / 实体接法稳定后可删（`UpdateMeshEmitTest` 与相关成员）。

### 待人工验证
1. 游戏场景：Stress 面板勾 Emit from Player Mesh → 粒子从胶囊表面沿法线冒、转身跟着转。
2. Editor：Particle entry Shape=Mesh，选 Source Model → 粒子沿模型表面；Render=Cube → 有明暗、会转的小方块。
3. Editor：Reference Model → SubMeshes → Use as emit source（先选中 Particle entry）→ Paladin 动着冒粒子，关 Anim Play 定住。
4. Editor：Mesh entry 开 Dissolve + Noise，Particle entry 勾 Edge Mode → 粒子只沿燃烧边缘冒。
5. VS 输出窗口无 `Forcing to NULL` / `HAZARD`；启动日志 `[LoadShaders] EdgeFilterCS / CubeVS / CubePS` 均 OK。

### 6.1 追加（2026-09-18 晚）：按三角形发射 + Editor 直接选参考模型

- 按顶点发射在胶囊上只出半球（圆柱段没有中间顶点）。改为 **按三角形发射**：`RegisterEmitSource` 多收 index buffer 的 raw SRV + index 数 + 每 index 字节数（2/4），`EmitSourceLayout` 扩到 32B（加 `triangleCount / indexBytes`）。`EmitMesh` 随机挑三角形、随机重心坐标插值位置与法线（不做面积加权，密度仍与三角形密度成正比）。没传 index 的源退回按顶点。Edge Mode 仍按顶点表（缘的表本来就是顶点下标）。
- `IndexBuffer::Create` 与 `SkinnedModelGPU` 的 index buffer 都加了 `SHADER_RESOURCE | ALLOW_RAW_VIEWS` 和 raw SRV。
- Editor：`VFXContext` 加 `refSources / refWorld`，`VFXEditorScene::Init` 把 Paladin 的每个 submesh 登记成源；Particle entry 的 Inspector（Shape=Mesh）多一个 **Source** 下拉：`File model` 或 `Ref: <submesh 名>`，选后自动 `SetExternalSource` 并跟随 Model Pos/Rot/Scale。Reference Model 面板的按钮保留，走同一批 id。

### 6.2 追加（2026-09-18 晚）：装到游戏实体上（MeshVFXSystem / DissolveComponent / 死亡燃烧）

- **`MeshVFXSystem`**（`ECS/System/`）：照 `ProjectileVFXSystem` 的样子。`Attach(reg, e, VFXId, ctx)` 克隆模板，把 `ModelComponent` 第 0 个 SubMesh 登记为源（按 `Model` 缓存，同模型共用一个槽），效果里 **Shape=Mesh 且 source 为空** 的 Particle entry 自动 `SetExternalSource` 并跟随实体世界矩阵（`MeshVFXComponent::world` 放 heap，地址稳定）。`Update` 每帧算世界矩阵、推进溶解、把溶解参数交给 `SetSourceEdgeParams`、消完后 `Destroy` 实体。
- **`DissolveComponent`** + `Shader/Common/Dissolve.hlsli`：`PS.hlsl` / `PBR_PS.hlsl` 都 include，b1 = `DissolveCB`、t5 = noise，公式同 `VFXMeshPS`。`Renderer::DrawMesh` 每次都写 b1（没溶解写 threshold = -1），`RenderSystem` 从组件填 `DissolveParams`。
- **死亡流程**：`CollisionTestScene::UpdateGameplay` 里 HP 归零的非玩家实体 → `StartBurn(DeathBurn, m_BurnDuration)`。精英默认无敌，Enemies 面板每个精英多了 **Burn** 按钮（去无敌 + HP 0）。`VFXId::DeathBurn` → `Assets/Data/VFXData/DeathBurn.json`（两条 Mesh+Edge entry：橙色火星广告牌 + 掉落的小方块）。
- 缘的 threshold = `DissolveComponent::progress`，PS 与粒子用同一个值，所以粒子始终贴着燃烧边。
- 雑魚（GPU swarm）没有 per-entity 模型，这套装不上；玩家死亡仍走 `PlayerStateSystem`，未接燃烧。
