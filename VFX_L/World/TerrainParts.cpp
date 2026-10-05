// ============================================================
// TerrainParts.cpp
// 地形を建てる側（TerrainBuild）のうち、生成と「保存した地図の編集」が共用する部分：
// 格子の矩形・高さ場の補助、起伏の合成（素の起伏 + 台座）、地形の部品（箱・坂）を建てる
// ============================================================
#include "World/TerrainBuild.h"
#include <climits>

namespace TerrainBuild
{
    // 格子の矩形の下隅（世界、y = 0）
    Vector3 RectMin(const GridWorld& g, const Rect& r)
    {
        return { g.OriginX() + r.x * kCs, 0.0f, g.OriginZ() + r.z * kCs };
    }

    // 足跡の高さ場を h まで上げる（台地の上面）
    void RaiseRect(GridWorld& g, const Rect& r, float h)
    {
        const int sub = GridWorld::kHeightSub;
        for (int hz = r.z * sub; hz < (r.z + r.d) * sub; ++hz)
            for (int hx = r.x * sub; hx < (r.x + r.w) * sub; ++hx)
                g.SetHeight(hx, hz, (std::max)(g.HeightAt(hx, hz), h));
    }

    // 凸体の上面を足跡の高さ場へ（坂道・高台の坂）。
    // 高さ = 上を向いた平面のうち一番低い物（下から見て初めて当たる面）
    void WriteHullHeights(GridWorld& g, const Rect& r, const CollisionMath::Convex& hull,
        const Vector3& center)
    {
        const int sub = GridWorld::kHeightSub;
        for (int hz = r.z * sub; hz < (r.z + r.d) * sub; ++hz)
            for (int hx = r.x * sub; hx < (r.x + r.w) * sub; ++hx)
            {
                const Vector3 p = g.HeightCellToWorld(hx, hz);
                const float lx = p.x - center.x;
                const float lz = p.z - center.z;
                float top = 1e9f;
                for (int i = 0; i < hull.count; ++i)
                {
                    const auto& pl = hull.planes[i];
                    if (pl.n.y <= 0.1f) continue;   // 上向きの面だけ
                    top = (std::min)(top, (pl.d - pl.n.x * lx - pl.n.z * lz) / pl.n.y);
                }
                if (top < 1e8f)
                    g.SetHeight(hx, hz, (std::max)(g.HeightAt(hx, hz), top + center.y));
            }
    }

    // ---- 坂道の足跡 ----
    // 台地 p の side 側の外に、辺に沿って offset マス目から幅 rw、長さ len
    Rect RampRect(const Rect& p, Side s, int offset, int rw, int len)
    {
        switch (s)
        {
        case Side::PosX: return { p.x + p.w, p.z + offset, len, rw };
        case Side::NegX: return { p.x - len, p.z + offset, len, rw };
        case Side::PosZ: return { p.x + offset, p.z + p.d, rw, len };
        default:         return { p.x + offset, p.z - len, rw, len };
        }
    }
    // 坂道を降りた先の 1 マス幅の帯。塞がれていると登り口にならない
    Rect LandingRect(const Rect& ramp, Side s)
    {
        switch (s)
        {
        case Side::PosX: return { ramp.x + ramp.w, ramp.z, 1, ramp.d };
        case Side::NegX: return { ramp.x - 1, ramp.z, 1, ramp.d };
        case Side::PosZ: return { ramp.x, ramp.z + ramp.d, ramp.w, 1 };
        default:         return { ramp.x, ramp.z - 1, ramp.w, 1 };
        }
    }

