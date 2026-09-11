// ============================================================
// GPUReadback.h
// GPU から少量の数値を読み戻す通道。
//
// 絶対に守る規律:
//   阻塞式の Map(D3D11_MAP_READ) は使わない。
//   あれは GPU の完了を待つため管線を断ち切り、数 ms を失う。
//   （GPUParticleSystem で毎フレームの ReadDeadCount を廃止したのと同じ理由）
//
// 手法:
//   staging を 3 枚輪転させる。
//     第 N   フレーム: CopyResource で GPU 側から staging[N%3] へ写す
//                      （GPU 内の操作なので CPU は待たない）
//     第 N+1 フレーム: staging[(N+1)%3] を DO_NOT_WAIT で Map
//                      → 2 フレーム前の値が取れる。まだなら即失敗で戻る
//
//   読める値は 1〜2 フレーム古い。それで困る用途は無い:
//     生存数 → 湧き間隔は 1 秒。撃破数 → 表示だけ。
//     被弾量 → 無敵時間 0.6 秒。全部 30ms の遅れは無意味。
//
// 用途は counter だけ（合計 16 バイト程度）。
//   位置や HP のような大きい配列は読み戻さない。
//   それらの真実は GPU 上にしか無い、という前提でシステムを組む。
// ============================================================
#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

// GPU 側で書かれ、CPU が読む数値の全て。
// HLSL の SwarmCounters と一致させること（順序・サイズ厳守）
struct SwarmCounters
{
    uint32_t aliveEnemies = 0;      // 0
    uint32_t killCount = 0;         // 4
    uint32_t playerDamage = 0;      // 8
    uint32_t aliveProjectiles = 0;  // 12

    // ---- 照準: 玩家に一番近い雑魚 ----
    // nearestKey は GPU 内部用（距離の上位 20bit | スロット 12bit）。CPU は読まない
    uint32_t nearestKey = 0xFFFFFFFFu;  // 16
    float    nearestPos[3] = {};        // 20
    float    nearestVel[3] = {};        // 32
    float    nearestDist = 1e30f;       // 44  目標無しなら巨大値
};
static_assert(sizeof(SwarmCounters) == 48, "SwarmCounters layout mismatch");

class GPUReadback
{
public:
    static constexpr int kFrames = 3;   // 輪転させる staging の枚数

    bool Initialize(ID3D11Device* device);
    void Shutdown();

    // 毎フレーム、全 CS の後に呼ぶ。
    // GPU 側の counter buffer を staging へ写すだけ（CPU は待たない）
    void RequestCopy(ID3D11DeviceContext* ctx, ID3D11Buffer* src);

    // 毎フレーム、RequestCopy の前に呼ぶ。
    // 読めたら true を返して out に格納。まだなら false（前回値を使い回す）
    bool TryRead(ID3D11DeviceContext* ctx, SwarmCounters& out);

    // 最後に読めた値（TryRead が false でもこれは有効）
    const SwarmCounters& Latest() const { return m_Latest; }

private:
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_Staging[kFrames];
    int  m_WriteIndex = 0;      // 次に CopyResource する先
    bool m_Filled[kFrames] = {};  // その枚に Copy 済みか

    SwarmCounters m_Latest;
};