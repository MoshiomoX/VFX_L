# ============================================================
# BuildChestUI.ps1
# The wooden chest of the spellbook (right side of the backpack screen).
# Everything is drawn procedurally here (no downloaded sources):
#   ChestFrame.png  512x512 nine-slice frame: walnut boards, iron corner
#                   plates with brass rivets, transparent centre
#                   (border = 128 px on every side)
#   ChestBack.png   512x512 inner back wall: vertical planks, grooves, nails,
#                   inner shadow toward the frame (stronger under the top)
#   ChestLock.png   128x160 brass hasp with a keyhole (hangs from the front board)
# Colors are sRGB; the game loads these files as *_SRGB so the sampler decodes
# them to linear (the UI is composed in the linear HDR buffer).
# Alpha is straight; transparent pixels keep the wood color (clean filtering).
#   -> VFX_L/Assets/Texture/UI/Chest/*.png
#
# Usage (Windows PowerShell 5.1):
#   .\BuildChestUI.ps1
# ============================================================
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Texture\UI\Chest')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class ChestBuilder
{
    // ---------------- noise ----------------
    static double Hash(int x, int y, int s)
    {
        unchecked
        {
            uint h = (uint)(x * 374761393 + y * 668265263 + s * 1442695041);
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= h >> 16;
            return (h & 0xFFFFFF) / 16777215.0;
        }
    }
    static double Smooth(double t) { return t * t * (3.0 - 2.0 * t); }
    static double Noise(double x, double y, int s)
    {
        int xi = (int)Math.Floor(x), yi = (int)Math.Floor(y);
        double fx = x - xi, fy = y - yi;
        double a = Hash(xi, yi, s), b = Hash(xi + 1, yi, s);
        double c = Hash(xi, yi + 1, s), d = Hash(xi + 1, yi + 1, s);
        double u = Smooth(fx), v = Smooth(fy);
        return (a + (b - a) * u) * (1.0 - v) + (c + (d - c) * u) * v;
    }
    static double Fbm(double x, double y, int s)
    {
        double sum = 0.0, amp = 0.5, f = 1.0;
        for (int i = 0; i < 4; i++) { sum += amp * Noise(x * f, y * f, s + i * 17); f *= 2.0; amp *= 0.5; }
        return sum / 0.9375;
    }
    static double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
    static double Lerp(double a, double b, double t) { return a + (b - a) * t; }

    // ---------------- materials (sRGB 0..1) ----------------
    // a = along the grain, c = across. dark / light = the two ends of the wood color
    static double[] Wood(double a, double c, int seed, double[] dark, double[] light)
    {
        double w = Fbm(a * 0.010, c * 0.080, seed);
        double warp = Fbm(a * 0.004, c * 0.020, seed + 5);
        double rings = 0.5 + 0.5 * Math.Sin(c * 0.50 + w * 8.0 + warp * 14.0);
        double fibre = Noise(a * 0.22, c * 1.4, seed + 9);
        double t = Clamp01(0.50 * w + 0.32 * rings * rings + 0.18 * fibre);
        return new double[] { Lerp(dark[0], light[0], t), Lerp(dark[1], light[1], t), Lerp(dark[2], light[2], t) };
    }

    static readonly double[] FrameDark  = { 0.20, 0.12, 0.07 };
    static readonly double[] FrameLight = { 0.47, 0.31, 0.18 };
    static readonly double[] BackDark   = { 0.11, 0.07, 0.045 };
    static readonly double[] BackLight  = { 0.27, 0.18, 0.11 };
    static readonly double[] Iron       = { 0.23, 0.23, 0.25 };
    static readonly double[] Brass      = { 0.72, 0.55, 0.26 };

    static void Mul(double[] c, double k) { c[0] *= k; c[1] *= k; c[2] *= k; }

    // brass rivet: returns true if (x, y) is on it, writes the color
    static bool Rivet(double x, double y, double cx, double cy, double r, double[] outC)
    {
        double dx = x - cx, dy = y - cy;
        double d = Math.Sqrt(dx * dx + dy * dy);
        if (d > r + 1.5) return false;
        if (d > r) { outC[0] = 0.05; outC[1] = 0.04; outC[2] = 0.03; return true; }   // dark ring
        double nz = Math.Sqrt(Math.Max(0.0, 1.0 - (d / r) * (d / r)));
        double nx = dx / r, ny = dy / r;
        // light from the upper left
        double lx = -0.5, ly = -0.6, lz = 0.62;
        double ndl = Math.Max(0.0, nx * lx + ny * ly + nz * lz);
        double spec = Math.Pow(ndl, 24.0);
        double k = 0.30 + 0.85 * ndl;
        outC[0] = Clamp01(Brass[0] * k + spec * 0.7);
        outC[1] = Clamp01(Brass[1] * k + spec * 0.6);
        outC[2] = Clamp01(Brass[2] * k + spec * 0.4);
        return true;
    }

    // ---------------- output ----------------
    static void Save(string path, int w, int h, double[] rgb, double[] alpha)
    {
        using (var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            var d = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            var a = new byte[w * h * 4];
            for (int i = 0; i < w * h; i++)
            {
                a[i * 4 + 0] = (byte)Math.Round(Clamp01(rgb[i * 3 + 2]) * 255.0);
                a[i * 4 + 1] = (byte)Math.Round(Clamp01(rgb[i * 3 + 1]) * 255.0);
                a[i * 4 + 2] = (byte)Math.Round(Clamp01(rgb[i * 3 + 0]) * 255.0);
                a[i * 4 + 3] = (byte)Math.Round(Clamp01(alpha[i]) * 255.0);
            }
            Marshal.Copy(a, 0, d.Scan0, a.Length);
            bmp.UnlockBits(d);
            bmp.Save(path, ImageFormat.Png);
        }
    }

    // ============================================================
    // frame (nine-slice, border B on every side)
    // ============================================================
    public static void Frame(string path)
    {
        const int S = 512, B = 128;
        var rgb = new double[S * S * 3];
        var alpha = new double[S * S];
        var rv = new double[3];

        for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        {
            int dT = y, dB = S - 1 - y, dL = x, dR = S - 1 - x;
            int m = Math.Min(Math.Min(dT, dB), Math.Min(dL, dR));

            // which board, and (along, across) on it. c = 0 at the outer edge
            double a, c; int seed; bool horiz;
            if (m == dT)      { a = x; c = dT; seed = 11; horiz = true; }
            else if (m == dB) { a = x; c = dB; seed = 23; horiz = true; }
            else if (m == dL) { a = y; c = dL; seed = 37; horiz = false; }
            else              { a = y; c = dR; seed = 51; horiz = false; }

            double[] col = Wood(a, c, seed, FrameDark, FrameLight);

            // top / bottom are two planks; sides are one
            if (horiz)
            {
                double g = Math.Abs(c - B * 0.5);
                if (g < 1.5) Mul(col, 0.35);
                else if (c > B * 0.5 + 1.5 && c < B * 0.5 + 3.5) Mul(col, 1.15);
            }
            // outer rim: dark outline + a lit bevel. inner rim: shadow into the box
            if (c < 2.5) Mul(col, 0.30);
            else if (c < 7.0) Mul(col, 1.22);
            if (c > B - 3.5) Mul(col, 0.30);
            else if (c > B - 14.0) Mul(col, Lerp(1.0, 0.62, (c - (B - 14.0)) / 10.5));

            // ---- iron corner plates ----
            int lx = Math.Min(dL, dR), ly = Math.Min(dT, dB);
            if (lx < B && ly < B)
            {
                double lim = B * 1.30, side = B * 0.97;
                double e = Math.Min((lim - (lx + ly)) / 1.4142, Math.Min(side - lx, side - ly));
                if (e > -2.0)
                {
                    if (e <= 0.0) { col = new double[] { 0.04, 0.035, 0.03 }; }   // outline
                    else
                    {
                        double n = Fbm(x * 0.15, y * 0.15, 71);
                        double sheen = 1.0 - 0.25 * ((lx + ly) / (double)(2 * B));
                        col = new double[] { Iron[0], Iron[1], Iron[2] };
                        Mul(col, (0.80 + 0.40 * n) * sheen);
                        if (e < 3.0) Mul(col, 1.55);        // bevel highlight
                        if (lx < 3 || ly < 3) Mul(col, 0.4); // outer rim stays dark
                    }
                    double r = B * 0.075;
                    if (Rivet(lx, ly, B * 0.24, B * 0.24, r, rv) ||
                        Rivet(lx, ly, B * 0.70, B * 0.20, r, rv) ||
                        Rivet(lx, ly, B * 0.20, B * 0.70, r, rv))
                        col = new double[] { rv[0], rv[1], rv[2] };
                }
            }

            int i = y * S + x;
            rgb[i * 3] = col[0]; rgb[i * 3 + 1] = col[1]; rgb[i * 3 + 2] = col[2];
            bool inner = x >= B && x < S - B && y >= B && y < S - B;
            alpha[i] = inner ? 0.0 : 1.0;
        }
        Save(path, S, S, rgb, alpha);
    }

    // ============================================================
    // back wall (stretched over the inside of the box)
    // ============================================================
    public static void Back(string path)
    {
        const int S = 512, planks = 5;
        var rgb = new double[S * S * 3];
        var alpha = new double[S * S];
        double pw = S / (double)planks;

        for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        {
            int p = (int)(x / pw);
            double px = x - p * pw;                       // x inside the plank
            double[] col = Wood(y + p * 97.0, px, 101 + p * 13, BackDark, BackLight);
            Mul(col, 0.88 + 0.24 * Hash(p, 0, 7));        // each plank a bit different

            // groove between planks
            if (px < 2.0 || px > pw - 1.0) Mul(col, 0.25);
            else if (px < 4.0) Mul(col, 1.12);

            // two nails at the top and bottom of each plank
            double[] nail = new double[3];
            double nx = p * pw + pw * 0.5;
            foreach (double ny in new double[] { S * 0.07, S * 0.93 })
            {
                double dx = x - nx, dy = y - ny, d = Math.Sqrt(dx * dx + dy * dy);
                if (d < 5.0)
                {
                    double k = d < 4.0 ? (0.55 - 0.1 * (dx + dy) / 4.0) : 0.12;
                    col = new double[] { 0.30 * k * 2.0, 0.29 * k * 2.0, 0.28 * k * 2.0 };
                }
            }

            // inner shadow: from the frame on every side, deeper under the top
            double dT = y, dB = S - 1 - y, dL = x, dR = S - 1 - x;
            double sh = 1.0;
            sh *= 1.0 - 0.70 * Math.Exp(-dT / 70.0);
            sh *= 1.0 - 0.45 * Math.Exp(-dB / 26.0);
            sh *= 1.0 - 0.50 * Math.Exp(-dL / 34.0);
            sh *= 1.0 - 0.50 * Math.Exp(-dR / 34.0);
            Mul(col, sh);

            int i = y * S + x;
            rgb[i * 3] = col[0]; rgb[i * 3 + 1] = col[1]; rgb[i * 3 + 2] = col[2];
            alpha[i] = 1.0;
        }
        Save(path, S, S, rgb, alpha);
    }

    // ============================================================
    // brass hasp with a keyhole
    // ============================================================
    public static void Lock(string path)
    {
        const int W = 128, H = 160;
        var rgb = new double[W * H * 3];
        var alpha = new double[W * H];
        double cx = W * 0.5, cyB = H - W * 0.5, rB = W * 0.5 - 6.0;
        var rv = new double[3];

        for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
        {
            // shield: rectangle on top, half circle at the bottom. e = distance to the edge
            double eRect = Math.Min(Math.Min(x - 6.0, W - 7.0 - x), y - 2.0);
            double e;
            if (y < cyB) e = eRect;
            else
            {
                double dx = x - cx, dy = y - cyB;
                e = Math.Min(rB - Math.Sqrt(dx * dx + dy * dy), eRect);
            }

            double[] col = { Brass[0], Brass[1], Brass[2] };
            double a = 0.0;
            if (e > -1.5)
            {
                a = e > 0.0 ? 1.0 : 1.0 + e / 1.5;
                double n = Fbm(x * 0.12, y * 0.12, 131);
                double grad = 1.05 - 0.45 * (y / (double)H);   // lit from above
                Mul(col, (0.85 + 0.3 * n) * grad);
                if (e < 1.5) col = new double[] { 0.08, 0.06, 0.03 };
                else if (e < 5.0) Mul(col, 1.35);
                else if (e < 8.0) Mul(col, 0.75);

                // keyhole
                double kx = x - cx, ky = y - H * 0.56;
                bool hole = kx * kx + ky * ky < 11.0 * 11.0
                    || (ky > 0.0 && ky < 30.0 && Math.Abs(kx) < 4.0 + ky * 0.12);
                if (hole) col = new double[] { 0.02, 0.015, 0.01 };
                else if (kx * kx + ky * ky < 14.0 * 14.0) Mul(col, 0.65);

                if (Rivet(x, y, W * 0.27, 18.0, 7.0, rv) || Rivet(x, y, W * 0.73, 18.0, 7.0, rv))
                    col = new double[] { rv[0], rv[1], rv[2] };
            }
            int i = y * W + x;
            rgb[i * 3] = col[0]; rgb[i * 3 + 1] = col[1]; rgb[i * 3 + 2] = col[2];
            alpha[i] = Clamp01(a);
        }
        Save(path, W, H, rgb, alpha);
    }
}
'@

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
[ChestBuilder]::Frame((Join-Path $OutDir 'ChestFrame.png'))
[ChestBuilder]::Back((Join-Path $OutDir 'ChestBack.png'))
[ChestBuilder]::Lock((Join-Path $OutDir 'ChestLock.png'))
Get-ChildItem $OutDir -Filter *.png | ForEach-Object { '{0}  {1} bytes' -f $_.Name, $_.Length }
