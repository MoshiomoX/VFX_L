// ============================================================
// TerrainSteps.cpp
// 段々の台地（2026-10-19、ユーザー「PokéBonk の地図をそのまま持ってくる」）。
//   開けた草原に、ノイズで決めた曲がりくねった輪郭の台地を 1 段 stepH ずつ積む（最大 maxLevel 段）。
//   段と段は草の坂で結ぶ（どの台地にも必ず 1 本以上。坂が置けない台地は 1 段下げる = 行き止まりを作らない）。
//   外周は rimLevels 段の帯（登れない縁）。0 段の低い所に水たまり。
// 決めるのは ReliefField の level / rampAt / ramps / pond だけ。面の高さ・崖の壁・衝突は ReliefField::SurfaceAt を
// 見る側（高さ場・段々のメッシュ・地面の衝突）が同じ式で出す。
// ============================================================
#include "World/TerrainBuild.h"

using namespace DirectX::SimpleMath;

namespace TerrainBuild
{
    namespace
    {
        int EdgeDist(int gw, int gd, int x, int z) { return (std::min)((std::min)(x, gw - 1 - x), (std::min)(z, gd - 1 - z)); }

        // 4 近傍の連結成分。comp[c] = 成分の番号（mask が偽なら -1）。成分の大きさを返す
        std::vector<int> Components(const std::vector<uint8_t>& mask, int gw, int gd, std::vector<int>& comp)
        {
            comp.assign(mask.size(), -1);
            std::vector<int> sizes;
            std::vector<int> stack;
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                {
                    const int c0 = z * gw + x;
                    if (!mask[c0] || comp[c0] >= 0) continue;
                    const int id = (int)sizes.size();
                    int size = 0;
                    stack.clear();
                    stack.push_back(c0);
                    comp[c0] = id;
                    while (!stack.empty())
                    {
                        const int c = stack.back();
                        stack.pop_back();
                        ++size;
                        const int cx = c % gw, cz = c / gw;
                        const int nb[4] = { cx > 0 ? c - 1 : -1, cx < gw - 1 ? c + 1 : -1, cz > 0 ? c - gw : -1, cz < gd - 1 ? c + gw : -1 };
                        for (int n : nb)
                            if (n >= 0 && mask[n] && comp[n] < 0) { comp[n] = id; stack.push_back(n); }
                    }
                    sizes.push_back(size);
                }
            return sizes;
        }

        void SideStep4(uint8_t side, int& dx, int& dz)
        {
            dx = (side == 0) ? 1 : (side == 1) ? -1 : 0;
            dz = (side == 2) ? 1 : (side == 3) ? -1 : 0;
        }

