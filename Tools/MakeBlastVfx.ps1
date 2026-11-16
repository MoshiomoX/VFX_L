# MakeBlastVfx.ps1 - writes an anime-style explosion VFX json (few big toon puffs)
# Explosion.json:   -Scale 1.0 -Count 1.0 -KeepFrom <old Explosion.json> -KeepIdx @(5)   (5 = point light)
# MeteorBlast.json: -Scale 1.35 -Count 1.4 -KeepFrom <old MeteorBlast.json> -KeepIdx @(5,6) (light + rock meshes)
# The kept entries are appended as they are; run from PowerShell with & (not -File) so -KeepIdx stays an array.
# Usage: MakeBlast.ps1 -Out <json> -Name <name> -Scale <size mul> -Count <puff count mul> [-Keep <json with entries to append>]
param([string]$Out, [string]$Name, [double]$Scale = 1.0, [double]$Count = 1.0, [string]$KeepFrom = "", [int[]]$KeepIdx = @())

function P([hashtable]$h) {
    $d = [ordered]@{
        angularVelRange = @(0, 0); atlasAnimate = $false; atlasCols = 9; atlasRows = 9; atlasIndex = 0; sheet = 2
        frameMode = 3; frameCount = 1; blend = 1; boxExtents = @(1, 1, 1); direction = @(0, 1, 0); dragCoeff = 0
        emitRate = 10; emitType = 1; endColorMax = @(1, 1, 1, 0); endColorMin = @(1, 1, 1, 0); gravity = @(0, 0, 0)
        innerRadius = 0; lifetimeRange = @(0.3, 0.4); maxParticles = 2000; position = @(0, 0, 0); radius = 0.3
        renderMode = 0; rotationRange = @(0, 6.28); sizeRange = @(1, 1, 1, 1); speedRange = @(0, 0); spreadAngle = 0
        startColorMax = @(1, 1, 1, 1); startColorMin = @(1, 1, 1, 1)
    }
    foreach ($k in $h.Keys) { if ($k -ne 'duration' -and $k -ne 'startTime') { $d[$k] = $h[$k] } }
    [ordered]@{ data = $d; duration = ($(if ($h.duration) { $h.duration } else { 0.25 })); startTime = ($(if ($h.startTime) { $h.startTime } else { 0 })); type = "Particle" }
}
$s = $Scale; $c = $Count
function Sz($a, $b, $e, $f) { @([math]::Round($a * $s, 3), [math]::Round($b * $s, 3), [math]::Round($e * $s, 3), [math]::Round($f * $s, 3)) }

$entries = New-Object System.Collections.ArrayList

# 1 flash: one big additive pop
[void]$entries.Add((P @{ sheet = 1; atlasCols = 10; atlasRows = 8; atlasIndex = 1; frameMode = 1; frameCount = 1; blend = 0
    emitRate = 4; emitType = 0; radius = 0; lifetimeRange = @(0.11, 0.13); rotationRange = @(0, 0)
    sizeRange = (Sz 1.0 1.0 5.0 5.0); startColorMin = @(1, 0.55, 0.25, 0.85); startColorMax = @(1, 0.55, 0.25, 0.85)
    endColorMin = @(1, 0.18, 0.05, 0); endColorMax = @(1, 0.18, 0.05, 0) }))

# 2 fireball: ~20 toon ball particles (sheet 3, one round ball each); the bulgy dome comes from their placement.
#   Red rim / yellow core per ball, burst out then eaten away by noise erosion with a yellow edge (no rising)
[void]$entries.Add((P @{ sheet = 3; atlasCols = 1; atlasRows = 1; atlasIndex = 0; frameMode = 1; frameCount = 1
    emitRate = [math]::Round(80 * $c); radius = 0.9 * $s; speedRange = @((2.4 * $s), (5.0 * $s)); dragCoeff = 6
    gravity = @(0, 0.4, 0); lifetimeRange = @(0.45, 0.65); sizeRange = (Sz 1.0 1.6 1.9 2.6)
    startColorMin = @(1.8, 1.15, 0.06, 1); startColorMax = @(1.9, 1.25, 0.1, 1)
    endColorMin = @(0.55, 0.05, 0.02, 0); endColorMax = @(0.6, 0.06, 0.02, 0)
    colorKeys = @(
        [ordered]@{ time = 0; color = @(1.85, 1.2, 0.08, 1) },
        [ordered]@{ time = 0.3; color = @(1.7, 0.8, 0.04, 0.95) },
        [ordered]@{ time = 0.65; color = @(1.3, 0.35, 0.05, 0.75) },
        [ordered]@{ time = 1; color = @(0.6, 0.06, 0.02, 0) })
    toon = [ordered]@{ cut = 0.3; bands = 3; shade = 0.55; outline = $false; noise = $true; fire = $true } }))

