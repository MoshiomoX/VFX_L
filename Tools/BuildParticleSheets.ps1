# ============================================================
# BuildParticleSheets.ps1
# Packs downloaded particle texture packs into atlases for the GPU particle
# system (one PNG + one manifest JSON per sheet) under
#   VFX_L/Assets/Particles/Sheets/
#
# Every output texel is PREMULTIPLIED alpha (rgb already multiplied by a),
# stored in a plain PNG. The particle PS knows which sheets are premultiplied
# from the manifest ("premultiplied": true), so image viewers show darker
# edges than the game does. That is expected.
#
# Usage (Windows PowerShell 5.1):
#   .\BuildParticleSheets.ps1 -Sheet KenneyParticles -Source <unzipped kenney_particle-pack>
#   .\BuildParticleSheets.ps1 -Sheet KenneySmoke     -Source <unzipped kenney_smoke-particles>
# Sources: see Assets/Particles/Sheets/README.md
# ============================================================
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('KenneyParticles', 'KenneySmoke')]
    [string]$Sheet,

    [Parameter(Mandatory = $true)]
    [string]$Source,

    [string]$OutDir = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Particles\Sheets')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class SheetBuilder
{
    // mode 0 : opaque white-on-black source -> white with alpha = max(r,g,b)
    //          (premultiplied: rgb = a = luminance). Tint comes from the particle colour.
    // mode 1 : coloured straight-alpha source, smooth resample -> premultiplied
    // mode 2 : pixel art, nearest neighbour at an integer scale (never shrinks
    //          below 1x unless it does not fit) -> premultiplied
    public static void Build(string[] files, int cols, int rows, int cell, int pad,
                             int mode, string outPath)
    {
        int w = cols * cell, h = rows * cell;
        byte[] atlas = new byte[w * h * 4];   // BGRA premultiplied

        for (int i = 0; i < files.Length; ++i)
        {
            int cx = (i % cols) * cell, cy = (i / cols) * cell;
            using (Bitmap src = new Bitmap(files[i]))
            using (Bitmap tmp = new Bitmap(cell, cell, PixelFormat.Format32bppPArgb))
            {
                using (Graphics g = Graphics.FromImage(tmp))
                {
                    g.Clear(mode == 0 ? Color.Black : Color.Transparent);
                    g.CompositingMode = CompositingMode.SourceOver;
                    int avail = cell - pad * 2;
                    float scale;
                    if (mode == 2)
                    {
                        // integer scale: 1x if it fits, otherwise shrink to fit
                        scale = Math.Max(1, (int)Math.Floor((float)avail / Math.Max(src.Width, src.Height)));
                        if (src.Width * scale > avail || src.Height * scale > avail)
                            scale = (float)avail / Math.Max(src.Width, src.Height);
                        g.InterpolationMode = InterpolationMode.NearestNeighbor;
                        g.PixelOffsetMode = PixelOffsetMode.Half;
                    }
                    else
                    {
                        scale = (float)avail / Math.Max(src.Width, src.Height);
                        g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                        g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                    }
                    int dw = (int)Math.Round(src.Width * scale), dh = (int)Math.Round(src.Height * scale);
                    int dx = (cell - dw) / 2, dy = (cell - dh) / 2;
                    using (ImageAttributes ia = new ImageAttributes())
                    {
                        ia.SetWrapMode(WrapMode.TileFlipXY);   // no dark fringe at the source border
                        g.DrawImage(src, new Rectangle(dx, dy, dw, dh), 0, 0, src.Width, src.Height,
                                    GraphicsUnit.Pixel, ia);
                    }
                }

                // PArgb read = premultiplied BGRA
                BitmapData bd = tmp.LockBits(new Rectangle(0, 0, cell, cell),
                    ImageLockMode.ReadOnly, PixelFormat.Format32bppPArgb);
                byte[] px = new byte[cell * cell * 4];
                for (int y = 0; y < cell; ++y)
                    Marshal.Copy(IntPtr.Add(bd.Scan0, y * bd.Stride), px, y * cell * 4, cell * 4);
                tmp.UnlockBits(bd);

                for (int y = 0; y < cell; ++y)
                {
                    for (int x = 0; x < cell; ++x)
                    {
                        int s = (y * cell + x) * 4;
                        int d = ((cy + y) * w + (cx + x)) * 4;
                        byte b = px[s], gg = px[s + 1], r = px[s + 2], a = px[s + 3];
                        if (mode == 0)
                        {
                            byte v = Math.Max(r, Math.Max(gg, b));
                            atlas[d] = v; atlas[d + 1] = v; atlas[d + 2] = v; atlas[d + 3] = v;
                        }
                        else
                        {
                            atlas[d] = b; atlas[d + 1] = gg; atlas[d + 2] = r; atlas[d + 3] = a;
                        }
                    }
                }
            }
        }

        // Written as plain Argb bytes so GDI+ stores the premultiplied values unchanged
        using (Bitmap outBmp = new Bitmap(w, h, PixelFormat.Format32bppArgb))
        {
            BitmapData od = outBmp.LockBits(new Rectangle(0, 0, w, h),
                ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            for (int y = 0; y < h; ++y)
                Marshal.Copy(atlas, y * w * 4, IntPtr.Add(od.Scan0, y * od.Stride), w * 4);
            outBmp.UnlockBits(od);
            outBmp.Save(outPath, ImageFormat.Png);
        }
    }
}
'@

# ------------------------------------------------------------
# frame names -> groups (consecutive frames sharing a prefix)
# ------------------------------------------------------------
function Get-Groups([string[]]$names) {
    $groups = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $names.Count; ++$i) {
        $g = ($names[$i] -replace '[_ ]?\d+$', '')
        if ($groups.Count -gt 0 -and $groups[$groups.Count - 1].name -eq $g) {
            $groups[$groups.Count - 1].count++
        } else {
            [void]$groups.Add([ordered]@{ name = $g; start = $i; count = 1 })
        }
    }
    return , $groups
}