        // ---- 輪郭のノイズ（StepParams::noise）。どれも「大きいほど高い段」の 1 つの値。分位で切るので値域は揃えなくてよい ----
        // fBm：勾配ノイズを 3 層（倍の細かさ、半分の振れ幅）
        float Fbm(float x, float z, uint32_t seed)
        {
            return GradNoise(x, z, seed) + 0.5f * GradNoise(x * 2.03f + 11.7f, z * 2.03f - 4.1f, seed + 7u)
                + 0.25f * GradNoise(x * 4.11f - 7.3f, z * 4.11f + 2.9f, seed + 13u);
        }
        // 域の歪み：座標そのものを別のノイズでずらしてから fBm を引く（Inigo Quilez の domain warping）。流れるような曲線になる
        float WarpedFbm(float x, float z, uint32_t seed)
        {
            const float qx = Fbm(x + 0.0f, z + 0.0f, seed + 101u);
            const float qz = Fbm(x + 5.2f, z + 1.3f, seed + 103u);
            return Fbm(x + 0.9f * qx, z + 0.9f * qz, seed + 107u);
        }
        // セル（Worley）：格子ごとに 1 点、一番近い点までの距離 F1。点の近くが高い（= 丸い島）。少し勾配ノイズで縁を崩す
        float Cellular(float x, float z, uint32_t seed)
        {
            const int ix = (int)std::floor(x), iz = (int)std::floor(z);
            float f1 = 1.0e9f;
            for (int dz = -1; dz <= 1; ++dz)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int cx = ix + dx, cz = iz + dz;
                    const float px = cx + Hash01(cx, cz, seed + 211u);
                    const float pz = cz + Hash01(cx, cz, seed + 223u);
                    const float d = (px - x) * (px - x) + (pz - z) * (pz - z);
                    f1 = (std::min)(f1, d);
                }
            return (1.0f - std::sqrt(f1)) + 0.25f * GradNoise(x * 2.5f, z * 2.5f, seed + 227u);
        }
        float StepNoise(int kind, float x, float z, uint32_t seed)
        {
            switch (kind)
            {
            case 1:  return WarpedFbm(x, z, seed);
            case 2:  return Cellular(x * 0.55f, z * 0.55f, seed);   // 1 セル ≒ 1.8 x scale（島 1 つが台地 1 つ）
            default: return GradNoise(x, z, seed + 101u) + 0.5f * GradNoise(x / 0.45f + 7.3f, z / 0.45f - 3.1f, seed + 103u);
            }
        }
    }

    bool StepRimCell(const ReliefField& rf, const StepParams& P, int gx, int gz)
    {
        if (P.rimLevels <= 0) return false;
        return EdgeDist(rf.gw, rf.gd, gx, gz) < P.rimLevels * P.rimBand;
    }

    // ============================================================
    // 段・坂・水たまりを決める
    // ============================================================
    void GenerateSteps(ReliefField& rf, const StepParams& P, uint32_t seed, std::mt19937& rng)
    {
        const int gw = rf.gw, gd = rf.gd;
        const size_t N = (size_t)gw * gd;
        rf.stepH = (std::max)(P.stepH, 0.0f);
        rf.level.assign(N, 0);
        rf.rampAt.assign(N, -1);
        rf.ramps.clear();
        rf.pond.assign(N, 0);
        rf.pondDepth = P.pondDepth;
        if (rf.stepH <= 0.0f || gw < 20 || gd < 20) { rf.stepH = 0.0f; return; }

        const int maxLevel = std::clamp(P.maxLevel, 1, 6);
        const int rimW = (std::max)(P.rimLevels, 0) * (std::max)(P.rimBand, 1);
        const int cx = gw / 2, cz = gd / 2;
        auto idx = [&](int x, int z) { return (size_t)z * gw + x; };
        auto plain = [&](int x, int z) { return rf.zone[idx(x, z)] == ReliefField::kPlain; };
        auto rim = [&](int x, int z) { return EdgeDist(gw, gd, x, z) < rimW; };
        auto spawn = [&](int x, int z) { return std::abs(x - cx) <= P.spawnClear + 1 && std::abs(z - cz) <= P.spawnClear + 1; };
        auto lv = [&](int x, int z) -> uint8_t& { return rf.level[idx(x, z)]; };

        // ---- 1. ノイズ → 段。割合は実際の値の分位で決める（coverage が 1 段以上の面積になる）----
        {
            std::vector<float> n(N, -1.0e9f), vals;
            vals.reserve(N);
            const float s1 = (std::max)(P.scale, 4.0f);
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                {
                    if (!plain(x, z) || rim(x, z) || spawn(x, z)) continue;
                    const float wx = rf.ox + (x + 0.5f) * kCs, wz = rf.oz + (z + 0.5f) * kCs;
                    const float v = StepNoise(P.noise, wx / s1, wz / s1, seed);
                    n[idx(x, z)] = v;
                    vals.push_back(v);
                }
            if (!vals.empty())
            {
                std::sort(vals.begin(), vals.end());
                auto q = [&](float frac)
                    {
                        const int i = std::clamp((int)(frac * (vals.size() - 1)), 0, (int)vals.size() - 1);
                        return vals[(size_t)i];
                    };
                const float cov = std::clamp(P.coverage, 0.0f, 0.9f);
                const float t1 = q(1.0f - cov), t2 = q(1.0f - cov * 0.35f), t3 = q(1.0f - cov * 0.10f);
                for (size_t c = 0; c < N; ++c)
                {
                    if (n[c] < -1.0e8f) continue;
                    uint8_t L = 0;
                    if (cov > 0.0f && n[c] >= t1) L = 1;
                    if (maxLevel >= 2 && n[c] >= t2) L = 2;
                    if (maxLevel >= 3 && n[c] >= t3) L = 3;
                    rf.level[c] = L;
                }
            }
            // 外周の帯：外ほど高い（登れない縁）
            for (int z = 0; z < gd; ++z)
                for (int x = 0; x < gw; ++x)
                    if (rim(x, z) && plain(x, z))
                        lv(x, z) = (uint8_t)(P.rimLevels - EdgeDist(gw, gd, x, z) / (std::max)(P.rimBand, 1));
        }

        // ---- 2. 整える：隣との差は 1 段まで、1 マス幅の歯・切れ込みを均す、小さい台地・穴を消す ----
        auto fixAdjacency = [&]()
            {
                for (int pass = 0; pass < 32; ++pass)
                {
                    bool changed = false;
                    for (int z = 1; z < gd - 1; ++z)
                        for (int x = 1; x < gw - 1; ++x)
                        {
                            if (!plain(x, z) || rim(x, z)) continue;
                            int m = 255;
                            const int nb[4][2] = { { x - 1, z }, { x + 1, z }, { x, z - 1 }, { x, z + 1 } };
                            for (const auto& q : nb)
                                if (plain(q[0], q[1])) m = (std::min)(m, (int)lv(q[0], q[1]));
                            if (m < 255 && lv(x, z) > m + 1) { lv(x, z) = (uint8_t)(m + 1); changed = true; }
                        }
                    if (!changed) break;
                }
            };
        auto thinPass = [&](int L)
            {
                std::vector<uint8_t> mask(N, 0);
                for (size_t c = 0; c < N; ++c) mask[c] = rf.level[c] >= L;
                std::vector<uint8_t> next = rf.level;
                auto is = [&](int x, int z) { return mask[idx(x, z)] != 0; };
                for (int z = 1; z < gd - 1; ++z)
                    for (int x = 1; x < gw - 1; ++x)
                    {
                        if (!plain(x, z) || rim(x, z)) continue;
                        const bool thinX = !is(x - 1, z) && !is(x + 1, z);
                        const bool thinZ = !is(x, z - 1) && !is(x, z + 1);
                        const bool gapX = is(x - 1, z) && is(x + 1, z);
                        const bool gapZ = is(x, z - 1) && is(x, z + 1);
                        uint8_t& c = next[idx(x, z)];
                        if (is(x, z) && (thinX || thinZ)) c = (std::min)(c, (uint8_t)(L - 1));
                        else if (!is(x, z) && (gapX || gapZ)) c = (std::max)(c, (uint8_t)L);
                    }
                rf.level.swap(next);
            };
        auto smallPass = [&](int L)
            {
                // 小さい台地（≥ L の成分。帯は数えない）→ 1 段下げる
                std::vector<uint8_t> mask(N, 0);
                for (int z = 0; z < gd; ++z)
                    for (int x = 0; x < gw; ++x)
                        mask[idx(x, z)] = (rf.level[idx(x, z)] >= L && !rim(x, z)) ? 1 : 0;
                std::vector<int> comp;
                const std::vector<int> sizes = Components(mask, gw, gd, comp);
                for (size_t c = 0; c < N; ++c)
                    if (comp[c] >= 0 && sizes[(size_t)comp[c]] < P.minCells) rf.level[c] = (uint8_t)(L - 1);
                // 小さい穴（< L の成分で 4 マス未満、外周に触れない）→ 埋める
                for (size_t c = 0; c < N; ++c) mask[c] = (rf.level[c] < L && plain((int)(c % gw), (int)(c / gw))) ? 1 : 0;
                const std::vector<int> hs = Components(mask, gw, gd, comp);
                std::vector<uint8_t> touchesEdge(hs.size(), 0);
                for (int z = 0; z < gd; ++z)
                    for (int x = 0; x < gw; ++x)
                        if (comp[idx(x, z)] >= 0 && (rim(x, z) || spawn(x, z) || x == 0 || z == 0 || x == gw - 1 || z == gd - 1))
                            touchesEdge[(size_t)comp[idx(x, z)]] = 1;
                for (size_t c = 0; c < N; ++c)
                    if (comp[c] >= 0 && hs[(size_t)comp[c]] < 4 && !touchesEdge[(size_t)comp[c]]) rf.level[c] = (uint8_t)L;
            };
        for (int round = 0; round < 2; ++round)
        {
            for (int L = 1; L <= maxLevel; ++L) { thinPass(L); smallPass(L); }
            fixAdjacency();
        }

        // ---- 3. 坂：段 L の台地（≥ L の成分）ごとに、段 L-1 の平地から登る坂を 1〜3 本。置けなければ 1 段下げる ----
        const int rw = (std::max)(P.rampWidth, 1);
        const float tanS = std::tan(DirectX::XMConvertToRadians(std::clamp(P.rampSlopeDeg, 5.0f, 35.0f)));
        const int len = (std::max)(1, (int)std::ceil(rf.stepH / (kCs * tanS)));
        auto inField = [&](int x, int z) { return x >= 1 && z >= 1 && x <= gw - 2 && z <= gd - 2; };
        auto usable = [&](int x, int z, int L)   // 坂のマスに使える（段 L の平地、坂・帯ではない）
            {
                return inField(x, z) && plain(x, z) && !rim(x, z) && lv(x, z) == L && rf.rampAt[idx(x, z)] < 0;
            };
        struct Cand { int x, z, w, d; uint8_t side; float cx, cz; };
        int dissolved = 0;
        for (int L = 1; L <= maxLevel; ++L)
        {
            for (int iter = 0; iter < 64; ++iter)
            {
                std::vector<uint8_t> mask(N, 0);
                for (int z = 0; z < gd; ++z)
                    for (int x = 0; x < gw; ++x)
                        mask[idx(x, z)] = (lv(x, z) >= L && plain(x, z) && !rim(x, z)) ? 1 : 0;
                std::vector<int> comp;
                const std::vector<int> sizes = Components(mask, gw, gd, comp);
                if (sizes.empty()) break;
                // 既にこの段へ登る坂が付いている成分（高い端のマスの成分）
                std::vector<int> has(sizes.size(), 0);
                for (const MapData::StepRamp& r : rf.ramps)
                {
                    if (r.lo != L - 1) continue;
                    int dx, dz;
                    SideStep4(r.side, dx, dz);
                    // 高い端 = 下る向きの反対側の外 1 列
                    const int hx = (dx > 0) ? r.x - 1 : (dx < 0) ? r.x + r.w : r.x;
                    const int hz = (dz > 0) ? r.z - 1 : (dz < 0) ? r.z + r.d : r.z;
                    const int hw = (dx != 0) ? 1 : r.w, hd = (dz != 0) ? 1 : r.d;
                    for (int z = hz; z < hz + hd; ++z)
                        for (int x = hx; x < hx + hw; ++x)
                            if (x >= 0 && z >= 0 && x < gw && z < gd && comp[idx(x, z)] >= 0) ++has[(size_t)comp[idx(x, z)]];
                }
                bool any = false;
                for (int k = 0; k < (int)sizes.size(); ++k)
                {
                    const int need = std::clamp(sizes[(size_t)k] / 60, 1, 3);
                    if (has[(size_t)k] >= need) continue;
                    // 候補：成分の縁のマス（段 L）に接する、段 L-1 の平地の矩形（幅 rw、長さ len）+ その先 1 列も平地
                    std::vector<Cand> cands;
                    for (int z = 1; z < gd - 1; ++z)
                        for (int x = 1; x < gw - 1; ++x)
                        {
                            if (comp[idx(x, z)] != k || lv(x, z) != L) continue;
                            for (uint8_t side = 0; side < 4; ++side)
                            {
                                int dx, dz;
                                SideStep4(side, dx, dz);
                                const int px = (dx != 0) ? 0 : 1, pz = (dx != 0) ? 1 : 0;   // 縁に沿う向き
                                bool ok = true;
                                for (int j = 0; j < rw && ok; ++j)
                                {
                                    const int hx = x + px * j, hz = z + pz * j;   // 高い端の列
                                    ok = hx >= 0 && hz >= 0 && hx < gw && hz < gd && comp[idx(hx, hz)] == k && lv(hx, hz) == L;
                                    for (int m = 1; m <= len + 1 && ok; ++m)
                                    {
                                        const int rx = hx + dx * m, rz = hz + dz * m;
                                        ok = (m <= len) ? usable(rx, rz, L - 1)
                                            : (inField(rx, rz) && plain(rx, rz) && !rim(rx, rz) && lv(rx, rz) == L - 1);   // 降りた先も平地
                                    }
                                }
                                if (!ok) continue;
                                Cand c;
                                c.side = side;
                                if (dx > 0)      { c.x = x + 1;   c.z = z; c.w = len; c.d = rw; }
                                else if (dx < 0) { c.x = x - len; c.z = z; c.w = len; c.d = rw; }
                                else if (dz > 0) { c.x = x; c.z = z + 1;   c.w = rw;  c.d = len; }
                                else             { c.x = x; c.z = z - len; c.w = rw;  c.d = len; }
                                c.cx = c.x + c.w * 0.5f; c.cz = c.z + c.d * 0.5f;
                                cands.push_back(c);
                            }
                        }
                    if (cands.empty())
                    {
                        if (has[(size_t)k] > 0) continue;   // 1 本はある：足りないだけなので諦める
                        // 登れない台地：1 段下げる（成分のマス全部）
                        for (size_t c = 0; c < N; ++c)
                            if (comp[c] == k && rf.level[c] > 0) --rf.level[c];
                        ++dissolved;
                        any = true;
                        continue;
                    }
                    std::shuffle(cands.begin(), cands.end(), rng);
                    std::vector<Cand> made;
                    for (const MapData::StepRamp& r : rf.ramps)   // 既にある坂からも離す
                        if (r.lo == L - 1) made.push_back({ r.x, r.z, r.w, r.d, r.side, r.x + r.w * 0.5f, r.z + r.d * 0.5f });
                    int placed = has[(size_t)k];
                    for (const Cand& c : cands)
                    {
                        if (placed >= need) break;
                        bool farEnough = true;
                        for (const Cand& m : made)
                        {
                            const float ddx = m.cx - c.cx, ddz = m.cz - c.cz;
                            if (ddx * ddx + ddz * ddz < (float)((rw + 5) * (rw + 5))) { farEnough = false; break; }
                        }
                        if (!farEnough) continue;
                        bool stillFree = true;
                        for (int z = c.z; z < c.z + c.d && stillFree; ++z)
                            for (int x = c.x; x < c.x + c.w && stillFree; ++x)
                                stillFree = usable(x, z, L - 1);
                        if (!stillFree) continue;
                        MapData::StepRamp r;
                        r.x = c.x; r.z = c.z; r.w = c.w; r.d = c.d; r.side = c.side; r.lo = (uint8_t)(L - 1);
                        const int16_t id = (int16_t)rf.ramps.size();
                        rf.ramps.push_back(r);
                        for (int z = c.z; z < c.z + c.d; ++z)
                            for (int x = c.x; x < c.x + c.w; ++x)
                                rf.rampAt[idx(x, z)] = id;
                        made.push_back(c);
                        ++placed;
                        any = true;
                    }
                }
                if (!any) break;
            }
        }

        // ---- 4. 水たまり：0 段の平地で、周り 8 マスも 0 段の平地（坂・帯・崖の際ではない）な所の、ノイズの低い割合 ----
        if (P.pondCoverage > 0.0f)
        {
            std::vector<float> pn(N, 1.0e9f), vals;
            for (int z = 2; z < gd - 2; ++z)
                for (int x = 2; x < gw - 2; ++x)
                {
                    if (!plain(x, z) || rim(x, z) || spawn(x, z) || lv(x, z) != 0 || rf.rampAt[idx(x, z)] >= 0) continue;
                    bool flat = true;
                    for (int dz = -1; dz <= 1 && flat; ++dz)
                        for (int dx = -1; dx <= 1 && flat; ++dx)
                            flat = plain(x + dx, z + dz) && !rim(x + dx, z + dz) && lv(x + dx, z + dz) == 0 && rf.rampAt[idx(x + dx, z + dz)] < 0;
                    if (!flat) continue;
                    const float wx = rf.ox + (x + 0.5f) * kCs, wz = rf.oz + (z + 0.5f) * kCs;
                    pn[idx(x, z)] = ValueNoise(wx / 14.0f, wz / 14.0f, seed + 211u) + 0.35f * ValueNoise(wx / 5.0f, wz / 5.0f, seed + 223u);
                    vals.push_back(pn[idx(x, z)]);
                }
            if (!vals.empty())
            {
                std::sort(vals.begin(), vals.end());
                const float t = vals[(size_t)std::clamp((int)(std::clamp(P.pondCoverage, 0.0f, 0.5f) * (vals.size() - 1)), 0, (int)vals.size() - 1)];
                for (size_t c = 0; c < N; ++c) rf.pond[c] = (pn[c] <= t) ? 1 : 0;
                // 小さすぎる水たまり（3 マス未満）は消す
                std::vector<int> comp;
                const std::vector<int> sizes = Components(rf.pond, gw, gd, comp);
                for (size_t c = 0; c < N; ++c)
                    if (comp[c] >= 0 && sizes[(size_t)comp[c]] < 3) rf.pond[c] = 0;
            }
        }

        int cells1 = 0, cells2 = 0, cells3 = 0, ponds = 0;
        for (int z = 0; z < gd; ++z)
            for (int x = 0; x < gw; ++x)
            {
                if (rim(x, z)) continue;
                const int L = lv(x, z);
                cells1 += L >= 1; cells2 += L >= 2; cells3 += L >= 3;
                ponds += rf.pond[idx(x, z)];
            }
        std::cout << "[Terrain] steps: " << rf.stepH << "m x " << maxLevel << ", cells >=1 " << cells1 << " >=2 " << cells2
            << " >=3 " << cells3 << " of " << N << ", ramps " << rf.ramps.size() << " (" << rw << "x" << len << "), dissolved "
            << dissolved << ", pond cells " << ponds << ", rim " << P.rimLevels << " x " << P.rimBand << " cells" << std::endl;
    }

    // ============================================================
    // 崖の衝突
    // 段の境（マスの境の辺）ごとに、上の段の内側へ 0.4m の薄い箱。真っ直ぐ続く所は 4 マスまでまとめる
    // （上面 = その範囲の上の段の地面の最低より kStepUp = 0.6m 下。上を歩く時に引っ掛からず、下から見ると壁。
    //  残りの 0.6m は地面の衝突が「乗る」扱いにする）。
    // 坂の低い端・高い端（高さが続いている所）には付かない（差が 0.6m 未満なら無し）
    // ============================================================
    int EmitStepCliffs(Emitter& emit, const GridWorld& grid, const ReliefField& rf)
    {
        if (!rf.HasSteps()) return 0;
        const int gw = rf.gw, gd = rf.gd;
        constexpr float kThick = 0.4f;
        constexpr float kMinDrop = 0.6f;
        constexpr int kMaxRun = 4;
        // 箱の上面は上の段の地面よりこれだけ下：残りの段差は地面の衝突（PhysicsSystem の kStepUp = 0.6）が「乗る」扱いにする。
        // 0.05 だと、ジャンプで足が縁のすぐ下に届いた時に箱の側面に引っ掛かって押し戻された（2026-10-20 ユーザー報告）
        constexpr float kStepUp = 0.6f;
        int count = 0;

        // 境 (ax, az) | (bx, bz) の 3 点（辺の両端と中央）での両側の高さ
        struct EdgeH { float hiMin, loMin; bool cliff; bool aHigh; };
        auto edge = [&](int ax, int az, int bx, int bz, bool alongZ) -> EdgeH
            {
                // 辺の位置（世界）：alongZ = 境が z に沿う（a と b が x に隣）
                const float ex = alongZ ? rf.ox + (std::max)(ax, bx) * kCs : rf.ox + ax * kCs;
                const float ez = alongZ ? rf.oz + az * kCs : rf.oz + (std::max)(az, bz) * kCs;
                float hA[3], hB[3];
                for (int i = 0; i < 3; ++i)
                {
                    const float t = (i + 0.5f) / 3.0f * kCs;
                    const float x = alongZ ? ex : ex + t, z = alongZ ? ez + t : ez;
                    hA[i] = rf.SurfaceAt(ax, az, x, z);
                    hB[i] = rf.SurfaceAt(bx, bz, x, z);
                }
                EdgeH e;
                float diff = 0.0f;
                for (int i = 0; i < 3; ++i) diff += hA[i] - hB[i];
                e.aHigh = diff > 0.0f;
                const float* hi = e.aHigh ? hA : hB;
                const float* lo = e.aHigh ? hB : hA;
                e.hiMin = (std::min)((std::min)(hi[0], hi[1]), hi[2]);
                e.loMin = (std::min)((std::min)(lo[0], lo[1]), lo[2]);
                float minDrop = 1.0e9f;
                for (int i = 0; i < 3; ++i) minDrop = (std::min)(minDrop, hi[i] - lo[i]);
                e.cliff = minDrop >= kMinDrop;
                return e;
            };
        auto emitBox = [&](bool alongZ, int edgeX, int edgeZ, int runCells, bool highIsNeg, float top, float bottom)
            {
                // alongZ: 境は x = ox + edgeX*kCs の線、z は edgeZ から runCells マス。箱は高い側（highIsNeg = -x 側）へ kThick
                Vector3 lo, hi;
                if (alongZ)
                {
                    const float x = rf.ox + edgeX * kCs;
                    lo = { highIsNeg ? x - kThick : x, bottom, rf.oz + edgeZ * kCs };
                    hi = { highIsNeg ? x : x + kThick, top, rf.oz + (edgeZ + runCells) * kCs };
                }
                else
                {
                    const float z = rf.oz + edgeZ * kCs;
                    lo = { rf.ox + edgeX * kCs, bottom, highIsNeg ? z - kThick : z };
                    hi = { rf.ox + (edgeX + runCells) * kCs, top, highIsNeg ? z : z + kThick };
                }
                if (hi.y <= lo.y + 0.05f) return;
                emit.Box(lo, hi, Layer_Terrain);
                ++count;
            };

        // x に隣り合うマスの境（z に沿う辺）：各 x の列で z 方向にまとめる
        for (int x = 1; x < gw - 1; ++x)
        {
            int runStart = -1, runLen = 0;
            bool runHighNeg = false;
            float runTop = 1.0e9f, runBottom = 1.0e9f;
            auto flush = [&]()
                {
                    if (runLen > 0) emitBox(true, x, runStart, runLen, runHighNeg, runTop - kStepUp, runBottom - 0.6f);
                    runStart = -1; runLen = 0; runTop = runBottom = 1.0e9f;
                };
            for (int z = 1; z < gd - 1; ++z)
            {
                const EdgeH e = edge(x - 1, z, x, z, true);
                const bool highNeg = e.aHigh;   // a = x-1 側が高い
                if (!e.cliff) { flush(); continue; }
                if (runLen > 0 && (highNeg != runHighNeg || runLen >= kMaxRun || std::fabs(e.hiMin - runTop) > 0.3f)) flush();
                if (runLen == 0) { runStart = z; runHighNeg = highNeg; }
                runTop = (std::min)(runTop, e.hiMin);
                runBottom = (std::min)(runBottom, e.loMin);
                ++runLen;
            }
            flush();
        }
        // z に隣り合うマスの境（x に沿う辺）
        for (int z = 1; z < gd - 1; ++z)
        {
            int runStart = -1, runLen = 0;
            bool runHighNeg = false;
            float runTop = 1.0e9f, runBottom = 1.0e9f;
            auto flush = [&]()
                {
                    if (runLen > 0) emitBox(false, runStart, z, runLen, runHighNeg, runTop - kStepUp, runBottom - 0.6f);
                    runStart = -1; runLen = 0; runTop = runBottom = 1.0e9f;
                };
            for (int x = 1; x < gw - 1; ++x)
            {
                const EdgeH e = edge(x, z - 1, x, z, false);
                const bool highNeg = e.aHigh;   // a = z-1 側が高い
                if (!e.cliff) { flush(); continue; }
                if (runLen > 0 && (highNeg != runHighNeg || runLen >= kMaxRun || std::fabs(e.hiMin - runTop) > 0.3f)) flush();
                if (runLen == 0) { runStart = x; runHighNeg = highNeg; }
                runTop = (std::min)(runTop, e.hiMin);
                runBottom = (std::min)(runBottom, e.loMin);
                ++runLen;
            }
            flush();
        }
        (void)grid;
        return count;
    }

    // ============================================================
    // 水たまりの見た目
    // マス毎に 1 枚：四隅は起伏の面（水たまりで下げる前）- 0.15m、底は更に 0.8m 下。隣のマスと四隅を共有するので継ぎ目は出ない
    // ============================================================
    void EmitPonds(Emitter& emit, const GridWorld& grid, const ReliefField& rf, TerrainGenerator::Biome biome)
    {
        if (rf.pond.size() != (size_t)rf.gw * rf.gd) return;
        Vector4 top, side;
        switch (biome)
        {
        case TerrainGenerator::Biome::Desert:  top = { 0.06f, 0.30f, 0.34f, 1.0f }; side = { 0.03f, 0.14f, 0.17f, 1.0f }; break;   // オアシス
        case TerrainGenerator::Biome::Dungeon: top = { 0.015f, 0.05f, 0.09f, 1.0f }; side = { 0.008f, 0.02f, 0.04f, 1.0f }; break; // 暗い水
        default:                               top = { 0.05f, 0.24f, 0.36f, 1.0f }; side = { 0.02f, 0.10f, 0.16f, 1.0f }; break;   // 草原の池
        }
        constexpr float kSurface = -0.15f, kDepth = 0.8f;
        for (int z = 0; z < rf.gd; ++z)
            for (int x = 0; x < rf.gw; ++x)
            {
                if (!rf.pond[(size_t)z * rf.gw + x]) continue;
                const float x0 = rf.ox + x * kCs, x1 = x0 + kCs, z0 = rf.oz + z * kCs, z1 = z0 + kCs;
                const uint8_t k = rf.ZoneAtCell(x, z);
                auto h = [&](float px, float pz) { return rf.SurfaceIn(k, px, pz) + kSurface; };
                Vector3 v[8] = {
                    { x0, h(x0, z0) - kDepth, z0 }, { x1, h(x1, z0) - kDepth, z0 }, { x1, h(x1, z1) - kDepth, z1 }, { x0, h(x0, z1) - kDepth, z1 },
                    { x0, h(x0, z0), z0 },          { x1, h(x1, z0), z0 },          { x1, h(x1, z1), z1 },          { x0, h(x0, z1), z1 },
                };
                emit.Water(v, top, side);
            }
        (void)grid;
    }
}
