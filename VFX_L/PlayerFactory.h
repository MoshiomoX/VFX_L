// ============================================================
// PlayerFactory.h
// プレイヤー Entity の組み立てを1ヶ所に集める。
//
// 従来は CollisionTestScene::Init に 30 行以上散っており、
// シーンを増やすたびに全部書き写す必要があった。
//
// ※System は含めない。
//   PhysicsSystem / CollisionSystem は全体共有（敵・投射物・地形も使う）で
//   プレイヤーの持ち物ではない。PlayerControlSystem だけを包んでも
//   「半分だけ隠れている」状態になり、UpdateGameplay の実行順注釈
//   （操作→衝突→物理→状態機→杖→…）が読めなくなる。
//   System の呼び出しは Scene に見える形で残す。
// ============================================================
#pragma once
#include "Entity.h"
#include <SimpleMath.h>

struct ID3D11Device;
class Registry;

namespace PlayerFactory
{
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;

    // ============================================================
    // 生成時の指定。
    // ※ここは「初期値」。実行時の真実は PlayerStatsComponent 側。
    //   ImGui で弄るのも Component であり、この構造体ではない。
    // ============================================================
    struct Config
    {
        Vector3 spawnPos = { 0.0f, 5.0f, 0.0f };

        float radius = 0.4f;
        float height = 1.0f;
        Vector4 color = { 0.3f, 0.6f, 1.0f, 1.0f };

        float moveSpeed = 5.0f;
        float jumpPower = 8.0f;

        float maxHealth = 100.0f;

        // 杖とバックパックを付けるか
        // ※VFX 確認用のシーンなど、戦闘が不要な場合に false
        bool withWand = true;
        bool withBackpack = true;
    };

    // ============================================================
    // プレイヤーを1体作る。
    // 付くもの：Transform / Collider(Capsule) / Rigidbody / Model
    //          / PlayerTag / PlayerStats / PlayerState / Health
    //          / Wand / Backpack（Config 次第）
    //
    // ※Wand は空で作る。spells / areas はバックパック集約の結果で埋まる。
    //   ここで数値を書かないことで「配置しなければ何も撃たない」を保証する。
    // ============================================================
    Entity Create(Registry& reg, ID3D11Device* device, const Config& cfg = {});

    // ============================================================
    // 体格や色を変えた後にメッシュと衝突体を作り直す。
    // ※寸法の出処は PlayerStatsComponent。Config は見ない。
    //   ImGui で radius を弄った後にこれを呼ぶ、という使い方を想定。
    // ============================================================
    void RebuildVisual(Registry& reg, Entity player, ID3D11Device* device,
        const Vector4& color);
}