function Write-Manifest($path, $name, $texture, $rows, $cols, $filter, $frames, $groups, $extra) {
    $m = [ordered]@{
        name          = $name
        texture       = $texture
        rows          = $rows
        cols          = $cols
        filter        = $filter
        premultiplied = $true
        frames        = @($frames)
        groups        = @($groups)
    }
    if ($extra) { foreach ($k in $extra.Keys) { $m[$k] = $extra[$k] } }
    $json = $m | ConvertTo-Json -Depth 6
    [System.IO.File]::WriteAllText($path, $json, (New-Object System.Text.UTF8Encoding($false)))
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

switch ($Sheet) {
    'KenneyParticles' {
        # the black-background set is the cleanest luminance source; skip Rotated/
        $dir = Join-Path $Source 'PNG (Black background)'
        $files = Get-ChildItem $dir -File -Filter *.png | Sort-Object Name
        $cols = 10; $rows = [math]::Ceiling($files.Count / $cols); $cell = 256
        $png = Join-Path $OutDir 'KenneyParticles.png'
        [SheetBuilder]::Build([string[]]($files.FullName), $cols, $rows, $cell, 0, 0, $png)
        $names = @($files | ForEach-Object { $_.BaseName })
        Write-Manifest (Join-Path $OutDir 'KenneyParticles.json') 'Kenney Particles' 'KenneyParticles.png' `
            $rows $cols 'linear' $names (Get-Groups $names) $null
    }
    'KenneySmoke' {
        $files = Get-ChildItem (Join-Path $Source 'PNG') -Recurse -File -Filter *.png |
            Sort-Object { $_.Directory.Name }, Name
        $cols = 9; $rows = [math]::Ceiling($files.Count / $cols); $cell = 256
        $png = Join-Path $OutDir 'KenneySmoke.png'
        [SheetBuilder]::Build([string[]]($files.FullName), $cols, $rows, $cell, 4, 1, $png)
        $names = @($files | ForEach-Object { $_.BaseName })
        Write-Manifest (Join-Path $OutDir 'KenneySmoke.json') 'Kenney Smoke' 'KenneySmoke.png' `
            $rows $cols 'linear' $names (Get-Groups $names) $null
    }
}

Write-Host "done: $Sheet -> $OutDir"