    // 隣のノードとの傾きを軸方向で tanMax までに抑える（急な所を両側から寄せる。fixed のノードは動かさない）。
    // 2026-10-05 に起伏を大きくした時、土饅頭が丘の斜面に載った所・台座の戻りが 40〜45° になった（雑魚は 40° まで）。
    // 軸方向で抑えるので斜め方向は最大 √2 倍（30° → 39°）
    void LimitSlope(std::vector<float>& h, int nx, int nz, float step, float tanMax, const std::vector<uint8_t>* fixed)
    {
        // 配列は素のポインタで回す（Debug の vector の添字検査が重い）。
        // 2 回目以降の走査は「前の走査か今の走査で動いたノードが絡む組」だけ調べる（2026-10-05）:
        // 前に調べてから両端とも動いていない組は、もう一度調べても同じく何もしない。調べる組の順番は変えないので、
        // 結果は全部調べる方式と 1 ビットも変わらない（全体 16 万ノード × 最大 64 回が Debug で 280 ms → 地図エディタが止まった）
        const float lim = tanMax * step;
        float* H = h.data();
        const uint8_t* F = fixed ? fixed->data() : nullptr;
        const size_t count = (size_t)nx * nz;
        std::vector<uint8_t> prevV(count, 1), curV(count, 0);       // ノードが動いたか（前の走査 / 今の走査）。初回は全部調べる
        std::vector<uint8_t> prevRowV((size_t)nz + 1, 1), curRowV((size_t)nz + 1, 0);   // 行に動いたノードがあるか
        uint8_t* prev = prevV.data();
        uint8_t* cur = curV.data();
        uint8_t* prevRow = prevRowV.data();
        uint8_t* curRow = curRowV.data();

        // 隣との差を lim までに縮める。動かしたら true
        auto relax = [&](size_t i, size_t j)
            {
                const float d = H[j] - H[i];
                const float ad = std::fabs(d);
                if (ad <= lim + 1e-4f) return false;
                const bool fi = F && F[i], fj = F && F[j];
                if (fi && fj) return false;
                const float ex = (ad - lim) * (d > 0.0f ? 1.0f : -1.0f);   // 差をこれだけ縮める
                if (fi)      H[j] -= ex;
                else if (fj) H[i] += ex;
                else { H[i] += ex * 0.5f; H[j] -= ex * 0.5f; }
                return true;
            };
        for (int it = 0; it < 64; ++it)
        {
            bool changed = false;
            for (int iz = 0; iz < nz; ++iz)
            {
                // この行の組（右隣・上隣）に絡むのは、この行と 1 つ上の行のノードだけ
                if (!(prevRow[iz] | prevRow[iz + 1] | curRow[iz] | curRow[iz + 1])) continue;
                const size_t row = (size_t)iz * nx;
                for (int ix = 0; ix < nx; ++ix)
                {
                    const size_t i = row + ix;
                    const uint8_t di = prev[i] | cur[i];
                    if (ix + 1 < nx && (di | prev[i + 1] | cur[i + 1]) && relax(i, i + 1))
                    {
                        cur[i] = cur[i + 1] = 1;
                        curRow[iz] = 1;
                        changed = true;
                    }
                    if (iz + 1 < nz)
                    {
                        const size_t j = i + (size_t)nx;
                        // 上の組は、右の組で i が動いたかも見る（今の走査の印を取り直す）
                        if ((prev[i] | cur[i] | prev[j] | cur[j]) && relax(i, j))
                        {
                            cur[i] = cur[j] = 1;
                            curRow[iz] = curRow[iz + 1] = 1;
                            changed = true;
                        }
                    }
                }
            }
            if (!changed) break;
            std::swap(prev, cur);
            std::swap(prevRow, curRow);
            std::fill(cur, cur + count, (uint8_t)0);
            std::fill(curRow, curRow + nz + 1, (uint8_t)0);
        }
    }

    // ============================================================
    // 起伏の合成：素の起伏 + 台座
    // ============================================================
    // 洞窟の周り（岩の壁・洞の口・下り坂の上端）は平原の 0 に均す: 坑のマスからの距離（ノード、チェビシェフ）が
    // 4 マス以内は 1、そこから padMargin マスで 0 へ
    void ComputeMineWeights(const std::vector<uint8_t>& zone, int gw, int gd, int padMargin, std::vector<float>& out)
    {
        const int hsub = GridWorld::kHeightSub;
        const int nx = gw * hsub + 1, nz = gd * hsub + 1;
        out.assign((size_t)nx * nz, 0.0f);
        std::vector<int> dist((size_t)nx * nz, INT_MAX / 4);
        int* D = dist.data();   // 素のポインタで回す（Debug の添字検査が重い）
        bool any = false;
        for (int gz = 0; gz < gd; ++gz)
            for (int gx = 0; gx < gw; ++gx)
            {
                if (zone[(size_t)gz * gw + gx] != ReliefField::kMine) continue;
                any = true;
                for (int iz = gz * hsub; iz <= (gz + 1) * hsub; ++iz)
                    for (int ix = gx * hsub; ix <= (gx + 1) * hsub; ++ix)
                        dist[(size_t)iz * nx + ix] = 0;
            }
        if (!any) return;
        auto relax = [&](int ix, int iz, int jx, int jz)
            {
                if (jx < 0 || jz < 0 || jx >= nx || jz >= nz) return;
                int& d = D[(size_t)iz * nx + ix];
                d = (std::min)(d, D[(size_t)jz * nx + jx] + 1);
            };
        for (int iz = 0; iz < nz; ++iz)
            for (int ix = 0; ix < nx; ++ix)
            {
                relax(ix, iz, ix - 1, iz); relax(ix, iz, ix, iz - 1);
                relax(ix, iz, ix - 1, iz - 1); relax(ix, iz, ix + 1, iz - 1);
            }
        for (int iz = nz - 1; iz >= 0; --iz)
            for (int ix = nx - 1; ix >= 0; --ix)
            {
                relax(ix, iz, ix + 1, iz); relax(ix, iz, ix, iz + 1);
                relax(ix, iz, ix + 1, iz + 1); relax(ix, iz, ix - 1, iz + 1);
            }
        const int full = 4 * hsub, fade = (std::max)(padMargin, 1) * hsub;
        for (size_t i = 0; i < dist.size(); ++i)
        {
            const int d = dist[i];
            if (d >= full + fade) continue;
            float w = 1.0f;
            if (d > full)
            {
                const float t = (float)(d - full) / (float)fade;
                w = 1.0f - t * t * (3.0f - 2.0f * t);
            }
            out[i] = w;
        }
    }

