#pragma once
#include "VFX_Editor/EntryType.h"
#include <nlohmann/json.hpp>
#include <SimpleMath.h>
using json = nlohmann::json;

// ============================================================
// json を読む時の補助（2026-10-25）。壊れた json（数字が文字列・null・要素の足りない配列）でも
// 範囲外の添字アクセスをしない。読めなければ out はそのまま（呼ぶ側の既定値が残る）
// ============================================================
namespace VFXJson
{
    // j[key] が数値 n 個以上の配列なら out[0..n-1] に入れて true。それ以外は out に触らず false
    inline bool ReadFloats(const json& j, const char* key, float* out, int n)
    {
        if (!j.is_object()) return false;
        const auto it = j.find(key);
        if (it == j.end() || !it->is_array() || (int)it->size() < n) return false;
        for (int i = 0; i < n; ++i)
            if (!(*it)[i].is_number()) return false;
        for (int i = 0; i < n; ++i)
            out[i] = (*it)[i].get<float>();
        return true;
    }
}

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
    // 粒子（開始 / 終了色の RGB）・点光源・Mesh entry（tint の RGB）だけが応える。他の entry は何もしない
    virtual void Tint(const DirectX::SimpleMath::Vector3&) {}
    float startTime = 0.0f;
    float duration = -1.0f;
    bool isPlaying = false;
};