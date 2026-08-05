#pragma once
#include "EntryType.h"
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
    float startTime = 0.0f;
    float duration = -1.0f;
    bool isPlaying = false;
};