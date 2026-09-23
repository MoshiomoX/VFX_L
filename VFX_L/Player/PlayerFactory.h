// ============================================================
// PlayerFactory.h
// プレイヤー Entity の組み立てを1ヶ所に集める。
//
// 以前は CollisionTestScene::Init に 30 行ほど並んでいた。
// シーンが増えるたびに同じ組み立てを書き直すのを避ける。
//
// ※System は含めない。
//   PhysicsSystem / CollisionSystem は全 Entity 共通（プレイヤー専用ではない）。
//   PlayerControlSystem のようなプレイヤー専用の System も、
//   実行順は UpdateGameplay の並びで決まるもの
//   （ここで作ると順番が見えなくなる）。
//   System の所有は Scene の責任のまま残す。
// ============================================================
#pragma once
#include "ECS/Entity.h"
#include "ResourcePaths.h"
#include <SimpleMath.h>
#include <vector>

struct ID3D11Device;
class Registry;

namespace PlayerFactory
{
    using DirectX::SimpleMath::Vector3;
    using DirectX::SimpleMath::Vector4;

    // ============================================================
    // 組み立て時の設定
    // ※能力値は生成後は PlayerStatsComponent が真値になる。
    //   ImGui から触るのは Component の方で、この Config ではない。
    // ============================================================
    struct Config
    {
        Vector3 spawnPos = { 0.0f, 5.0f, 0.0f };

        float radius = 0.4f;
        float height = 1.0f;
        Vector4 color = { 0.3f, 0.6f, 1.0f, 1.0f };

        float moveSpeed = 5.0f;
        float jumpPower = 12.0f;

        float maxHealth = 100.0f;
        float maxMana = 100.0f;
        float manaRegen = 25.0f;

        // 杖とバックパックを付けるか
        // ※VFX だけ確認したいシーンでは false にする
        bool withWand = true;
        bool withBackpack = true;

        // ---- 見た目（骨付きモデル）----
        // nullptr / "" ならカプセル。読めなかった時もカプセルに落ちる
        const char* skinnedModel = Res::Mdl::KayKit_Mage;
        float modelScale = 1.0f;            // KayKit はメートル
        float modelYawOffsetDeg = 180.0f;   // KayKit は -Z が正面（Blender 出力）
        // 隠す手持ち品（ノード名）。Mage は杖・魔杖・魔道書が全部付いてくるので杖だけ残す
        std::vector<const char*> hiddenParts = { "1H_Wand", "Spellbook", "Spellbook_open" };
    };

    // ============================================================
    // プレイヤーを1体作る
    // 付くもの: Transform / Collider(Capsule) / Rigidbody / SkinnedAnim（無理なら Model）
    //          / PlayerTag / PlayerStats / PlayerState / Health / Mana
    //          / Level / Spellbook / Wand / Backpack（Config 次第）
    //
    // ※Wand は空で作る。spells / areas はグリッドの集約で埋まる。
    //   ここで魔法を入れると「グリッドが真値」という前提が壊れる。
    // ============================================================
    Entity Create(Registry& reg, ID3D11Device* device, const Config& cfg = {});

    // ============================================================
    // 体格や色を変えた時に衝突体と見た目を作り直す
    // 数値の出どころは PlayerStatsComponent（Config ではない）。
    //   ImGui で radius を触った後にこれを呼ぶ。
    // ============================================================
    void RebuildVisual(Registry& reg, Entity player, ID3D11Device* device,
        const Vector4& color);
}