    // 台座の重み（芯 = 1、そこから m ノードで 0 へ smoothstep）
    static float PadWeight(const MapData::Pad& p, int ix, int iz)
    {
        const int d = (std::max)((std::max)(p.ax0 - ix, ix - p.ax1), (std::max)(p.az0 - iz, iz - p.az1));
        if (d <= 0) return 1.0f;
        if (d >= p.m) return 0.0f;
        const float t = (float)d / (float)p.m;
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    // 台座は順番に依らない混ぜ方: ノード毎に 面 = lerp(素の起伏, Lmix, 最大の重み)、Lmix = Σ w^4 L / Σ w^4
    // （芯（重み 1）の台座がほぼ勝つ = 構造物の足元はほぼ L のまま、境は連続）。
    // 回し方は「台座毎に、その台座が届く範囲のノードへ足す」（2026-10-05。以前は「ノード毎に全部の台座を見る」で、
    // 全体の作り直し 16 万ノード × 台座 76 個が Debug で数百 ms。地図エディタで部品を動かす度に止まった）。
    // ノード毎に見れば足す順は台座の順のままなので、結果は以前と 1 ビットも変わらない
    void ComposeRelief(std::vector<float>& arr, const std::vector<float>& raw, const std::vector<MapData::Pad>& pads,
        uint8_t zone, const std::vector<float>* mineW, int nx, int nz, int ix0, int iz0, int ix1, int iz1)
    {
        ix0 = (std::max)(ix0, 0); iz0 = (std::max)(iz0, 0);
        ix1 = (std::min)(ix1, nx - 1); iz1 = (std::min)(iz1, nz - 1);
        if (ix1 < ix0 || iz1 < iz0) return;
        const int rw = ix1 - ix0 + 1, rh = iz1 - iz0 + 1;

        // 範囲の中のノード毎の 最大の重み / Σ w^4 L / Σ w^4
        std::vector<float> wmaxV((size_t)rw * rh, 0.0f), numV((size_t)rw * rh, 0.0f), denV((size_t)rw * rh, 0.0f);
        float* wmax = wmaxV.data();
        float* num = numV.data();
        float* den = denV.data();

        if (mineW)   // 洞窟の周りの 0 への均し（L = 0）。台座より先に入れる
        {
            const float* mw = mineW->data();
            for (int iz = iz0; iz <= iz1; ++iz)
                for (int ix = ix0; ix <= ix1; ++ix)
                {
                    const float m = mw[(size_t)iz * nx + ix];
                    if (m <= 0.0f) continue;
                    const size_t k = (size_t)(iz - iz0) * rw + (ix - ix0);
                    wmax[k] = m;
                    den[k] = m * m * m * m;
                }
        }
        for (const MapData::Pad& p : pads)
        {
            if (p.zone != zone) continue;
            // 重みが 0 でないのは芯から m ノード未満の所だけ
            const int px0 = (std::max)(p.ax0 - p.m, ix0), px1 = (std::min)(p.ax1 + p.m, ix1);
            const int pz0 = (std::max)(p.az0 - p.m, iz0), pz1 = (std::min)(p.az1 + p.m, iz1);
            for (int iz = pz0; iz <= pz1; ++iz)
                for (int ix = px0; ix <= px1; ++ix)
                {
                    const float w = PadWeight(p, ix, iz);
                    if (w <= 0.0f) continue;
                    const float w4 = w * w * w * w;
                    const size_t k = (size_t)(iz - iz0) * rw + (ix - ix0);
                    wmax[k] = (std::max)(wmax[k], w);
                    num[k] += w4 * p.L;
                    den[k] += w4;
                }
        }

        float* out = arr.data();
        const float* src = raw.data();
        for (int iz = iz0; iz <= iz1; ++iz)
            for (int ix = ix0; ix <= ix1; ++ix)
            {
                const size_t i = (size_t)iz * nx + ix;
                const size_t k = (size_t)(iz - iz0) * rw + (ix - ix0);
                out[i] = (den[k] > 0.0f) ? src[i] + (num[k] / den[k] - src[i]) * wmax[k] : src[i];
            }
    }

    // 起伏の仕上げ：台座の戻りなどの急な所を抑える。台座の芯（重み 1 = 構造物の足元・洞窟の周り）は動かさない
    void LimitReliefSlopes(std::vector<float>& arr, const std::vector<MapData::Pad>& pads, uint8_t zone,
        const std::vector<float>* mineW, int nx, int nz, float step, float maxSlopeDeg)
    {
        std::vector<uint8_t> core(arr.size(), 0);
        for (const MapData::Pad& p : pads)
        {
            if (p.zone != zone) continue;
            for (int iz = (std::max)(p.az0, 0); iz <= (std::min)(p.az1, nz - 1); ++iz)
                for (int ix = (std::max)(p.ax0, 0); ix <= (std::min)(p.ax1, nx - 1); ++ix)
                    core[(size_t)iz * nx + ix] = 1;
        }
        if (mineW)
            for (size_t i = 0; i < core.size(); ++i)
                if ((*mineW)[i] >= 0.999f) core[i] = 1;
        LimitSlope(arr, nx, nz, step, std::tan(DirectX::XMConvertToRadians(maxSlopeDeg)), &core);
    }

    // ============================================================
    // 地形の部品を建てる（生成・保存した地図の編集が共用）
    // emit へ衝突と見た目を出し、grid があれば高さ場・通行も書く。origin = 格子の原点を知るための格子
    // ============================================================
    void EmitBlockPart(Emitter& emit, GridWorld* grid, const GridWorld& origin, const MapData::BlockPart& p)
    {
        const Rect r = { p.x, p.z, p.w, p.d };
        const Vector3 lo = RectMin(origin, r) + Vector3(0.0f, p.bottom, 0.0f);
        const Vector3 hi = RectMin(origin, r) + Vector3(p.w * kCs, p.top, p.d * kCs);
        emit.Box(lo, hi, Layer_Terrain);
        Vector3 v[8];
        BoxVerts(v, lo, hi);
        emit.Visual(v, p.topColor, p.sideColor);
        if (p.raise) { if (grid) RaiseRect(*grid, r, p.top); }
        else emit.Block(grid, p.x, p.z, p.w, p.d);   // 登れない台地：上に何も湧かないよう格子を塞ぐ
    }

    // 坂の楔の 8 頂点。高い端（top）が台地の側面に接し、外へ向かって base まで下る
    void RampVerts(const GridWorld& origin, const MapData::RampPart& p, Vector3 v[8])
    {
        const Rect r = { p.x, p.z, p.w, p.d };
        const Vector3 lo = RectMin(origin, r);
        const Vector3 hi = lo + Vector3(r.w * kCs, 0.0f, r.d * kCs);
        const float low = p.base + 0.02f;   // 低い端にも厚みを残す（面が潰れて平面が作れなくならない）
        BoxVerts(v, { lo.x, p.base, lo.z }, { hi.x, p.top, hi.z });
        // 上面 4-7 のうち台地から遠い側を下げる（-x = 4,7 / +x = 5,6 / -z = 4,5 / +z = 6,7）
        switch ((Side)p.side)
        {
        case Side::PosX: v[5].y = v[6].y = low; break;
        case Side::NegX: v[4].y = v[7].y = low; break;
        case Side::PosZ: v[6].y = v[7].y = low; break;
        case Side::NegZ: v[4].y = v[5].y = low; break;
        }
    }

    void EmitRampPart(Emitter& emit, GridWorld* grid, const GridWorld& origin, const MapData::RampPart& p)
    {
        Vector3 v[8];
        RampVerts(origin, p, v);
        // テクスチャの層: 土の坂は自動（斜面 = 道）、草色の長い坂（高台・山頂）は地面と同じ草
        emit.Hull(v, Layer_Terrain);
        emit.Visual(v, p.topColor, p.sideColor, p.grassy ? (int)TerrainSurface::Ground : -1, -1);
        if (!grid) return;

        // 高さ場へ書く凸体は衝突の実体と同じ作り方（包囲箱の中心からの相対）
        Vector3 bmin = v[0], bmax = v[0];
        for (int i = 1; i < 8; ++i)
        {
            bmin = Vector3::Min(bmin, v[i]);
            bmax = Vector3::Max(bmax, v[i]);
        }
        const Vector3 center = (bmin + bmax) * 0.5f;
        Vector3 local[8];
        for (int i = 0; i < 8; ++i) local[i] = v[i] - center;
        WriteHullHeights(*grid, { p.x, p.z, p.w, p.d }, CollisionMath::ConvexFromHexahedron(local), center);
    }


    // ============================================================
    // 素の起伏（台座で均す前）= ノイズの丘 + 丘の部品 → 傾きを抑える
    // 生成と、地図エディタが全体の設定・丘の部品を変えた時の作り直し（MapTerrainEdit::RebuildRaw）が共用
    // ============================================================
    void BuildReliefNoise(const MapData::ReliefParams& rp, uint32_t seed, int gw, int gd,
        std::vector<float>& plain, std::vector<float>& summit)
    {
        const int nx = gw * GridWorld::kHeightSub + 1, nz = gd * GridWorld::kHeightSub + 1;
        const float step = kCs / GridWorld::kHeightSub;
        const float ox = -0.5f * gw * kCs, oz = -0.5f * gd * kCs;
        const float hill = rp.hillHeight, detail = rp.detailHeight;
        const float big = (std::max)(rp.hillScale, 4.0f), fine = (std::max)(rp.detailScale, 2.0f);
        const uint32_t s = seed;
        plain.assign((size_t)nx * nz, 0.0f);
        summit.assign((size_t)nx * nz, 0.0f);
        for (int iz = 0; iz < nz; ++iz)
            for (int ix = 0; ix < nx; ++ix)
            {
                const float x = ox + ix * step, z = oz + iz * step;
                const size_t i = (size_t)iz * nx + ix;
                plain[i] = hill * GradNoise(x / big, z / big, s + 101u) + detail * GradNoise(x / fine, z / fine, s + 103u);
                summit[i] = rp.summitMul
                    * (hill * GradNoise(x / big, z / big, s + 107u) + detail * GradNoise(x / fine, z / fine, s + 109u));
            }
    }

    void AddHillsAndLimit(const std::vector<MapData::Hill>& hills, float summitMul, float maxSlopeDeg, int gw, int gd,
        std::vector<float>& plain, std::vector<float>& summit)
    {
        const int nx = gw * GridWorld::kHeightSub + 1, nz = gd * GridWorld::kHeightSub + 1;
        const float step = kCs / GridWorld::kHeightSub;
        const float ox = -0.5f * gw * kCs, oz = -0.5f * gd * kCs;
        for (const MapData::Hill& hl : hills)
        {
            const float cx = hl.x, cz = hl.z, r = hl.radius, h = hl.height;
            if (r <= 0.0f) continue;
            const int ix0 = (std::max)(0, (int)std::floor((cx - r - ox) / step));
            const int ix1 = (std::min)(nx - 1, (int)std::ceil((cx + r - ox) / step));
            const int iz0 = (std::max)(0, (int)std::floor((cz - r - oz) / step));
            const int iz1 = (std::min)(nz - 1, (int)std::ceil((cz + r - oz) / step));
            for (int iz = iz0; iz <= iz1; ++iz)
                for (int ix = ix0; ix <= ix1; ++ix)
                {
                    const float dx = (ox + ix * step) - cx, dz = (oz + iz * step) - cz;
                    const float u2 = (dx * dx + dz * dz) / (r * r);
                    if (u2 >= 1.0f) continue;
                    const float k = (1.0f - u2) * (1.0f - u2) * h;
                    const size_t i = (size_t)iz * nx + ix;
                    plain[i] += k;
                    summit[i] += k * summitMul;
                }
        }
        // 素の起伏の傾きを抑える（台座の戻りは後で別に抑える）
        const float tanRaw = std::tan(DirectX::XMConvertToRadians(maxSlopeDeg - 2.0f));
        LimitSlope(plain, nx, nz, step, tanRaw, nullptr);
        LimitSlope(summit, nx, nz, step, tanRaw, nullptr);
    }
}
