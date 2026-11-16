# BuildToonBall.ps1 - generates Assets/Particles/Sheets/ToonBall.png (one 256px cell)
# A single round ball for toon particles: alpha follows a sphere height (1 at the centre, 0.25 on the outline,
# then a short falloff to 0), so the toon cut gives a clean circle and the toon bands become rings / sun shading.
# Bigger shapes (explosions) are built by placing several of these particles, not by painting clusters into the texture.
# White rgb, premultiplied (rgb = alpha), like the other sheets.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools/BuildToonBall.ps1
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root "VFX_L\Assets\Particles\Sheets\ToonBall.png"

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class ToonBallGen {
    public static void Build(string path) {
        const int N = 256;
        const double R = 0.78, fall = 0.14;   // outline radius and outer falloff (cell half-size = 1)
        var px = new byte[N * N * 4];
        for (int y = 0; y < N; ++y)
        for (int x = 0; x < N; ++x) {
            double u = (x + 0.5) / N * 2 - 1, v = (y + 0.5) / N * 2 - 1;
            double d = Math.Sqrt(u * u + v * v);
            double a;
            if (d <= R) a = 0.25 + 0.75 * Math.Sqrt(1.0 - (d / R) * (d / R));
            else a = 0.25 * Math.Max(0.0, 1.0 - (d - R) / fall);
            byte b = (byte)Math.Round(Math.Min(1.0, a) * 255);
            int o = (y * N + x) * 4;
            px[o] = b; px[o + 1] = b; px[o + 2] = b; px[o + 3] = b;   // BGRA, premultiplied white
        }
        using (var bmp = new Bitmap(N, N, PixelFormat.Format32bppArgb)) {
            var data = bmp.LockBits(new Rectangle(0, 0, N, N), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            Marshal.Copy(px, 0, data.Scan0, px.Length);
            bmp.UnlockBits(data);
            bmp.Save(path, ImageFormat.Png);
        }
    }
}
"@
[ToonBallGen]::Build($out)
"wrote $out"
