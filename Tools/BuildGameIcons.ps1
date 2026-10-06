# ============================================================
# BuildGameIcons.ps1
# Item icons from game-icons.net (CC BY 3.0): downloads the SVGs listed in
# $Icons from the game-icons GitHub repo and renders each one as a white
# silhouette on a transparent 136x136 PNG (straight alpha, like the old
# 7Soul icons) into VFX_L/Assets/Texture/UI/Icons/<Item>.png.
#
# Credits for every icon listed here must stay in
#   VFX_L/Assets/Texture/UI/Icons/README.txt   (author + CC BY 3.0)
#
# "@grid3" is not a game-icons file: a 3x3 grid drawn here (Frame3x3).
#
# Usage (Windows PowerShell 5.1):
#   .\BuildGameIcons.ps1                 # all icons
#   .\BuildGameIcons.ps1 -Only Magnifier # one icon
# ============================================================
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\VFX_L\Assets\Texture\UI\Icons'),
    [string]$Only = '',
    [int]$Size = 136,
    # fraction of the PNG the 512x512 icon box covers
    [double]$Fill = 0.86
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName PresentationCore, WindowsBase

# item file name  ->  game-icons path (author/name)
$Icons = [ordered]@{
    'Fireball'       = 'lorc/fireball'
    'ArcBolt'        = 'lorc/lightning-helix'
    'HomingBolt'     = 'lorc/on-target'
    'Meteor'         = 'lorc/meteor-impact'
    'GoldenArrow'    = 'lorc/high-shot'
    'StoneShot'      = 'lorc/stone-sphere'
    'Poison'         = 'lorc/poison-bottle'
    'Beam'           = 'lorc/laser-blast'
    'SplitRune'      = 'delapouite/split-arrows'
    'DoubleCastRune' = 'lorc/echo-ripples'
    'Magnifier'      = 'lorc/magnifying-glass'
    'HasteRune'      = 'lorc/stopwatch'
    'CrystalBall'    = 'lorc/crystal-ball'
    'MaxHealthUp'    = 'zeromancer/heart-plus'
    'MaxManaUp'      = 'lorc/potion-ball'
    'MoveSpeedUp'    = 'lorc/sprint'
    'JumpPowerUp'    = 'delapouite/jump-across'
    'ManaRegenUp'    = 'lorc/magic-swirl'
    'JumpCountUp'    = 'lorc/wingfoot'
    'SpellPowerUp'   = 'lorc/crystal-wand'
    'Magnet'         = 'lorc/magnet'
    'ManaSurge'      = 'lorc/embrassed-energy'
    'Gold'           = 'delapouite/two-coins'
    'Frame3x3'       = '@grid3'
}

$cache = Join-Path $env:TEMP 'vfxl_gameicons'
New-Item -ItemType Directory -Force $cache | Out-Null

function Get-IconPaths([string]$name) {
    if ($name -eq '@grid3') {
        # 3x3 squares with gaps, in the same 512 box as game-icons
        $list = @()
        for ($r = 0; $r -lt 3; $r++) { for ($c = 0; $c -lt 3; $c++) {
            $x = 40 + $c * 148; $y = 40 + $r * 148
            $list += "M$x ${y}h136v136h-136z"
        } }
        return $list
    }
    $file = Join-Path $cache (($name -replace '/', '_') + '.svg')
    if (-not (Test-Path $file)) {
        Invoke-WebRequest -UseBasicParsing -Uri "https://raw.githubusercontent.com/game-icons/icons/master/$name.svg" -OutFile $file
    }
    $svg = [IO.File]::ReadAllText($file)
    # the first path is the black 512x512 background square
    return [regex]::Matches($svg, '<path[^>]*\sd="([^"]+)"') |
        ForEach-Object { $_.Groups[1].Value } |
        Where-Object { $_ -ne 'M0 0h512v512H0z' }
}

foreach ($item in $Icons.Keys) {
    if ($Only -and $item -ne $Only) { continue }
    $paths = Get-IconPaths $Icons[$item]

    $dv = New-Object System.Windows.Media.DrawingVisual
    $dc = $dv.RenderOpen()
    $s = $Size * $Fill / 512.0
    $o = $Size * (1.0 - $Fill) / 2.0
    $dc.PushTransform((New-Object System.Windows.Media.MatrixTransform ([System.Windows.Media.Matrix]::new($s, 0, 0, $s, $o, $o))))
    foreach ($d in $paths) {
        # F1 = nonzero fill rule (SVG default)
        $dc.DrawGeometry([System.Windows.Media.Brushes]::White, $null, [System.Windows.Media.Geometry]::Parse("F1 " + $d))
    }
    $dc.Pop()
    $dc.Close()

    $rtb = New-Object System.Windows.Media.Imaging.RenderTargetBitmap($Size, $Size, 96, 96, [System.Windows.Media.PixelFormats]::Pbgra32)
    $rtb.Render($dv)
    $px = New-Object byte[] ($Size * $Size * 4)
    $rtb.CopyPixels($px, $Size * 4, 0)
    # straight alpha: white wherever there is coverage (the sprite PS premultiplies)
    for ($i = 0; $i -lt $px.Length; $i += 4) {
        if ($px[$i + 3] -gt 0) { $px[$i] = 255; $px[$i + 1] = 255; $px[$i + 2] = 255 }
    }
    $wb = New-Object System.Windows.Media.Imaging.WriteableBitmap($Size, $Size, 96, 96, [System.Windows.Media.PixelFormats]::Bgra32, $null)
    $wb.WritePixels((New-Object System.Windows.Int32Rect 0, 0, $Size, $Size), $px, $Size * 4, 0)

    $out = Join-Path $OutDir "$item.png"
    $enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
    $enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($wb))
    $fs = [IO.File]::Create($out); $enc.Save($fs); $fs.Close()
    Write-Host ("{0,-15} <- {1}" -f $item, $Icons[$item])
}
