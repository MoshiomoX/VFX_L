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
// Y はこの格子に存在しない。
//   図は (gx, gz) の純 2D。障害物は高さに関係なく「塞ぐ」扱い。
//   低い段差を敵に飛び越えさせたくなったら、walkable を高さ値に
//   変えて 2.5D 化する（その時が来るまではやらない）。
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

    void ClearAll() { std::fill(m_Walkable.begin(), m_Walkable.end(), (uint8_t)1); }

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

    bool m_DebugVisible = false;
};