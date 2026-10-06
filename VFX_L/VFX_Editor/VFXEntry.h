#pragma once
#include "VFX_Editor/EntryType.h"
#include <nlohmann/json.hpp>
#include <SimpleMath.h>
using json = nlohmann::json;

class VFXEntry
{
public:
    virtual ~VFXEntry() = default;

    virtual EntryType GetType() const = 0;
    virtual void OnPlay(const VFXContext& ctx) = 0;
    virtual void OnStop(const VFXContext& ctx) = 0;
    virtual void OnUpdate(float dt, const VFXContext& ctx) = 0;
    virtual void OnImGui() = 0;
    virtual std::unique_ptr<VFXEntry> Clone() const = 0;

	virtual  json ToJson() const = 0;
	virtual  void FromJson(const json& j) = 0;
    // 色を掛ける（複製した直後、再生前に 1 回。水晶玉の光球を貯蔵した魔法の色で染める。2026-10-06）。
    // 粒子（開始 / 終了色の RGB）と点光源だけが応える。他の entry は何もしない
    virtual void Tint(const DirectX::SimpleMath::Vector3&) {}
    float startTime = 0.0f;
    float duration = -1.0f;
    bool isPlaying = false;
};