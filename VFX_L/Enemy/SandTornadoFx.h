#pragma once
// ============================================================
// SandTornadoFx.h
// 砂嵐の術師の砂旋（kEnemyKindTornado）の本体の見た目（2026-10-26、ユーザー「砂旋本体も見えるように」）。
// 砂旋そのものは GPU の雑魚（撃てない・狙われない・yaw の向きへ moveSpeed で直進・tornadoLife 秒で消える）で、
// 10-20 に筒のモデルを廃止してから見えるのは足元の砂埃（TornadoDust.json）と風に巻かれる物だけだった。
// 飛び方は CPU で完全に先読みできる（生まれた所・向き・速さ・寿命が決まっていて、減速も壁も受けない）ので、
// GPU を触らず CPU で同じ直線を追いかけて竜巻（Twister.json）と同じ作りの柱（SandTornado.json：longjuanfeng2.FBX の
// Mesh 条目 2 枚、砂色）を AreaVFXPlayer で動かす。出現 introTime 秒で足元から伸び上がり、寿命が尽きたら outroTime 秒で
// ほどける（VFXEffect::SetPresence）。全消し（KillAll）の時は場面が Clear を呼ぶ
// ============================================================
#include "Swarm/AreaVFXPlayer.h"

#include <functional>
#include <vector>

class SandTornadoFx
{
public:
    struct Look
    {
        float introTime = 0.35f;   // 生まれてから柱が伸び切るまでの秒
        float outroTime = 0.5f;    // 寿命が尽きてからほどけて消えるまでの秒（GPU の砂旋は寿命でもう消えている）
        // 足元の赤い輪（Boss / エリートの予告と同じ SwarmSystem::WarnCircle。場面が CollisionTestSceneElites.cpp でまとめて出す。
        // 2026-10-26 ユーザー「砂旋の下にも赤い輪を」）：半径 m（出現 / 消える度合いを掛ける）、中を塗る割合（0 = 縁だけ）
        float ringRadius = 1.6f;
        float ringFill = 0.5f;
    };
    Look look;

    // pos = 砂旋の位置（敵の位置、y は地面でなくてよい：毎フレーム groundHeight で置き直す）、yaw = 飛ぶ向き（rad、+Z が 0）
    void Spawn(AreaVFXPlayer& vfx, const VFXContext& ctx, const DirectX::SimpleMath::Vector3& pos, float yaw, float speed, float life);
    // 毎フレーム：同じ直線で進めて柱を動かし、出現 / 消える度合いを入れる
    void Update(float dt, AreaVFXPlayer& vfx, const std::function<float(float, float)>& groundHeight);
    // 全部止める（全消しの時）
    void Clear(AreaVFXPlayer& vfx);
    int Count() const { return (int)m_Insts.size(); }

    struct Inst
    {
        uint32_t handle = 0;
        DirectX::SimpleMath::Vector3 pos;      // 今の位置（y = 地面）
        DirectX::SimpleMath::Vector3 dir;
        float speed = 0.0f;
        float life = 0.0f;
        float age = 0.0f;
        float presence = 0.0f;                 // 出現 / 消える度合い 0〜1（Update が入れる。赤い輪の大きさにも使う）
    };
    const std::vector<Inst>& Instances() const { return m_Insts; }   // 赤い輪を出す側が読む

private:
    std::vector<Inst> m_Insts;
};
