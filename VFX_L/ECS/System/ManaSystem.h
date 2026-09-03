// ============================================================
// ManaSystem.h
// 魔力の結算。予約された消費を引き落とし、回復を加える。
//
// 呼ぶ位置は WeaponSystem の後。
//   予約が全部揃ってから一度に結算する。
//   前に置くと今フレームの消費が次フレームに回り、
//   1フレーム分だけ多く撃てる。
//
// ManaComponent::current を書くのはこの System だけ。
//   回復・消費・将来の薬や成長、全部ここを通す。
// ============================================================
#pragma once

class Registry;

class ManaSystem
{
public:
    void Update(Registry& reg, float dt);
};