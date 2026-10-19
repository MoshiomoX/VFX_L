# BuildWindStreaks.ps1
# Wind streak texture for the sand tornado / gust (2026-10-19). Tileable 512x512 greyscale PNG:
# thin vertical streamlines (gaussian cross-section) at random u, broken into segments by periodic
# modulation along v (integer frequencies keep it tileable). The shell mesh twists uv.x along the
# height and SwarmTornadoPS adds a slant, so on screen the lines read as diagonal flow lines.
# Output: Assets/VFX/Tex/WindStreaks.png (run from VFX_L/VFX_L).
param([int]$Size = 512, [int]$Seed = 7, [string]$Out = "Assets/VFX/Tex/WindStreaks.png")
Add-Type -AssemblyName System.Drawing
$rng = New-Object System.Random $Seed
$N = $Size
$img = New-Object float[] ($N * $N)

function Streak($u0, $sigma, $amp, $f1, $f2, $ph1, $ph2, $cut) {
    $w = [Math]::Ceiling($sigma * 4 * $N)
    $cx = $u0 * $N
    for ($y = 0; $y -lt $N; $y++) {
        $v = $y / $N
        # segments along the line: product of two periodic waves, thresholded softly
        $m = (0.5 + 0.5 * [Math]::Sin(2 * [Math]::PI * ($f1 * $v + $ph1))) * (0.5 + 0.5 * [Math]::Sin(2 * [Math]::PI * ($f2 * $v + $ph2)))
        $seg = [Math]::Max(0.0, ($m - $cut) / (1.0 - $cut))
        if ($seg -le 0) { continue }
        $seg = [Math]::Sqrt($seg)
        for ($dx = -$w; $dx -le $w; $dx++) {
            $x = (($cx + $dx) % $N + $N) % $N
            $xi = [int][Math]::Floor($x)
            $d = ($xi + 0.5 - $cx) / $N
            # wrap distance
            if ($d -gt 0.5) { $d -= 1 } elseif ($d -lt -0.5) { $d += 1 }
            $g = [Math]::Exp(-0.5 * ($d * $d) / ($sigma * $sigma))
            $img[$y * $N + $xi] += $amp * $g * $seg
        }
    }
}

# main streamlines: long (one or two gaps per tile), fairly bright
# (2026-10-19 user: "the lines are far too thin, draw them thicker" -> sigma 0.004-0.010 -> 0.011-0.020, fewer lines)
for ($i = 0; $i -lt 16; $i++) {
    Streak $rng.NextDouble() (0.011 + 0.009 * $rng.NextDouble()) (0.8 + 0.3 * $rng.NextDouble()) `
        1 (1 + $rng.Next(0, 2)) $rng.NextDouble() $rng.NextDouble() 0.06
}
# thinner faint lines in between, a little shorter
for ($i = 0; $i -lt 24; $i++) {
    Streak $rng.NextDouble() (0.004 + 0.004 * $rng.NextDouble()) (0.3 + 0.25 * $rng.NextDouble()) `
        1 (1 + $rng.Next(0, 3)) $rng.NextDouble() $rng.NextDouble() 0.12
}

$bmp = New-Object System.Drawing.Bitmap $N, $N, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$rect = New-Object System.Drawing.Rectangle 0, 0, $N, $N
$data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly, $bmp.PixelFormat)
$bytes = New-Object byte[] ($data.Stride * $N)
for ($y = 0; $y -lt $N; $y++) {
    for ($x = 0; $x -lt $N; $x++) {
        $v = [Math]::Min(1.0, $img[$y * $N + $x])
        $b = [byte][Math]::Round($v * 255)
        $o = $y * $data.Stride + $x * 4
        $bytes[$o] = $b; $bytes[$o + 1] = $b; $bytes[$o + 2] = $b; $bytes[$o + 3] = 255
    }
}
[System.Runtime.InteropServices.Marshal]::Copy($bytes, 0, $data.Scan0, $bytes.Length)
$bmp.UnlockBits($data)
$full = Join-Path (Get-Location) $Out
$bmp.Save($full, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Output "wrote $full"
