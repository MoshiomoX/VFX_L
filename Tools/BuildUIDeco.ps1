# ============================================================
# BuildUIDeco.ps1
# UI ornaments (line art) for the fantasy UI: downloads the public-domain /
# CC0 sources, crops them and converts dark-lines-on-light into WHITE lines on
# transparency (straight alpha), so the game can tint them with a color
# (gold / silver / arcane cyan per item category).
#   -> VFX_L/Assets/Texture/UI/Deco/*.png   (sources in README.txt there)
#
# Usage (Windows PowerShell 5.1):
#   .\BuildUIDeco.ps1
# ============================================================
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Texture\UI\Deco')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class DecoBuilder
{
    // crop (x, y, w, h) of src, scale to outW wide, lines -> white with alpha.
    // alpha = srcAlpha * clamp((hi - luminance) / (hi - lo))
    public static void LineArt(string src, string dst, int x, int y, int w, int h, int outW, int lo, int hi)
    {
        LineArtFade(src, dst, x, y, w, h, outW, lo, hi, 0.0, 0.0);
    }

    // same, and fade the alpha out toward the left / bottom edge over that
    // fraction of the width / height (for pieces cut out of a longer band)
    public static void LineArtFade(string src, string dst, int x, int y, int w, int h, int outW, int lo, int hi,
        double fadeLeft, double fadeBottom)
    {
        using (var im = new Bitmap(src))
        {
            int outH = (int)Math.Round(h * (double)outW / w);
            using (var res = new Bitmap(outW, outH, PixelFormat.Format32bppArgb))
            {
                using (var g = Graphics.FromImage(res))
                {
                    g.Clear(Color.Transparent);
                    g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                    g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    g.DrawImage(im, new Rectangle(0, 0, outW, outH), new Rectangle(x, y, w, h), GraphicsUnit.Pixel);
                }
                var rect = new Rectangle(0, 0, outW, outH);
                var d = res.LockBits(rect, ImageLockMode.ReadWrite, PixelFormat.Format32bppArgb);
                int n = outW * outH * 4;
                var a = new byte[n];
                Marshal.Copy(d.Scan0, a, 0, n);
                for (int i = 0; i < n; i += 4)
                {
                    double lum = (a[i] + a[i + 1] + a[i + 2]) / 3.0;
                    double t = Math.Max(0.0, Math.Min(1.0, (hi - lum) / (double)(hi - lo)));
                    int px = (i / 4) % outW, py = (i / 4) / outW;
                    if (fadeLeft > 0.0) t *= Math.Min(1.0, px / (fadeLeft * outW));
                    if (fadeBottom > 0.0) t *= Math.Min(1.0, (outH - 1 - py) / (fadeBottom * outH));
                    a[i + 3] = (byte)Math.Round(a[i + 3] * t);
                    a[i] = 255; a[i + 1] = 255; a[i + 2] = 255;
                }
                Marshal.Copy(a, 0, d.Scan0, n);
                res.UnlockBits(d);
                res.Save(dst, ImageFormat.Png);
            }
        }
    }
}
'@

$cache = Join-Path $env:TEMP 'vfxl_uideco'
New-Item -ItemType Directory -Force $cache, $OutDir | Out-Null

function Fetch([string]$url, [string]$name) {
    $p = Join-Path $cache $name
    # Wikimedia answers 429 to generic user agents; its policy asks for a descriptive one
    if (-not (Test-Path $p)) { Invoke-WebRequest -UseBasicParsing -UserAgent 'VFX_L-asset-build/1.0 (hobby game; https://github.com/MoshiomoX/VFX_L)' -Uri $url -OutFile $p }
    return $p
}

# "4 Summoning Circles" by Luke.RUSTLTD (OpenGameArt, CC0), 1000x1000 black lines on white
$c6 = Fetch 'https://opengameart.org/sites/default/files/circle6.png' 'circle6.png'
$c7 = Fetch 'https://opengameart.org/sites/default/files/circle7.png' 'circle7.png'
# Meyer, "Handbook of Ornament" (1898) plate 166 "Halbkreis-Zwickel" (Wikimedia Commons, public domain), 1301x2038 scan
$meyer = Fetch 'https://upload.wikimedia.org/wikipedia/commons/9/94/Orna166-Halbkreis-Zwickel.png' 'Orna166-Halbkreis-Zwickel.png'
# "Ornamental Text Dividers" (Openclipart 224787, public domain), 1894x1595 black on transparent
$div = Fetch 'https://openclipart.org/image/2000px/224787' 'dividers224787.png'

[DecoBuilder]::LineArt($c7, (Join-Path $OutDir 'MagicCircleStar.png'), 0, 0, 1000, 1000, 512, 60, 235)
[DecoBuilder]::LineArt($c6, (Join-Path $OutDir 'MagicCircleFlower.png'), 0, 0, 1000, 1000, 512, 60, 235)
# figure 6: the interlaced-knot corner, cut as a TOP-RIGHT corner piece (mirrored in code for the
# others). Its bands run on past the cut, so they fade out toward the inner (left / bottom) edges
[DecoBuilder]::LineArtFade($meyer, (Join-Path $OutDir 'CornerKnot.png'), 872, 1370, 334, 334, 256, 70, 215, 0.35, 0.35)
# 4th divider (fleur-de-lis) and 5th (thin, knot in the middle)
[DecoBuilder]::LineArt($div, (Join-Path $OutDir 'DividerFleur.png'), 70, 588, 1784, 179, 1024, 60, 235)
[DecoBuilder]::LineArt($div, (Join-Path $OutDir 'DividerThin.png'), 100, 842, 1720, 174, 1024, 60, 235)

Get-ChildItem $OutDir -Filter *.png | ForEach-Object { Write-Host ("{0,-22} {1,8} bytes" -f $_.Name, $_.Length) }