# 3 hot core: a few yellow balls inside the fireball
[void]$entries.Add((P @{ sheet = 3; atlasCols = 1; atlasRows = 1; atlasIndex = 0; frameMode = 1; frameCount = 1
    emitRate = [math]::Round(20 * $c); radius = 0.35 * $s; speedRange = @((0.6 * $s), (1.6 * $s)); dragCoeff = 4
    lifetimeRange = @(0.18, 0.26); sizeRange = (Sz 0.9 1.2 1.3 1.6)
    startColorMin = @(1.9, 1.35, 0.12, 1); startColorMax = @(2.0, 1.45, 0.16, 1)
    endColorMin = @(1.7, 0.8, 0.05, 0); endColorMax = @(1.8, 0.9, 0.06, 0)
    toon = [ordered]@{ cut = 0.3; bands = 2; shade = 0.7; outline = $false } }))

# 4 dark smoke: starts small above the core, grows, stays in place and is eaten away by noise erosion (gone within 0.9 s)
[void]$entries.Add((P @{ sheet = 3; atlasCols = 1; atlasRows = 1; atlasIndex = 0; frameMode = 1; frameCount = 1; position = @(0, (1.1 * $s), 0)
    emitRate = [math]::Round(24 * $c); radius = 0.7 * $s; speedRange = @((0.8 * $s), (1.6 * $s)); dragCoeff = 3
    gravity = @(0, 0.3, 0); lifetimeRange = @(0.7, 0.9); sizeRange = (Sz 0.15 0.25 1.6 2.0); wind = 0.3
    startColorMin = @(0.06, 0.03, 0.025, 1); startColorMax = @(0.08, 0.04, 0.03, 1)
    endColorMin = @(0.1, 0.06, 0.05, 0); endColorMax = @(0.12, 0.07, 0.06, 0)
    colorKeys = @(
        [ordered]@{ time = 0; color = @(0.07, 0.035, 0.025, 1) },
        [ordered]@{ time = 0.5; color = @(0.08, 0.045, 0.035, 0.9) },
        [ordered]@{ time = 1; color = @(0.11, 0.065, 0.055, 0) })
    toon = [ordered]@{ cut = 0.3; bands = 2; shade = 0.65; outline = $true; noise = $true } }))

# 5 sparks (fewer than before)
[void]$entries.Add((P @{ sheet = 0; atlasCols = 6; atlasRows = 6; atlasIndex = 0; frameMode = 1; frameCount = 1; blend = 0
    angularVelRange = @(-360, 360); emitRate = [math]::Round(120 * $c); radius = 0.3 * $s; renderMode = 1; dragCoeff = 1.2
    gravity = @(0, -9, 0); lifetimeRange = @(0.45, 0.8); rotationRange = @(0, 360); sizeRange = @(0.07, 0.1, 0.02, 0.03)
    speedRange = @((5 * $s), (10 * $s))
    startColorMin = @(1, 0.45, 0.1, 1); startColorMax = @(1, 0.6, 0.2, 1)
    endColorMin = @(1, 0.15, 0.03, 0); endColorMax = @(1, 0.15, 0.03, 0)
    meshGlow = $true; mesh = ""; meshFaceVelocity = $false; meshForwardAxis = 2 }))

if ($KeepFrom -ne "") {
    $old = Get-Content -Raw -Encoding UTF8 $KeepFrom | ConvertFrom-Json
    foreach ($i in $KeepIdx) { [void]$entries.Add($old.entries[$i]) }
}

$doc = [ordered]@{ entries = $entries; loop = $false; name = $Name }
$json = $doc | ConvertTo-Json -Depth 12
[IO.File]::WriteAllText($Out, $json, (New-Object Text.UTF8Encoding($false)))
"wrote $Out ($($entries.Count) entries)"
