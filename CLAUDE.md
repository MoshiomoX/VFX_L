# VFX_L — Claude Code 用プロジェクト説明

## 回答の作法
- 回答は中国語、コード内コメントは日本語、HLSL のコメントは英語
- コメントに ★ などの記号を使わない。強調は ※、順序は数字
- Unity / Unreal の専用キー名は英語のまま
- 求められていない図・ファイル・アニメーションは作らない
- 新機能や要点に触れる時は「関键点」として明示する

## プロジェクトの宗旨
- 3D 自動戦闘 roguelite。見せ場は「組んだ魔法が一発の奇観として立ち上がる瞬間」
- Shader の計算は ComputeShader で最適化できるものは全てそうする
- 個人開発。エンジンから自作（DirectXTK / assimp / ImGui / nlohmann-json 以外）

## 技術的な硬い制約
- fxc は「BOM 無し + 純 ASCII」の HLSL しか受け付けない。日語コメント禁止
- .cso は $(OutDir)%(RelativeDir)%(Filename).cso に出る。Debug/Release とも .cso を読む
- 作業ディレクトリは $(OutDir)。Assets は junction でリンク
- ソースの実フォルダはフィルタ（ソリューションエクスプローラー）と同一構成。include はプロジェクトルート相対（例 #include "UI/UIManager.h"）。$(ProjectDir) を AdditionalIncludeDirectories に追加済み。Shader\ と ThirdParty\ は移動しない
- Registry の View 走査中に Create/Destroy しない。溜めて後処理
- 一つのデータは一つの System だけが書く（velocity.y は PhysicsSystem 専任）
- 粒子は Submit → Flush の2段階。Flush は1フレーム1回だけ
- 状態機（PlayerStateSystem）は PhysicsSystem の後に置く

## 現在の構成（2026-09 時点）
### エンジン層
- ECS: Entity / SparseSet / Registry / View（entt 風 sparse-set）
- 描画: Shader（reflection で cbuffer 自動）/ RenderStates（統一）
- GPU 粒子: 10万プール、dead list（Append/Consume）、DrawInstancedIndirect
  - CPU は毎フレーム回読しない。空き数は CopyStructureCount で GPU 上のバッファへ
  - EmitCS に deadCount ガード（256 スレッド粒度の溢れ対策）
  - GetAliveCount() は暫定で 1 を返す（第5段階 ownerID 方式で置換予定）
- 衝突: CPU 総当たり。collider 2042 個で 28.4fps。空間分割が課題
- UI: SpriteRenderer（実例化）/ NumberRenderer。TextRenderer は未実装（次の作業）
- UIManager: モーダル UI をスタックで排他管理。第1段階完了（2026-09）。SceneBase へ移す予定

### ゲーム層
- 呪文システム（BackpackComponent / BackpackLogic / BackpackUI）
  - 7x7 は画布の上限。魔法を置けるのは Frame が敷かれたマスだけ
  - 魔法と Frame は別の占有表（occupancy / frameOccupancy）
  - Frame を動かすと上の魔法も一緒に動く。足場を失ったものだけ手元へ戻る
  - ドラッグ&ドロップ。ドラッグ中はデータを書き換えない
  - 影響格は触れているブロック1つ分だけ表示
- SpellbookComponent: 習得数。使える数 = 所持数 - グリッド上の数（毎回数える）
- LevelComponent: 経験値・レベル・pendingChoices（三択の候補）
- ExpOrbSystem: 敵が死ぬと ExpRewardComponent に従ってオーブを落とす。CollisionSystem 不使用
- LevelUpSystem: 純ランダム3択、重複なし、Frame も池に入る
- PlayerFactory: プレイヤーの組み立てを集約。System は含めない
- PlayerStateSystem: 3層 HSM（Move / Action / Damage）、優先度 + 抑制マスク
- WeaponSystem: CastMode = Auto / Manual / DebugBurst。方向は常に索敵

## 既知の問題・保留
- BackpackUI::m_Spellbook は裸ポインタ。魔法書の持ち主が増えたら引数渡しへ
- BackpackUI::GRID_SIZE と BackpackComponent::GRID は static_assert で縛っている
- VFXStates の Finishing は時間兜底（GetAliveCount が 1 固定のため）。VFX 強化時に直す
- GPUCollider は未使用。将来の流体/RT 用に予約
- CopyStructureCount は毎フレーム約 0.076ms。命令キュー flush の固有コスト。放置

## 次にやること（順番）
1. TextRenderer（DirectXTK SpriteFont、Assets/Fonts/font_jp.spritefont で日本語表示）
2. HUD（HP / MP 左上、経験値バー最上段、グリッドは中央やや左）
3. 魔法書を正式 UI へ（グリッド左 + 魔法書右）
4. 敵 AI
5. 投射物の修正（追跡・貫通・索敵の選び方などまとめて）
6. VFXStates の Finishing 修正 + VFX エディタ強化