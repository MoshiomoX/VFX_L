// ============================================================
// SwarmAreaLightCollectCS.hlsl
// GPU で生まれた範囲（弾の命中 / 寿命切れ）の点光源。中身は
// SwarmLightCollectCS と同じで、ソースが生きている範囲になる。
// CPU が詠唱した範囲は vfxType 0 なのでここでは何も足さない
// （CPU のエフェクト側が自分でライトを積む）。
// ============================================================
#define SWARM_LIGHT_AREAS
#include "SwarmLightCollectCS.hlsl"
