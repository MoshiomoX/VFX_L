param(
    [ValidateSet("sand", "stone", "armor", "gargoyle", "necro", "skeleton", "warden", "eaxe", "earcher", "erider", "eflame", "eshade", "elibrarian")][string]$Style = "sand",
    [string]$Source = "",
    [string]$Out = "",
    [int]$Seed = 2410
)
# ============================================================
# BuildBossSkin.ps1 (2026-10-24)
# 面の Boss の皮（Kenney Blocky の箱の展開図 1024^2。18 体共通の UV）を Kenney の皮から塗り直して作る。
#   -Style sand（既定）: 砂漠「砂の守護者」← Kenney Blocky H（紫灰のロボット。顔が板の組み合わせ = 石像に向く）
#       灰色 → 砂岩の明暗（元の明るさを保つ）、紫の差し色 → 金、白（目・胸の宝石・艶）→ 明るい砂岩（目は発光する飾りの箱）
#   -Style stone（2026-10-25）: 草原「岩の巨人」← Kenney Blocky O（牙のあるオーク = トロルのような巨人の顔）
#       青緑の肌 → 灰色の岩、茶色（髪・胴着）→ 苔の緑、ベージュ（ズボン）→ 土色の岩、薄紫（靴）→ 濃い石板、
#       橙の目 → 暗い岩（目は発光する飾り）、白い牙はそのまま骨の白。岩の所に苔の斑
#   どちらも 8px の粒の明暗のむら + ひび割れ（背景ではない所だけ）
#   遺跡の敵（2026-10-25）: 使っていない Kenney の皮を明るさのグラデーションで塗り直す（顔は飾りの兜・頭巾・頭蓋で隠す）
#     -Style armor   : 呪いの鎧 ← q（黒いスーツ）→ 鋼の明暗、赤いネクタイ → 呪いの紫
#     -Style gargoyle: 石像ガーゴイル ← c（緑の服）→ 灰色の石 + 多めのひび
#     -Style necro   : 死霊術師 ← e（紫の服）→ 黒紫の衣、手・首 → 青白い肌
#     -Style skeleton: 骸骨 ← i（水色の服）→ 骨の色 + 少しのひび
#     -Style warden  : 鎖の番人 ← m（緑の服に革紐）→ 錆びた鉄と革 + 錆の斑
#   面の固定エリート（2026-10-25）: 同じ明るさのグラデーション（顔は飾りの兜・頭巾で隠す）
#     -Style eaxe      : 大斧の戦鬼 ← p → 暗い赤銅の鎧、肌 → 赤らんだ褐色
#     -Style earcher   : 森の弓手 ← f → 深い森の緑と苔色、肌 → 色白
#     -Style erider    : 砂塵の騎兵 ← a → 焦げ茶と砂色の布、肌 → 日焼けの銅色
#     -Style eflame    : 炎砂の術師 ← b → 暗い臙脂 → 朱 → 橙金の衣
#     -Style eshade    : 影の剣士 ← r（黒い忍者）→ 藍の黒と紫の差し色
#     -Style elibrarian: 呪いの司書 ← e → 紺の衣と古い紙の色、肌 → 灰色がかった白
# 使い方（リポジトリの根から）: powershell -File Tools/BuildBossSkin.ps1 [-Style stone]
# 素材: Kenney Blocky Characters（CC0）
# ============================================================
$srcLetter = @{ sand = "h"; stone = "o"; armor = "q"; gargoyle = "c"; necro = "e"; skeleton = "i"; warden = "m"; eaxe = "p"; earcher = "f"; erider = "a"; eflame = "b"; eshade = "r"; elibrarian = "e" }
$outName = @{ sand = "BossSandGuardian"; stone = "BossStoneGiant"; armor = "RuinsArmor"; gargoyle = "RuinsGargoyle"; necro = "RuinsNecro"; skeleton = "RuinsSkeleton"; warden = "RuinsWarden"; eaxe = "EliteAxe"; earcher = "EliteArcher"; erider = "EliteRider"; eflame = "EliteFlame"; eshade = "EliteShade"; elibrarian = "EliteLibrarian" }
if ($Source -eq "") { $Source = "VFX_L/Assets/Model/Kenney_BlockyCharacters/fbx/Textures/texture-$($srcLetter[$Style]).png" }
if ($Out -eq "") { $Out = "VFX_L/Assets/Texture/Enemy/$($outName[$Style]).png" }
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class BossSkin
{
    static float Smooth(float a, float b, float x) { float t = Math.Max(0f, Math.Min(1f, (x - a) / (b - a))); return t * t * (3f - 2f * t); }
    static float Hash(int x, int y, int s) { unchecked { uint h = (uint)(x * 374761393 + y * 668265263 + s * 2147483647); h = (h ^ (h >> 13)) * 1274126177u; h ^= h >> 16; return (h & 0xFFFFFF) / 16777215f; } }

    static float Hue(float r, float g, float b, float mx, float mn)
    {
        float d = mx - mn;
        if (d < 1e-4f) return 0f;
        float hh = (mx == r) ? (g - b) / d : (mx == g) ? 2f + (b - r) / d : 4f + (r - g) / d;
        hh *= 60f;
        return hh < 0f ? hh + 360f : hh;
    }
    // 値ノイズ（cell px の格子を滑らかに補間）0〜1
    static float ValueNoise(float x, float y, float cell, int s)
    {
        float fx = x / cell, fy = y / cell;
        int ix = (int)Math.Floor(fx), iy = (int)Math.Floor(fy);
        float tx = fx - ix, ty = fy - iy;
        tx = tx * tx * (3f - 2f * tx); ty = ty * ty * (3f - 2f * ty);
        float a = Hash(ix, iy, s), b = Hash(ix + 1, iy, s), c = Hash(ix, iy + 1, s), d = Hash(ix + 1, iy + 1, s);
        return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
    }

    // 草原の岩の巨人：色相で分けて塗り替える（cr, cg, cb は sRGB 0〜1）
    static void MapStone(int x, int y, int seed, float rr, float g, float b, float lum, float sat, float mx, float mn,
        out float cr, out float cg, out float cb)
    {
        float hue = Hue(rr, g, b, mx, mn);
        bool rock = false;
        if (lum > 0.85f && sat < 0.25f) { cr = 0.90f; cg = 0.88f; cb = 0.80f; return; }   // 牙 → 骨の白
        if (sat > 0.55f && mx > 0.8f && hue > 25f && hue < 50f) { cr = 0.24f; cg = 0.25f; cb = 0.26f; return; }   // 橙の目 → 暗い岩
        if (sat > 0.35f && hue > 140f && hue < 200f)
        {
            float k = 0.62f + 0.45f * mx;   // 青緑の肌 → 灰色の岩（0.47 × 0.87 だとゲームの中でほぼ黒。sRGB 0.58 くらい）
            cr = 0.58f * k; cg = 0.60f * k; cb = 0.60f * k; rock = true;
        }
        else if (sat > 0.28f && hue < 45f && mx < 0.78f)
        {
            float k = 0.55f + 0.55f * mx;   // 茶色（髪・胴着）→ 苔の緑
            cr = 0.30f * k; cg = 0.45f * k; cb = 0.16f * k;
        }
        else if (hue > 20f && hue < 55f && sat <= 0.45f && mx >= 0.75f)
        {
            float k = 0.70f + 0.30f * mx;   // ベージュ（ズボン）→ 土色の岩
            cr = 0.55f * k; cg = 0.49f * k; cb = 0.40f * k; rock = true;
        }
        else if (hue > 200f && hue < 290f)
        {
            float k = 0.60f + 0.50f * mx;   // 薄紫（靴）→ 濃い石板
            cr = 0.34f * k; cg = 0.36f * k; cb = 0.40f * k; rock = true;
        }
        else
        {
            float t = Smooth(0.18f, 0.85f, lum);   // その他 → 灰色の明暗
            cr = cg = cb = 0.25f + 0.45f * t;
            rock = true;
        }
        // 岩の所に苔の斑（16px の値ノイズが高い所）
        if (rock)
        {
            float n = ValueNoise(x, y, 22f, seed + 7);
            float m = Smooth(0.70f, 0.84f, n) * 0.6f;   // 0.62〜0.78 × 0.75 は迷彩のように多すぎた
            cr += (0.25f - cr) * m; cg += (0.38f - cg) * m; cb += (0.14f - cb) * m;
        }
    }

    static float Lerp(float a, float b, float t) { return a + (b - a) * t; }

    // 遺跡の敵（2026-10-25）：明るさ → 3 色のグラデーション（暗 → 中 → 明）。肌色（手・首）と鮮やかな色は種類毎に差し替え
    static void MapRuins(string style, int x, int y, int seed, float rr, float g, float b, float lum, float sat, float mx, float mn,
        out float cr, out float cg, out float cb)
    {
        float hue = Hue(rr, g, b, mx, mn);
        bool skinTone = sat > 0.18f && sat < 0.62f && hue > 10f && hue < 45f && mx > 0.72f;
        bool vivid = sat > 0.5f && !skinTone;
        float[] a, m, c;
        switch (style)
        {
            case "armor": a = new float[] { 0.18f, 0.18f, 0.21f }; m = new float[] { 0.36f, 0.37f, 0.41f }; c = new float[] { 0.58f, 0.60f, 0.64f }; break;
            case "gargoyle": a = new float[] { 0.24f, 0.25f, 0.24f }; m = new float[] { 0.46f, 0.47f, 0.44f }; c = new float[] { 0.62f, 0.62f, 0.58f }; break;
            case "necro": a = new float[] { 0.07f, 0.04f, 0.09f }; m = new float[] { 0.20f, 0.11f, 0.25f }; c = new float[] { 0.38f, 0.28f, 0.40f }; break;
            case "skeleton": a = new float[] { 0.45f, 0.42f, 0.34f }; m = new float[] { 0.76f, 0.72f, 0.61f }; c = new float[] { 0.93f, 0.90f, 0.80f }; break;
            case "eaxe": a = new float[] { 0.14f, 0.05f, 0.04f }; m = new float[] { 0.40f, 0.14f, 0.09f }; c = new float[] { 0.70f, 0.46f, 0.28f }; break;
            case "earcher": a = new float[] { 0.06f, 0.11f, 0.06f }; m = new float[] { 0.20f, 0.33f, 0.16f }; c = new float[] { 0.52f, 0.58f, 0.34f }; break;
            case "erider": a = new float[] { 0.16f, 0.10f, 0.06f }; m = new float[] { 0.48f, 0.36f, 0.22f }; c = new float[] { 0.86f, 0.74f, 0.52f }; break;
            case "eflame": a = new float[] { 0.16f, 0.03f, 0.04f }; m = new float[] { 0.62f, 0.16f, 0.06f }; c = new float[] { 0.95f, 0.62f, 0.20f }; break;
            case "eshade": a = new float[] { 0.03f, 0.03f, 0.07f }; m = new float[] { 0.09f, 0.08f, 0.19f }; c = new float[] { 0.26f, 0.22f, 0.40f }; break;
            case "elibrarian": a = new float[] { 0.04f, 0.05f, 0.11f }; m = new float[] { 0.14f, 0.16f, 0.32f }; c = new float[] { 0.66f, 0.60f, 0.46f }; break;
            default: a = new float[] { 0.14f, 0.11f, 0.09f }; m = new float[] { 0.38f, 0.29f, 0.22f }; c = new float[] { 0.56f, 0.45f, 0.34f }; break;   // warden
        }
        float t = Smooth(0.15f, 0.92f, lum);
        if (t < 0.5f) { float u = t * 2f; cr = Lerp(a[0], m[0], u); cg = Lerp(a[1], m[1], u); cb = Lerp(a[2], m[2], u); }
        else { float u = (t - 0.5f) * 2f; cr = Lerp(m[0], c[0], u); cg = Lerp(m[1], c[1], u); cb = Lerp(m[2], c[2], u); }
        if (style == "armor" && sat > 0.5f && (hue < 15f || hue > 330f)) { cr = 0.36f; cg = 0.10f; cb = 0.46f; }   // 呪いの紫
        if (style == "necro" && skinTone) { float k = 0.75f + 0.3f * mx; cr = 0.42f * k; cg = 0.47f * k; cb = 0.40f * k; }   // 青白い肌
        if (style == "necro" && vivid) { cr *= 0.7f; cg *= 0.7f; cb *= 0.7f; }
        if (style == "eaxe" && skinTone) { float k = 0.70f + 0.3f * mx; cr = 0.62f * k; cg = 0.36f * k; cb = 0.26f * k; }   // 赤らんだ褐色の肌
        if (style == "earcher" && skinTone) { float k = 0.75f + 0.3f * mx; cr = 0.80f * k; cg = 0.68f * k; cb = 0.58f * k; }
        if (style == "erider" && skinTone) { float k = 0.70f + 0.3f * mx; cr = 0.58f * k; cg = 0.38f * k; cb = 0.22f * k; }   // 日焼けの銅色
        if (style == "eflame" && skinTone) { float k = 0.70f + 0.3f * mx; cr = 0.66f * k; cg = 0.44f * k; cb = 0.30f * k; }
        if (style == "eflame" && vivid) { cr = 0.98f; cg = 0.70f; cb = 0.18f; }   // 差し色 → 金
        if (style == "eshade" && vivid) { cr = 0.40f; cg = 0.14f; cb = 0.62f; }    // 赤い帯 → 紫
        if (style == "eshade" && skinTone) { float k = 0.6f + 0.3f * mx; cr = 0.30f * k; cg = 0.30f * k; cb = 0.38f * k; }
        if (style == "elibrarian" && skinTone) { float k = 0.75f + 0.3f * mx; cr = 0.62f * k; cg = 0.62f * k; cb = 0.64f * k; }
        if (style == "elibrarian" && vivid) { cr = 0.70f; cg = 0.55f; cb = 0.18f; }   // 差し色 → 古い金
        if (style == "warden")
        {
            if (sat > 0.28f && hue < 45f && mx < 0.78f && !skinTone) { cr = 0.22f; cg = 0.13f; cb = 0.08f; }   // 革紐
            float n = ValueNoise(x, y, 18f, seed + 3);
            float r = Smooth(0.66f, 0.82f, n) * 0.45f;   // 錆の斑
            cr += (0.48f - cr) * r; cg += (0.24f - cg) * r; cb += (0.10f - cb) * r;
        }
        if (style == "gargoyle")
        {
            float n = ValueNoise(x, y, 26f, seed + 5);
            float m2 = Smooth(0.72f, 0.88f, n) * 0.35f;   // 苔の薄い斑
            cr += (0.28f - cr) * m2; cg += (0.34f - cg) * m2; cb += (0.22f - cb) * m2;
        }
    }

    public static void Build(string src, string dst, int seed, string style)
    {
        bool stone = (style == "stone");
        bool ruins = (style != "sand" && style != "stone");
        int crackCount = style == "gargoyle" ? 48 : style == "skeleton" ? 12 : style == "armor" ? 8 : style == "necro" ? 0 : style == "warden" ? 16 : style == "eaxe" ? 6 : style.StartsWith("e") ? 0 : 34;
        Bitmap bmp = new Bitmap(src);
        int w = bmp.Width, h = bmp.Height;
        Bitmap res = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        Rectangle r = new Rectangle(0, 0, w, h);
        BitmapData sd = bmp.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        byte[] px = new byte[w * h * 4];
        Marshal.Copy(sd.Scan0, px, 0, px.Length);
        bmp.UnlockBits(sd);
        byte[] outp = new byte[px.Length];
        bool[] used = new bool[w * h];

        for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            int i = (y * w + x) * 4;
            float b = px[i] / 255f, g = px[i + 1] / 255f, rr = px[i + 2] / 255f;
            float mx = Math.Max(rr, Math.Max(g, b)), mn = Math.Min(rr, Math.Min(g, b));
            float lum = 0.299f * rr + 0.587f * g + 0.114f * b;
            float sat = mx > 1e-3f ? (mx - mn) / mx : 0f;
            used[y * w + x] = lum > 0.2f;   // 背景（使っていない UV）は暗い灰

            float cr, cg, cb;
            // 8px の粒（ピクセル絵の大きさ）の明暗のむら ±6%
            float grain = 1f + (Hash(x / 8, y / 8, seed) - 0.5f) * 0.12f;
            if (lum < 0.2f)
            {
                cr = rr; cg = g; cb = b;   // 背景（使っていない UV）はそのまま
            }
            else if (stone)
                MapStone(x, y, seed, rr, g, b, lum, sat, mx, mn, out cr, out cg, out cb);
            else if (ruins)
                MapRuins(style, x, y, seed, rr, g, b, lum, sat, mx, mn, out cr, out cg, out cb);
            else if (sat > 0.30f && b > rr * 0.9f && b > g)
            {
                // 紫の差し色 → 金（元の明るさで濃淡）
                float k = 0.70f + 0.45f * mx;
                cr = 0.93f * k; cg = 0.71f * k; cb = 0.27f * k;
            }
            else if (lum > 0.90f)
            {
                // 白（目・胸の宝石・艶）→ 明るい砂岩（目は飾りの箱が光る）
                cr = 0.86f; cg = 0.70f; cb = 0.47f;
            }
            else
            {
                // 灰 → 砂岩の明暗
                float t = Smooth(0.18f, 0.80f, lum);
                cr = 0.38f + (0.88f - 0.38f) * t;
                cg = 0.27f + (0.71f - 0.27f) * t;
                cb = 0.16f + (0.47f - 0.16f) * t;
            }
            cr *= grain; cg *= grain; cb *= grain;
            outp[i] = (byte)Math.Max(0, Math.Min(255, cb * 255f));
            outp[i + 1] = (byte)Math.Max(0, Math.Min(255, cg * 255f));
            outp[i + 2] = (byte)Math.Max(0, Math.Min(255, cr * 255f));
            outp[i + 3] = 255;
        }

        // ひび割れ：使っている所から始めて、ジグザグに 6〜10 歩。太さ 3px、暗い焦げ茶
        Random rng = new Random(seed);
        int cracks = 0;
        for (int tries = 0; tries < 4000 && cracks < crackCount; ++tries)
        {
            int x0 = rng.Next(w), y0 = rng.Next(h);
            if (!used[y0 * w + x0]) continue;
            ++cracks;
            double ang = rng.NextDouble() * Math.PI * 2.0;
            double cx = x0, cy = y0;
            int steps = 4 + rng.Next(4);
            for (int s = 0; s < steps; ++s)
            {
                ang += (rng.NextDouble() - 0.5) * 0.8;
                double len = 10 + rng.Next(12);
                double nx = cx + Math.Cos(ang) * len, ny = cy + Math.Sin(ang) * len;
                int n = (int)(len * 2);
                for (int k = 0; k <= n; ++k)
                {
                    double t = (double)k / n;
                    int px0 = (int)(cx + (nx - cx) * t), py0 = (int)(cy + (ny - cy) * t);
                    for (int oy = -1; oy <= 1; ++oy)
                    for (int ox = -1; ox <= 1; ++ox)
                    {
                        int qx = px0 + ox, qy = py0 + oy;
                        if (qx < 0 || qy < 0 || qx >= w || qy >= h || !used[qy * w + qx]) continue;
                        int j = (qy * w + qx) * 4;
                        bool core = (ox == 0 && oy == 0);
                        float f = core ? 0.55f : 0.80f;   // 芯は濃く、縁は少し（真っ黒だと髪の毛のように見えた）
                        outp[j] = (byte)(outp[j] * f); outp[j + 1] = (byte)(outp[j + 1] * f); outp[j + 2] = (byte)(outp[j + 2] * f);
                    }
                }
                cx = nx; cy = ny;
                if (cx < 0 || cy < 0 || cx >= w || cy >= h || !used[(int)cy * w + (int)cx]) break;
            }
        }

        BitmapData dd = res.LockBits(r, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        Marshal.Copy(outp, 0, dd.Scan0, outp.Length);
        res.UnlockBits(dd);
        res.Save(dst, ImageFormat.Png);
        bmp.Dispose(); res.Dispose();
    }
}
"@
$root = Get-Location
$src = Join-Path $root $Source
$dst = Join-Path $root $Out
New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
[BossSkin]::Build($src, $dst, $Seed, $Style)
"wrote $dst"
