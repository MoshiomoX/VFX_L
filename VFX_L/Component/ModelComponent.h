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
    // 動かない置物: RenderSystem は描かず、シーンの StaticPropRenderer がモデル毎にまとめて描く
    // （Build した時の Transform のまま。StaticPropRenderer を持つシーンでだけ立てる）
    bool batched = false;
};