// ============================================================
// MinimapBake.cpp
// ミニマップ用の地形の絵を焼く（World/MinimapBake.h）
// ============================================================
#include "World/MinimapBake.h"
#include "World/TerrainBuild.h"
#include "World/GridWorld.h"
#include "World/MapData.h"

using namespace DirectX::SimpleMath;

namespace MinimapBake
{
    namespace
    {
        uint8_t ToByte(float v)
        {
            v = std::clamp(v, 0.0f, 1.0f);
            return (uint8_t)(v * 255.0f + 0.5f);
        }
        Vector4 Mul(const Vector4& c, float k) { return { c.x * k, c.y * k, c.z * k, c.w }; }

        // 崖とみなす傾き（1m 進んで何 m 上がるか）。tan 42° あたりから崖の色へ混ぜ始め、tan 65° で崖一色
        constexpr float kCliffStart = 0.9f;
        constexpr float kCliffFull = 2.1f;
        // 陰影の強さ（傾き 1 あたり）。光はミニマップの左上（ワールドの +x, +z。右手系を真上から見ると +x は左）から
        constexpr float kShade = 0.18f;
        // 高さ 1m あたりの明るさの変化（台地 3m で +12%、丘の頂 4m で +16%）
        constexpr float kHeightTint = 0.04f;
    }

    Image Build(const GridWorld& grid, const MapData::Map& map, int biome, uint32_t seed)
    {
        Image img;
        const int W = grid.HeightW(), H = grid.HeightD();
        if (W <= 0 || H <= 0) return img;

        img.w = W;
        img.h = H;
        img.texel = GridWorld::kCellSize / GridWorld::kHeightSub;
        img.originX = grid.OriginX();
        img.originZ = grid.OriginZ();
        img.rgba.assign((size_t)W * H * 4, 0);

        const TerrainBuild::Palette& P = TerrainBuild::PaletteFor((TerrainGenerator::Biome)biome);
        const int gw = grid.Width(), gd = grid.Depth();
        const bool hasZone = (int)map.zone.size() == gw * gd;
        const bool hasRing = (int)map.caveRing.size() == gw * gd;
        const int sub = GridWorld::kHeightSub;
        const float texel = img.texel;
        const float* hs = grid.Heights().data();
        uint8_t* out = img.rgba.data();

        // 端の texel は隣をはみ出さないよう添字を丸める
        auto heightAt = [&](int x, int z)
            {
                x = std::clamp(x, 0, W - 1);
                z = std::clamp(z, 0, H - 1);
                return hs[(size_t)z * W + x];
            };

        for (int z = 0; z < H; ++z)
        {
            for (int x = 0; x < W; ++x)
            {
                const int gx = x / sub, gz = z / sub;
                const int cell = gz * gw + gx;
                const int zone = hasZone ? map.zone[cell] : 0;
                const bool ring = hasRing && map.caveRing[cell] != 0;
                const float wx = img.originX + (x + 0.5f) * texel;
                const float wz = img.originZ + (z + 0.5f) * texel;
                const float n = TerrainBuild::ValueNoise(wx * 0.11f, wz * 0.11f, seed);

                // ---- 地の色 ----
                Vector4 c;
                Vector4 cliff = P.cliff;
                if (ring)
                {
                    c = Mul(P.cliff, 0.75f);                     // 洞の岩の壁
                }
                else if (zone == 2)
                {
                    c = Mul(TerrainBuild::LerpColor(P.mineDark, P.mineLight, n), 0.7f);   // 洞窟の底（地下なので暗く）
                }
                else
                {
                    c = TerrainBuild::LerpColor(P.groundDark, P.groundLight, n);
                    if (zone == 1) { c = Mul(c, 1.25f); cliff = P.cliffHigh; }           // 山頂は一段明るく
                }

                // ---- 高い所ほど明るく（台地の上面・丘の頂が地の色のまま埋もれないように。区域の基準からの差で見る）----
                {
                    const float base = (zone == 1) ? map.summitH : (zone == 2) ? -map.mineD : 0.0f;
                    const float rel = std::clamp(heightAt(x, z) - base, -6.0f, 8.0f);
                    c = Mul(c, 1.0f + kHeightTint * rel);
                }

                // ---- 傾き（中心差分。両隣 = 1m）----
                const float dx = (heightAt(x + 1, z) - heightAt(x - 1, z)) / (2.0f * texel);
                const float dz = (heightAt(x, z + 1) - heightAt(x, z - 1)) / (2.0f * texel);
                const float slope = std::sqrt(dx * dx + dz * dz);
                const float cliffT = std::clamp((slope - kCliffStart) / (kCliffFull - kCliffStart), 0.0f, 1.0f);
                c = TerrainBuild::LerpColor(c, cliff, cliffT);

                // ---- 陰影：ミニマップの左上（+x, +z）から光が当たっている体。+x・+z へ下る面（傾きが負）が明るい ----
                // （2026-10-23 まで dz の符号が逆で、画面の下側から照らしていた = 台地がへこんで見えた）
                const float lit = std::clamp(1.0f + kShade * (std::clamp(-dx, -2.0f, 2.0f) + std::clamp(-dz, -2.0f, 2.0f)), 0.55f, 1.45f);
                c = Mul(c, lit);

                // ---- 塞がったマス（木・岩・箱・登れない台地）は暗い点に。崖はそのまま ----
                if (!ring && cliffT < 0.5f && !grid.IsWalkable(gx, gz))
                    c = Mul(c, 0.45f);
                // ---- 水たまり（2026-10-19）は青 ----
                if (map.pond.size() == (size_t)gw * gd && map.pond[cell])
                    c = Vector4(0.06f, 0.26f, 0.42f, 1.0f);

                uint8_t* px = out + ((size_t)z * W + x) * 4;
                px[0] = ToByte(c.x);
                px[1] = ToByte(c.y);
                px[2] = ToByte(c.z);
                px[3] = 255;
            }
        }
        return img;
    }
}
