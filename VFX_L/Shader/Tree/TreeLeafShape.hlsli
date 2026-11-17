// ============================================================
// TreeLeafShape.hlsli
// 葉の形：付け根 x = 0 から先 x = 1 までの半幅（-1..1 の板の中で）。先がとがり、付け根が細い
// ============================================================
#ifndef TREE_LEAF_SHAPE_HLSLI
#define TREE_LEAF_SHAPE_HLSLI
#include "../Common/MathConst.hlsli"

float TreeLeafWidth(float x)
{
    return pow(saturate(sin(PI * x)), 0.7) * (1.0 - 0.2 * x);
}

#endif
