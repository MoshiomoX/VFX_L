# AddMeteorRock.ps1 - puts a rock body on the falling meteor (VFXData/Meteor.json)
# Adds (or replaces) three entries (tag "meteorRock") in front of the existing fire / smoke entries:
#   rock  : Rock_2.fbx "single" mesh, lit, dark basalt, faces the fall direction
#   shell : the same rock a bit larger, glowing lava orange, back faces only (= a burning rim behind the body)
#   trail : toon fire balls (sheet 3, red rim / yellow core) left behind the rock
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File Tools/AddMeteorRock.ps1 [-Size 1.3]
param([double]$Size = 1.3)
$path = Join-Path (Split-Path $PSScriptRoot -Parent) "VFX_L\Assets\Data\VFXData\Meteor.json"
$doc = Get-Content -Raw -Encoding UTF8 $path | ConvertFrom-Json

function Body([double]$sz, [double[]]$col, [bool]$glow, [bool]$behind) {
    $d = [ordered]@{
        sheet = 0; atlasIndex = 0; frameMode = 1; frameCount = 1; atlasAnimate = $false; atlasCols = 6; atlasRows = 6; blend = 0
        emitType = 0; position = @(0, 0, 0); direction = @(0, 1, 0); radius = 0; innerRadius = 0; spreadAngle = 0; boxExtents = @(1, 1, 1)
        emitRate = 50; maxParticles = 2000; lifetimeRange = @(0.04, 0.04); speedRange = @(0, 0)
        sizeRange = @($sz, $sz, $sz, $sz); rotationRange = @(0, 0); angularVelRange = @(0, 0)
        startColorMin = $col; startColorMax = $col; endColorMin = $col; endColorMax = $col
        gravity = @(0, 0, 0); dragCoeff = 0; renderMode = 1; mesh = "Assets/VFX/Mesh/Rock_2.fbx"
        meshGlow = $glow; meshFaceVelocity = $true; meshForwardAxis = 3; inheritVelocity = $true; single = $true
    }
    if ($behind) { $d.meshBehind = $true }
    [ordered]@{ data = $d; duration = 0.25; startTime = 0; type = "Particle"; tag = "meteorRock" }
}

# toon fire trail: small red-rim / yellow-core balls (same look as the blast), shrinking behind the rock
$trail = [ordered]@{
    data = [ordered]@{
        sheet = 3; atlasCols = 1; atlasRows = 1; atlasIndex = 0; frameMode = 1; frameCount = 1; atlasAnimate = $false; blend = 1
        emitType = 1; position = @(0, 0, 0); direction = @(0, 1, 0); radius = [math]::Round(0.3 * $Size, 3); innerRadius = 0; spreadAngle = 0; boxExtents = @(1, 1, 1)
        emitRate = 110; maxParticles = 2000; lifetimeRange = @(0.22, 0.32); speedRange = @(0.2, 0.8)
        sizeRange = @([math]::Round(0.55 * $Size, 3), [math]::Round(0.75 * $Size, 3), [math]::Round(0.1 * $Size, 3), [math]::Round(0.18 * $Size, 3))
        rotationRange = @(0, 0); angularVelRange = @(0, 0)
        startColorMin = @(1.85, 1.2, 0.08, 1); startColorMax = @(1.9, 1.3, 0.1, 1)
        endColorMin = @(0.9, 0.12, 0.03, 0); endColorMax = @(1.0, 0.14, 0.03, 0)
        colorKeys = @(
            [ordered]@{ time = 0; color = @(1.9, 1.25, 0.1, 1) },
            [ordered]@{ time = 0.4; color = @(1.6, 0.6, 0.05, 0.9) },
            [ordered]@{ time = 1; color = @(0.9, 0.12, 0.03, 0) })
        gravity = @(0, 0, 0); dragCoeff = 2; renderMode = 0
        toon = [ordered]@{ cut = 0.3; bands = 3; shade = 0.55; outline = $false; noise = $true; fire = $true }
    }
    duration = 0.25; startTime = 0; type = "Particle"; tag = "meteorRock"
}

$rest = @($doc.entries | Where-Object { $_.tag -ne "meteorRock" })
# the old additive fire sprite washed the rock out: smaller and dimmer (only once, it is not tagged)
foreach ($e in $rest) {
    if ($e.type -eq "Particle" -and $e.data.sheet -eq 1 -and $e.data.atlasIndex -eq 8 -and $e.data.sizeRange[0] -gt 1.0) {
        $e.data.sizeRange = @(0.9, 1.1, 0.3, 0.45)
        $e.data.startColorMin = @(0.7, 0.22, 0.05, 1); $e.data.startColorMax = @(0.7, 0.35, 0.1, 1)
    }
}
$rock = Body $Size @(0.05, 0.035, 0.03, 1) $false $false
$shell = Body ([math]::Round($Size * 1.15, 3)) @(1.8, 0.32, 0.04, 1) $true $true
$doc.entries = @($rock, $shell, $trail) + $rest
$json = $doc | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($path, $json, (New-Object Text.UTF8Encoding($false)))
"wrote $path ($($doc.entries.Count) entries)"
