// ============================================================
// SwarmPropMeshCB.hlsli
// 敵に付けて描くメッシュの cbuffer（b4）。盾（SwarmShieldVS）、氷の塊（SwarmIceVS）、
// トゲ玉（SwarmSpikeBallVS）、砂山（SwarmMoundVS）で共用する。C++ 側：SwarmSystem::PropMeshCB
// ============================================================
#ifndef SWARM_PROP_MESH_CB_HLSLI
#define SWARM_PROP_MESH_CB_HLSLI

// 使い方は描くものごとに違う（「-」は使わない）
//                 盾                     氷の塊                    トゲ玉                  砂山
//   g_MeshAnchor  置くモデル空間の点     モデル空間での底の中心    -（原点中心）           -
//   g_MeshScale   モデル単位 -> m        同左（普通の体格の塊）    倍率 1 の体の半径 m     体格 1 の砂山の半径 m
//   g_HoldOffset  敵の座標系 m（y は足元から）
//                                        x = 体の中心から塊まで、  y = 倍率 1 の中心の     y = 体格 1 の砂山の
//                                        y = 足元から沈める量      足元からの高さ          高さ m
//   g_MeshYaw     置く前に回す rad       -                         -                       -
//   g_PropTint    色（PS が白テクスチャに掛ける）
//                                        色（リニア、1 超え可）    頂点カラーに掛ける      頂点カラーに掛ける
//   g_PropParam   歩く時の上下の揺れ m   0 = 前の塊、1 = 後ろ      -                       移動中の上下の揺れ m
//   g_PropAlpha   -                      不透明度                  -                       -
cbuffer SwarmPropMeshCB : register(b4)
{
    float3 g_MeshAnchor;
    float g_MeshScale;
    float3 g_HoldOffset;
    float g_MeshYaw;
    float4 g_PropTint;
    float g_PropParam;
    float g_PropAlpha;
    float2 _propPad;
};

#endif
