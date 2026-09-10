// ============================================================
// ChaseAISystem.h
// 雑魚の移動決定。算法は steering の三規則:
//   seek（玩家への向心）+ separation（仲間との分離）
//   + avoid（地形からの回避。GridWorld を読むだけ）
//
// 雑魚は PhysicsSystem を通さない。
//   押し出し（めり込んでから深度を計算して戻す）は
//   「動的実体 × 全 collider」の二次増加になるため、
//   雑魚の数が増えると真っ先に潰れる。
//   代わりに「そもそも塞がったマスへ行かない」で解決する。
//   格子の読み取りは配列1回なので、地形が何個あっても値段が変わらない。
//
// 精度は格子（2m）まで落ちるが、雑魚が壁から半メートル浮いても
// 誰も気づかない。精確な物理は玩家だけが持てばよい。
//
// 書くもの: rb.velocity の x/z、tf.position.y、tf.rotation.y
// ============================================================
#pragma once

class Registry;
class GridWorld;

class ChaseAISystem
{
public:
    void Update(Registry& reg, const GridWorld& grid, float dt);

    // --- 調整（ImGui から触る）---
    float separationRadius = 1.2f;
    float separationPower = 4.0f;

    // 塞がったマスから押し返す力。壁に貼り付く前に曲がらせる
    float avoidPower = 6.0f;
    // 前方どれだけ先のマスを見るか（秒。速度 × これ で先読み距離）
    float lookAhead = 0.35f;

    // 雑魚の足元の高さ。地面が平らな間はこれで足りる
    float groundY = 0.0f;
};