# ============================================================
# BuildKeyIcons.ps1
# Keyboard key glyphs for UI/InputGlyph ({K:Esc} etc. inside UI text).
#
#   KeyCap.png     128x128 blank white keycap (key body at 16..111 like every
#                  Kenney Input Prompts image). Built from Kenney's
#                  keyboard_f.png (Keyboard & Mouse / Double) by filling the
#                  cut-out letter, only when -KenneyDir is given.
#   Key<Name>.png  the keycap stretched horizontally (left corner / plain
#                  middle / right corner) with the key name in Bebas Neue
#                  (OFL 1.1, Assets/Fonts), dark letters. Kenney's own
#                  ESC / TAB / ENTER letters are about 30% of the key and turn
#                  into a grey smear at the size of the menu hints, so the
#                  names are drawn bigger here. Drawn at height 128 and shrunk
#                  to -Height (64): the keys end up about 20-30 px tall on
#                  screen, and from 128 px the game samples a blurry mip where
#                  the letters go grey. Margin = 1/8 of the height on every
#                  side (16 px at 128, 8 px at 64); InputGlyph cuts it off.
# Alpha is straight; transparent pixels are white (no colored fringe when the
# sampler blends the edge). Loaded as sRGB by InputGlyph.
#   -> VFX_L/Assets/Texture/UI/Input/
#
# Usage (Windows PowerShell 5.1):
#   .\BuildKeyIcons.ps1                       # rebuild Key*.png from KeyCap.png
#   .\BuildKeyIcons.ps1 -KenneyDir "<...>\Keyboard & Mouse\Double"   # also rebuild KeyCap.png
# Add a key: put it in $Keys (file name = Key<Name>.png, token = {K:<Name>}).
# ============================================================
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Texture\UI\Input'),
    [string]$FontFile = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Fonts\BebasNeue-Regular.ttf'),
    [string]$KenneyDir = '',
    [int]$Height = 64
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# token name -> label on the key
$Keys = [ordered]@{
    'A' = 'A'; 'D' = 'D'; 'E' = 'E'; 'F' = 'F'; 'I' = 'I'; 'Q' = 'Q'; 'R' = 'R'; 'S' = 'S'; 'T' = 'T'; 'W' = 'W'
    'Esc' = 'ESC'; 'Enter' = 'ENTER'; 'Space' = 'SPACE'; 'Tab' = 'TAB'; 'BackSpace' = 'BACKSPACE'
    'Shift' = 'SHIFT'; 'Ctrl' = 'CTRL'; 'Alt' = 'ALT'
}

$Edge = 28            # corner width inside the 96 px key (the rest of the key is plain white)
$PadX = 16            # space left / right of a word
$LetterEm = 78        # one letter (cap height about half the key, like Kenney's letters)
$WordEm = 72          # words (as big as the key allows: the menu hint lines are small)
$Ink = [System.Drawing.Color]::FromArgb(255, 24, 26, 32)
$Bold = 3            # extra stroke around the letters (px at 128). The game filters the shrunk key in linear
                     # light, where thin dark strokes on white fade to grey; thick strokes stay dark

$capPath = Join-Path $OutDir 'KeyCap.png'

# ---- blank keycap from Kenney's F (the letter is a transparent cut-out) ----
if ($KenneyDir -ne '') {
    $src = New-Object System.Drawing.Bitmap (Join-Path $KenneyDir 'keyboard_f.png')
    $cap = New-Object System.Drawing.Bitmap 128, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    for ($y = 0; $y -lt 128; $y++) {
        for ($x = 0; $x -lt 128; $x++) {
            $c = $src.GetPixel($x, $y)
            if ($x -ge 30 -and $x -le 97 -and $y -ge 30 -and $y -le 97) { $c = [System.Drawing.Color]::White }
            $cap.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c.A, 255, 255, 255))
        }
    }
    $src.Dispose()
    $cap.Save($capPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $cap.Dispose()
    "KeyCap.png"
}

