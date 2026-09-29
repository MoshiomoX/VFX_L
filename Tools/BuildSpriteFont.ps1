# ============================================================
# BuildSpriteFont.ps1
# Builds a DirectXTK .spritefont straight from a TTF/OTF file (no need to
# install the font, unlike MakeSpriteFont which looks fonts up by name).
#
# - Glyphs are rendered with WPF (grayscale antialiasing), white, and packed
#   into one atlas.
# - Texture is BC2 (DXGI_FORMAT_BC2_UNORM = 74), premultiplied: alpha is the
#   4-bit BC2 alpha, the color block is a white/black ramp of the same
#   coverage (same scheme as MakeSpriteFont's CompressedMono).
# - Only code points that fall in -Ranges AND exist in the font are written.
#   The default ranges cover ASCII, Latin-1, punctuation, arrows, math,
#   enclosed numbers, shapes/symbols, CJK symbols + kana, CJK ideographs and
#   the full-width forms.
# - Line spacing = (ascent + descent) of the font at -EmPx.
#
# Usage (Windows PowerShell 5.1):
#   .\BuildSpriteFont.ps1 -Font ..\VFX_L\Assets\Fonts\YujiSyuku-Regular.ttf `
#                         -Out  ..\VFX_L\Assets\Fonts\YujiSyuku.spritefont
# ============================================================
param(
    [Parameter(Mandatory = $true)]
    [string]$Font,

    [Parameter(Mandatory = $true)]
    [string]$Out,

    # glyph em size in pixels (NotoSansJP.spritefont was 32 px = 24 pt)
    [double]$EmPx = 32,

    [int]$AtlasWidth = 4096,

    [int[]]$Ranges = @(
        0x0020, 0x007E, 0x00A0, 0x00FF, 0x2000, 0x206F, 0x2190, 0x21FF,
        0x2200, 0x22FF, 0x2460, 0x24FF, 0x25A0, 0x26FF, 0x3000, 0x30FF,
        0x4E00, 0x9FFF, 0xFF00, 0xFFEF)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase, System.Xaml

Add-Type -ReferencedAssemblies PresentationCore, WindowsBase, System.Xaml -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.IO;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;

public static class SpriteFontBuilder
{
    class Item
    {
        public int Code;
        public ushort Glyph;
        public double Advance;
        public int X0, Y0, W, H;   // box in line space (pen at x=0, line top at y=0)
        public int AtlasX, AtlasY;
        public bool Blank;
    }

    public static string Build(string ttfPath, string outPath, double emPx, int atlasWidth, int[] ranges)
    {
        var gt = new GlyphTypeface(new Uri(Path.GetFullPath(ttfPath)));
        double ascent = gt.Baseline * emPx;
        double lineSpacing = gt.Height * emPx;

        // ---- pick code points ----
        var items = new List<Item>();
        foreach (var kv in gt.CharacterToGlyphMap)
        {
            int c = kv.Key;
            bool inRange = false;
            for (int i = 0; i + 1 < ranges.Length; i += 2)
                if (c >= ranges[i] && c <= ranges[i + 1]) { inRange = true; break; }
            if (!inRange) continue;
            items.Add(new Item { Code = c, Glyph = kv.Value, Advance = gt.AdvanceWidths[kv.Value] * emPx });
        }
        items.Sort((a, b) => a.Code.CompareTo(b.Code));

        // ---- measure ink boxes (1 px padding all round) ----
        foreach (var it in items)
        {
            var run = MakeRun(gt, it.Glyph, emPx, new Point(0, 0));
            Rect ink = run.ComputeInkBoundingBox();
            if (ink.IsEmpty || ink.Width < 0.01 || ink.Height < 0.01) { it.Blank = true; continue; }
            int x0 = (int)Math.Floor(ink.Left) - 1;
            int x1 = (int)Math.Ceiling(ink.Right) + 1;
            int y0 = (int)Math.Floor(ascent + ink.Top) - 1;
            int y1 = (int)Math.Ceiling(ascent + ink.Bottom) + 1;
            it.X0 = x0; it.Y0 = y0; it.W = x1 - x0; it.H = y1 - y0;
        }

        // ---- shelf packing, tallest first. (0,0)-(2,2) stays empty for blank glyphs ----
        var order = new List<Item>(items);
        order.RemoveAll(i => i.Blank);
        order.Sort((a, b) => b.H != a.H ? b.H.CompareTo(a.H) : a.Code.CompareTo(b.Code));
        int penX = 4, penY = 0, shelfH = 4;
        foreach (var it in order)
        {
            if (penX + it.W > atlasWidth) { penY += shelfH; penX = 0; shelfH = 0; }
            it.AtlasX = penX; it.AtlasY = penY;
            penX += it.W;
            shelfH = Math.Max(shelfH, it.H);
        }
        int atlasHeight = (penY + shelfH + 3) / 4 * 4;

        // ---- render every glyph, white (RenderTargetBitmap always uses grayscale AA) ----
        var dv = new DrawingVisual();
        using (var dc = dv.RenderOpen())
        {
            foreach (var it in order)
            {
                var origin = new Point(it.AtlasX - it.X0, it.AtlasY - it.Y0 + ascent);
                dc.DrawGlyphRun(Brushes.White, MakeRun(gt, it.Glyph, emPx, origin));
            }
        }
        var rtb = new RenderTargetBitmap(atlasWidth, atlasHeight, 96, 96, PixelFormats.Pbgra32);
        rtb.Render(dv);
        var px = new byte[atlasWidth * atlasHeight * 4];
        rtb.CopyPixels(px, atlasWidth * 4, 0);

        // ---- BC2 encode ----
        int bw = atlasWidth / 4, bh = atlasHeight / 4;
        var tex = new byte[bw * bh * 16];
        for (int by = 0; by < bh; by++)
        for (int bx = 0; bx < bw; bx++)
        {
            ulong alphaBits = 0;
            uint colorIdx = 0;
            for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
            {
                int a = px[((by * 4 + y) * atlasWidth + (bx * 4 + x)) * 4 + 3];
                int a4 = (a * 15 + 127) / 255;
                int lvl = (a * 3 + 127) / 255;              // 0..3 = 0, 1/3, 2/3, 1
                uint idx = lvl == 3 ? 0u : lvl == 2 ? 2u : lvl == 1 ? 3u : 1u;
                int p = y * 4 + x;
                alphaBits |= (ulong)a4 << (p * 4);
                colorIdx |= idx << (p * 2);
            }
            int o = (by * bw + bx) * 16;
            for (int i = 0; i < 8; i++) tex[o + i] = (byte)(alphaBits >> (i * 8));
            tex[o + 8] = 0xFF; tex[o + 9] = 0xFF;          // color0 = white (565)
            tex[o + 10] = 0x00; tex[o + 11] = 0x00;        // color1 = black
            for (int i = 0; i < 4; i++) tex[o + 12 + i] = (byte)(colorIdx >> (i * 8));
        }

        // ---- write .spritefont ----
        using (var bwr = new BinaryWriter(File.Create(outPath)))
        {
            bwr.Write(System.Text.Encoding.ASCII.GetBytes("DXTKfont"));
            bwr.Write((uint)items.Count);
            foreach (var it in items)
            {
                int l, t, r, b; float xo, yo, xa;
                if (it.Blank) { l = 0; t = 0; r = 1; b = 1; xo = 0; yo = 0; xa = (float)(it.Advance - 1); }
                else
                {
                    l = it.AtlasX; t = it.AtlasY; r = it.AtlasX + it.W; b = it.AtlasY + it.H;
                    xo = it.X0; yo = it.Y0; xa = (float)(it.Advance - it.X0 - it.W);
                }
                bwr.Write((uint)it.Code);
                bwr.Write(l); bwr.Write(t); bwr.Write(r); bwr.Write(b);
                bwr.Write(xo); bwr.Write(yo); bwr.Write(xa);
            }
            bwr.Write((float)lineSpacing);
            bwr.Write((uint)'?');
            bwr.Write((uint)atlasWidth);
            bwr.Write((uint)atlasHeight);
            bwr.Write((uint)74);                           // DXGI_FORMAT_BC2_UNORM
            bwr.Write((uint)(bw * 16));                    // bytes per block row
            bwr.Write((uint)bh);                           // block rows
            bwr.Write(tex);
        }
        return string.Format("{0} glyphs, atlas {1}x{2}, line spacing {3:F2}, ascent {4:F2}",
            items.Count, atlasWidth, atlasHeight, lineSpacing, ascent);
    }

    static GlyphRun MakeRun(GlyphTypeface gt, ushort glyph, double emPx, Point origin)
    {
        return new GlyphRun(gt, 0, false, emPx, 1.0f,
            new ushort[] { glyph }, origin, new double[] { gt.AdvanceWidths[glyph] * emPx },
            null, null, null, null, null, null);
    }
}
'@

$fontPath = (Resolve-Path $Font).Path
$outPath = [IO.Path]::GetFullPath((Join-Path (Get-Location) $Out))
[SpriteFontBuilder]::Build($fontPath, $outPath, $EmPx, $AtlasWidth, $Ranges)
Write-Host "wrote $outPath ($((Get-Item $outPath).Length) bytes)"
