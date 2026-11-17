// ============================================================
// Hash.hlsli
// 整数のハッシュ（lowbias32）。種や格子の座標から、よく混ざった uint を作る。
// ============================================================
#ifndef HASH_HLSLI
#define HASH_HLSLI

uint HashU32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

#endif