$cap = New-Object System.Drawing.Bitmap $capPath
$fonts = New-Object System.Drawing.Text.PrivateFontCollection
$fonts.AddFontFile((Resolve-Path $FontFile).Path)
$family = $fonts.Families[0]
$fmt = [System.Drawing.StringFormat]::GenericTypographic

function LabelPath([string]$text, [float]$em) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $p.AddString($text, $family, [int][System.Drawing.FontStyle]::Regular, $em, (New-Object System.Drawing.PointF 0, 0), $fmt)
    return $p
}

foreach ($name in $Keys.Keys) {
    $label = $Keys[$name]
    $em = if ($label.Length -eq 1) { $LetterEm } else { $WordEm }
    $path = LabelPath $label $em
    $b = $path.GetBounds()

    $keyW = [Math]::Max(96, [int][Math]::Ceiling($b.Width + $Bold + $PadX * 2))
    if ($keyW % 2) { $keyW++ }   # even, so the shrink to 64 is exactly half
    $W = $keyW + 32
    $bmp = New-Object System.Drawing.Bitmap $W, 128, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(0, 255, 255, 255))
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half

    # keycap: left corner / middle (plain, stretched) / right corner, each copied 1:1 in height
    $mid = $keyW - $Edge * 2
    $g.DrawImage($cap, (New-Object System.Drawing.Rectangle 0, 0, (16 + $Edge), 128), 0, 0, (16 + $Edge), 128, [System.Drawing.GraphicsUnit]::Pixel)
    for ($x = 0; $x -lt $mid; $x++) {
        $g.DrawImage($cap, (New-Object System.Drawing.Rectangle (16 + $Edge + $x), 0, 1, 128), 64, 0, 1, 128, [System.Drawing.GraphicsUnit]::Pixel)
    }
    $g.DrawImage($cap, (New-Object System.Drawing.Rectangle (16 + $Edge + $mid), 0, (16 + $Edge), 128), (112 - $Edge), 0, (16 + $Edge), 128, [System.Drawing.GraphicsUnit]::Pixel)

    # name, centered on the key body (16..111 vertically)
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceOver
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $m = New-Object System.Drawing.Drawing2D.Matrix
    $m.Translate(($W - $b.Width) * 0.5 - $b.X, 64.0 - $b.Height * 0.5 - $b.Y)
    $path.Transform($m)
    $brush = New-Object System.Drawing.SolidBrush $Ink
    $g.FillPath($brush, $path)
    $pen = New-Object System.Drawing.Pen $Ink, $Bold
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawPath($pen, $path)
    $pen.Dispose()
    $brush.Dispose(); $path.Dispose(); $g.Dispose()

    # shrink to $Height (filtered in sRGB here, so the dark strokes keep their weight)
    $outW = [int]($W * $Height / 128)
    $out = New-Object System.Drawing.Bitmap $outW, $Height, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($out)
    $g.Clear([System.Drawing.Color]::FromArgb(0, 255, 255, 255))
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.DrawImage($bmp, (New-Object System.Drawing.Rectangle 0, 0, $outW, $Height), 0, 0, $W, 128, [System.Drawing.GraphicsUnit]::Pixel)
    $g.Dispose(); $bmp.Dispose()

    # transparent pixels -> white (filtering at the rounded edge stays white)
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $outW; $x++) {
            $c = $out.GetPixel($x, $y)
            if ($c.A -eq 0) { $out.SetPixel($x, $y, [System.Drawing.Color]::FromArgb(0, 255, 255, 255)) }
        }
    }

    $file = Join-Path $OutDir ("Key{0}.png" -f $name)
    $out.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $out.Dispose()
    "Key{0}.png  {1}x{2}  '{3}'" -f $name, $outW, $Height, $label
}
$cap.Dispose()
$fonts.Dispose()
