// ============================================================
// ClothChainComponent.h
// 骨の鎖を Verlet で揺らす（マント。2026-10-04）。
//   根の段を anchorBone（上背）に固定し、残りのノードを重力 + 空気抵抗 + 風で動かす。
//   体の当たりはカプセル（骨 → 骨 + 半径）。
//   ClothChainSystem が毎フレーム動かし、結果を SkinnedAnimComponent::boneOverrides に書く。
//   長さは m（世界）、時間は秒
//
// 2026-10-23：横 columns 列の格子に（ユーザー「マントを複数の鎖に」）。
//   真ん中の列 = chainBones、他の列 = SkinnedModel::SplitClothChain が読み込み時に足した列の骨
//   （SkinnedModel::ClothColumnBoneName）。列どうしは横 / 斜め / 横の 1 つ飛ばしの距離で繋ぐ。
//   1 列（columns = 1、または列の骨が無いモデル）の時は前の 1 本鎖と同じで、
//   体の当たりは体の左右方向を無視して測る（鎖は布の中心線なので、横にずれた脚でも布の幅のどこかには当たる）。
//   格子の時は列ごとに本当の 3D で当てる（片脚が片側だけを押す、端が体の脇へ回り込む）
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <SimpleMath.h>

struct ClothChainCollider
{
    std::string boneA, boneB;   // カプセルの両端（骨の原点）
    float radius = 0.1f;        // m
    int a = -1, b = -1;         // 解決した骨番号（ClothChainSystem）
};

struct ClothChainComponent
{
    // ---- 設定 ----
    bool enabled = true;
    std::string anchorBone;               // 根を固定する骨（上背）
    std::vector<std::string> chainBones;  // 真ん中の列の骨、根 → 先。最後は長さの終点だけ（なければ前の骨の向きで延ばす）
    int columns = 1;                      // 横の列の数（奇数）。列の骨が見つからなければ 1 列で動かす
    std::vector<ClothChainCollider> colliders;

    float gravity = 9.8f;
    float drag = 1.2f;            // 空気抵抗（1/秒）。5 m/s で走ると約 30 度なびく
    float damping = 0.01f;        // 1 ステップ（1/120 秒）あたりの速度の減衰
    float stiffRoot = 0.12f;      // 元の形（上背に付いたままの形）へ戻す強さ。根 → 先で直線に減る
    float stiffTip = 0.0f;
    float bend = 0.25f;           // 縦の 1 つ飛ばしのノードの距離を保つ強さ（急な折れ曲がりを抑える）
    float sideStretch = 1.0f;     // 横の隣の距離を保つ強さ（格子の時）
    float shear = 0.5f;           // 斜めの距離を保つ強さ（菱形に潰れるのを抑える）
    float sideBend = 0.15f;       // 横の 1 つ飛ばしが縮んだ時に広げる強さ（小さいほど横に折れ / 襞が寄る）
    // はためき：ノードが空気に対して動く速さ × flutter の横 / 縦の揺さぶりを、根 → 先へ流れる波で足す（格子の時）。
    // 一様な風だと格子全体が 1 枚の板のまま傾くだけなので、布らしい波打ちを足す
    float flutter = 0.3f;
    float flutterFreq = 9.0f;     // rad/s
    float windMul = 1.0f;         // 世界の風（WindState、ClothChainSystem::wind）に掛ける倍率。2026-10-12：以前は布ごとに別の風を持っていた
    // 局所の風源（竜巻・渦・砂旋・突風。ClothChainSystem::localWind）に掛ける倍率（2026-10-22）。
    // 0.6 だと突風（0.8 秒）の間にマントの先が 0.13m しか動かず見えなかった → 1.2（windtoys 自動テストで見比べ）
    float localWindMul = 1.2f;
    int   iterations = 4;         // 拘束の反復
    float substep = 1.0f / 120.0f;
    float margin = 0.03f;         // 布の厚み（カプセル半径に足す）
    float groundOffset = 0.04f;   // 地面からの浮かせ

    // ---- 状態（ClothChainSystem）----
    bool ready = false;
    bool failed = false;
    int  anchor = -1;
    int  cols = 1;                                       // 実際に使う列の数
    int  rows = 0;                                       // 段の数（= chainBones の数）
    std::vector<int> bones;                              // [列 * rows + 段] の骨番号（無い = -1：終点など）
    std::vector<DirectX::SimpleMath::Matrix> bindG;      // スキニングした時の global（モデル空間）[列 * rows + 段]
    DirectX::SimpleMath::Matrix anchorBindInv;
    DirectX::SimpleMath::Vector3 rightModel;             // 体の左右（モデル空間）
    DirectX::SimpleMath::Vector3 backModel;              // 体の後ろ（モデル空間）
    struct Link { int a, b; float rest; int kind; };    // 距離の拘束（kind = ClothChainSystem の種類）
    std::vector<Link> links;                             // rest は最初の Step で目標の形から測る
    std::vector<DirectX::SimpleMath::Vector3> pos, prev; // ノード（世界）[列 * rows + 段]
    DirectX::SimpleMath::Vector3 lastRoot;
    float prevH = 1.0f / 120.0f;
    float time = 0.0f;
    float stepMs = 0.0f;                                 // 1 フレームの Step にかかった時間（パネル / 自動テスト用）
    float poseMs = 0.0f;                                 // その内、素のポーズを作るのにかかった時間

    int Node(int col, int row) const { return col * rows + row; }
    int CenterCol() const { return cols / 2; }
    // 自動テスト用：真ん中の列の根 / 先（世界）
    const DirectX::SimpleMath::Vector3& CenterRoot() const { return pos[Node(CenterCol(), 0)]; }
    const DirectX::SimpleMath::Vector3& CenterTip() const { return pos[Node(CenterCol(), rows - 1)]; }
};
