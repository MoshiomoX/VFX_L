// ============================================================
// ModelComponent.h
// ECS 用モデル描画コンポーネント（純データ）
// Model 本体は ResourceManager が所有する共有ポインタを指すだけ。
// 位置は同一 Entity の TransformComponent に従う。
// ============================================================
#pragma once
#include <memory>

class Model;

struct ModelComponent
{
    std::shared_ptr<Model> model;   // ResourceManager からロードした共有モデル
    bool visible = true;
};