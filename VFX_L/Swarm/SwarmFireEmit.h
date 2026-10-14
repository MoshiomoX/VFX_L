// ============================================================
// SwarmFireEmit.h
// 燃えている草から粒子を出すための入力（2026-10-12）。GrassFireField が毎フレーム埋め、
// シーンが SwarmSystem::SetGrassFire で渡す。発射そのものは SwarmSystem（Swarm/SwarmSystemFire.cpp）が
// 弾・範囲の発射の直後に回す：空きの予約（emitBudget）を同じフレームで共有するため
// ============================================================
#pragma once
#include <d3d11.h>
#include <SimpleMath.h>
#include <cstdint>

struct GrassFireEmitInput
{
    ID3D11ShaderResourceView* fire = nullptr;     // GrassFireField の今の温度 / 燃料（R16G16F）
    ID3D11ShaderResourceView* ground = nullptr;   // GrassRenderer の地面テクスチャ（a = 草）
    ID3D11ShaderResourceView* height = nullptr;   // GrassRenderer の高さ場（R32F）
    DirectX::SimpleMath::Vector2 mapOrigin;
    DirectX::SimpleMath::Vector2 mapSize;
    DirectX::SimpleMath::Vector3 camPos;
    float    emitRadius = 60.0f;   // カメラからこれより遠い所は出さない
    float    ignite = 0.4f;        // 燃えていると見なす温度（GrassFireCS と同じ）
    float    rateMul = 1.0f;       // レシピの発射率に掛ける（燃えているマス 1 つあたり）
    uint32_t vfx = 0;              // VFXId::GrassFire のレシピ番号（SwarmVFXTable::IndexOf）。0 = 出さない
    float    heightOffset = 0.05f; // 地面からこれだけ上で出す
    uint32_t fireSize = 512;       // fire テクスチャの辺（スレッド数を決める）
};
