// ============================================================
// GridWorld.h
// 地形の格子。地形の置き方と生成点判定の共通言語。
//
// 真値は地形の Entity（AABB collider 付きの静的 Box）。
// walkable[] はそこから導出される派生データ。
//   ※BackpackComponent の occupancy と同じ規律:
//     実体のリストが真値、占用表は Rebuild で作り直す。
//
// 場地はワールド原点が中心。
//   マス (0,0) は場地の隅（南西角）で、ワールド座標では
//   (-WorldWidth/2, -WorldDepth/2) から始まる。
//   座標が負になるので、マス変換は必ず floor を使う
//   （int 切り捨ては 0 方向へ丸まり、負側で 1 マスずれる）。
//
// 通行図 walkable は (gx, gz) の純 2D。箱・壁は高さに関係なく「塞ぐ」。
// 斜面（台形柱）だけは塞がず、別の高さ場（kHeightSub 分割）に
// 「歩ける面の高さ」を書く。雑魚はこれを引いて登る（2.5D）。
//
// デバッグ可視化も内蔵する（Ctrl+F3 で切替）。
// ============================================================
#pragma once
#include <vector>
#include <cstdint>
#include <cmath>
#include <SimpleMath.h>

class GridWorld
{
public:
    // 1マスの一辺（ワールド単位 = m）。
    // 敵カプセルの直径（0.8m)より十分大きいので、
    // 「1マス通れる = 敵が物理的に通れる」が成り立つ
    static constexpr float kCellSize = 2.0f;

    void Init(int gridW, int gridD)
    {
        m_GridW = gridW;
        m_GridD = gridD;
        m_OriginX = -0.5f * gridW * kCellSize;   // 場地の南西角のワールド座標
        m_OriginZ = -0.5f * gridD * kCellSize;
        m_Walkable.assign((size_t)gridW * gridD, 1);
        m_Height.assign((size_t)gridW * kHeightSub * gridD * kHeightSub, 0.0f);
    }

    int  Width() const { return m_GridW; }
    int  Depth() const { return m_GridD; }
    float WorldWidth() const { return m_GridW * kCellSize; }
    float WorldDepth() const { return m_GridD * kCellSize; }
    float OriginX() const { return m_OriginX; }
    float OriginZ() const { return m_OriginZ; }

    // ---- 座標変換 ----
    // 場地が原点中心なのでオフセット付き。static ではなくなった
    DirectX::SimpleMath::Vector3 CellToWorld(int gx, int gz) const
    {
        return { m_OriginX + (gx + 0.5f) * kCellSize,
                 0.0f,
                 m_OriginZ + (gz + 0.5f) * kCellSize };
    }

    void WorldToCell(const DirectX::SimpleMath::Vector3& p, int& gx, int& gz) const
    {
        gx = (int)std::floor((p.x - m_OriginX) / kCellSize);
        gz = (int)std::floor((p.z - m_OriginZ) / kCellSize);
    }

    // ---- 占用 ----
    bool IsWalkable(int gx, int gz) const
    {
        if (gx < 0 || gx >= m_GridW || gz < 0 || gz >= m_GridD) return false;
        return m_Walkable[(size_t)gz * m_GridW + gx] != 0;
    }

    bool IsAreaWalkable(int gx, int gz, int w, int d) const
    {
        for (int z = gz; z < gz + d; ++z)
            for (int x = gx; x < gx + w; ++x)
                if (!IsWalkable(x, z)) return false;
        return true;
    }

    // ワールド座標がそのまま通行可能マスか（敵の回避判定用）
    bool IsWalkableAt(const DirectX::SimpleMath::Vector3& p) const
    {
        int gx = 0, gz = 0;
        WorldToCell(p, gx, gz);
        return IsWalkable(gx, gz);
    }
    void BlockArea(int gx, int gz, int w, int d)
    {
        for (int z = gz; z < gz + d; ++z)
        {
            if (z < 0 || z >= m_GridD) continue;
            for (int x = gx; x < gx + w; ++x)
            {
                if (x < 0 || x >= m_GridW) continue;
                m_Walkable[(size_t)z * m_GridW + x] = 0;
            }
        }
    }   

    void ClearAll()
    {
        std::fill(m_Walkable.begin(), m_Walkable.end(), (uint8_t)1);
        std::fill(m_Height.begin(), m_Height.end(), 0.0f);
    }

    // ============================================================
    // 高さ場（2.5D）
    // walkable とは別に、マスより細かい格子で「歩ける面の高さ」を持つ。
    // 斜面（台形柱）の上を雑魚が登れるようにするためのもの。
    // 通行を塞ぐ物（箱・壁）は walkable=0 のままで高さは書かない。
    //   1 マスを kHeightSub 分割（2m / 4 = 0.5m 刻み）。
    //   SwarmSystem が GPU へ丸ごと上げ、MoveCS が双線形で引く
    // ============================================================
    static constexpr int kHeightSub = 4;
    int HeightW() const { return m_GridW * kHeightSub; }
    int HeightD() const { return m_GridD * kHeightSub; }
    const std::vector<float>& Heights() const { return m_Height; }

    // 高さ格子の (hx, hz) の中心ワールド座標
    DirectX::SimpleMath::Vector3 HeightCellToWorld(int hx, int hz) const
    {
        const float s = kCellSize / kHeightSub;
        return { m_OriginX + (hx + 0.5f) * s, 0.0f, m_OriginZ + (hz + 0.5f) * s };
    }
    void SetHeight(int hx, int hz, float h)
    {
        if (hx < 0 || hx >= HeightW() || hz < 0 || hz >= HeightD()) return;
        float& dst = m_Height[(size_t)hz * HeightW() + hx];
        if (h > dst) dst = h;   // 重なりは高い方
    }
    float HeightAt(int hx, int hz) const
    {
        if (hx < 0 || hx >= HeightW() || hz < 0 || hz >= HeightD()) return 0.0f;
        return m_Height[(size_t)hz * HeightW() + hx];
    }
    // ワールド座標の高さ（双線形。CPU 側で必要になった時用、GPU と同じ式）
    float SampleHeight(float x, float z) const
    {
        const float s = kCellSize / kHeightSub;
        const float fx = (x - m_OriginX) / s - 0.5f;
        const float fz = (z - m_OriginZ) / s - 0.5f;
        const int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
        const float tx = fx - ix, tz = fz - iz;
        const float h00 = HeightAt(ix, iz),     h10 = HeightAt(ix + 1, iz);
        const float h01 = HeightAt(ix, iz + 1), h11 = HeightAt(ix + 1, iz + 1);
        return (h00 * (1 - tx) + h10 * tx) * (1 - tz) + (h01 * (1 - tx) + h11 * tx) * tz;
    }

    // ============================================================
    // デバッグ可視化（Ctrl+F3 で切替）
    // ============================================================
    void DrawDebug(const DirectX::SimpleMath::Vector3& center, int viewRadius = 15);

    bool IsDebugVisible() const { return m_DebugVisible; }

private:
    int m_GridW = 0;
    int m_GridD = 0;
    float m_OriginX = 0.0f;
    float m_OriginZ = 0.0f;
    std::vector<uint8_t> m_Walkable;
    std::vector<float>   m_Height;     // kHeightSub 分割の高さ場（m）

    bool m_DebugVisible = false;
};