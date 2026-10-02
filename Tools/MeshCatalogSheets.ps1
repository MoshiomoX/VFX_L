# ============================================================
# MeshCatalogSheets.ps1
# Contact sheets for VFX_L/Assets/VFX/Mesh/README.md (2026-10-01).
#   powershell -File Tools/MeshCatalogSheets.ps1 -Dir <MeshCatalogRender.py out dir> [-Cols 3] [-Rows 6]
# Reads <Dir>/stats.tsv and <Dir>/<idx>_a.png / _b.png and writes <Dir>/sheet_N.png:
# one cell per model = "#idx file" + "size  verts  uv  textures" + 3/4 view + top view.
# ============================================================
param([string]$Dir, [int]$Cols = 3, [int]$Rows = 6)
Add-Type -AssemblyName System.Drawing
# ($Rows is the parameter; PowerShell names are case-insensitive, so the data is $data)
$data = Import-Csv -Path (Join-Path $Dir "stats.tsv") -Delimiter "`t" -Encoding UTF8
$cellW = 480; $img = 240; $labelH = 44; $cellH = $img + $labelH
$per = $Cols * $Rows
$font1 = New-Object System.Drawing.Font("Consolas", 11, [System.Drawing.FontStyle]::Bold)
$font2 = New-Object System.Drawing.Font("Consolas", 9)
$white = [System.Drawing.Brushes]::White
$gray = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 190, 190, 190))
$red = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 110, 110))
$sheetCount = [Math]::Ceiling($data.Count / $per)
for ($s = 0; $s -lt $sheetCount; $s++) {
  $bmp = New-Object System.Drawing.Bitmap ($cellW * $Cols + 6 * ($Cols - 1)), ($cellH * $Rows + 6 * ($Rows - 1))
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.Clear([System.Drawing.Color]::FromArgb(255, 40, 40, 44))
  for ($k = 0; $k -lt $per; $k++) {
    $n = $s * $per + $k
    if ($n -ge $data.Count) { break }
    $r = $data[$n]
    $x = ($k % $Cols) * ($cellW + 6); $y = [Math]::Floor($k / $Cols) * ($cellH + 6)
    $g.FillRectangle([System.Drawing.Brushes]::Black, $x, $y, $cellW, $labelH)
    $g.DrawString(("#{0} {1}" -f $r.idx, $r.file), $font1, $white, $x + 4, $y + 2)
    if ($r.status -eq "ok") {
      $info = "{0} x {1} x {2}  v{3}  {4}  {5}" -f $r.dx, $r.dy, $r.dz, $r.verts, $r.uv, $r.textures
      $g.DrawString($info, $font2, $gray, $x + 4, $y + 24)
      foreach ($t in @("a", "b")) {
        $p = Join-Path $Dir ("{0:D3}_{1}.png" -f [int]$r.idx, $t)
        if (Test-Path $p) {
          $im = [System.Drawing.Image]::FromFile($p)
          $g.DrawImage($im, $x + ($(if ($t -eq "a") { 0 } else { $img })), $y + $labelH, $img, $img)
          $im.Dispose()
        }
      }
    } else {
      $g.DrawString($r.status, $font2, $red, $x + 4, $y + 24)
    }
  }
  $out = Join-Path $Dir ("sheet_{0}.png" -f ($s + 1))
  $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  $out
}
