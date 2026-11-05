# EnemyStatsToDashboard.ps1 - enemystats.json (VFXL_BATTLE_AUTOTEST=enemystats) -> the 4 dataset files of the "enemy stats" dashboard
# Usage: powershell -File Tools/EnemyStatsToDashboard.ps1 -In <enemystats.json> -Out <dir>
param([string]$In, [string]$Out)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null
$j = Get-Content $In -Raw -Encoding UTF8 | ConvertFrom-Json

function RoundTo([object]$v, [int]$d) { if ($null -eq $v) { return $null }; return [math]::Round([double]$v, $d) }
function Label([int]$t) { return ("{0}:{1:00}" -f [math]::Floor($t / 60), ($t % 60)) }

$kindOrder = @($j.kinds | ForEach-Object { $_.key })
$kindInfo = @{}
foreach ($k in $j.kinds) { $kindInfo[$k.key] = $k }
function ParentName([string]$key) {
    $p = $kindInfo[$key].parent
    if ([string]::IsNullOrEmpty($p)) { return "" }
    if ($kindInfo.ContainsKey($p)) { return $kindInfo[$p].name }
    return $p
}

# ---- enemy_base ----
$base = foreach ($k in $j.kinds) {
    $sp = @($k.specials)
    [ordered]@{
        key = $k.key; name = $k.name; hp = [double]$k.hp; speed = (RoundTo $k.speed 2)
        contact = $(if ([double]$k.contact -lt 0) { $null } else { [double]$k.contact })
        skillName = $(if ($sp.Count -gt 0) { $sp[0].name } else { "" })
        skill = $(if ($sp.Count -gt 0) { [double]$sp[0].damage } else { $null })
        exp = [double]$k.exp; note = $k.note; source = (ParentName $k.key)
    }
}

$pivot = New-Object System.Collections.Generic.List[object]
$rates = New-Object System.Collections.Generic.List[object]
$elites = New-Object System.Collections.Generic.List[object]
$metrics = @("share", "hp", "contact", "skill", "speed", "exp")

foreach ($m in $j.maps) {
    foreach ($p in $m.positions) {
        $rows = @($p.rows)
        $prefix = "$($m.name)|$($p.pos)|$($p.tier)"
        # ---- stage_rates ----
        foreach ($r in $rows) {
            $g = $r.kinds.ghost; $dg = $r.kinds.darkGhost
            $rates.Add([pscustomobject][ordered]@{
                key = "$prefix|$($r.t)"; map = $m.name; pos = [int]$p.pos; tier = [int]$p.tier; t = [int]$r.t; time = $r.label
                boss = $p.boss; hpMul = [double]$r.hpMul; damageMul = [double]$r.damageMul
                spawnNormal = (RoundTo $r.spawnNormal 2); spawnSurge = (RoundTo $r.spawnSurge 2)
                ghostRate = $(if ($g -and $null -ne $g.rate) { RoundTo $g.rate 1 } else { $null })
                darkGhostRate = $(if ($dg -and $null -ne $dg.rate) { RoundTo $dg.rate 1 } else { $null })
                bossHp = [int][math]::Round([double]$r.bossHp); bossContact = [double]$r.bossContact
            })
        }
        # ---- elites ----
        $i = 0
        foreach ($e in @($p.elites)) {
            $elites.Add([pscustomobject][ordered]@{
                key = "$prefix|$i"; map = $m.name; pos = [int]$p.pos; tier = [int]$p.tier; time = (Label ([int]$e.t))
                name = $e.name; affixes = [int]$e.affixes; hp = [int][math]::Round([double]$e.hp)
                contact = [double]$e.contact; skill = [double]$e.skill; speed = [double]$e.speed
            })
            ++$i
        }
        # ---- enemy_pivot: one row per kind x metric, c0..c13 = the 14 time points ----
        foreach ($metric in $metrics) {
            foreach ($key in $kindOrder) {
                $present = $false
                foreach ($r in $rows) {
                    $kv = $r.kinds.$key
                    if (-not $kv) { continue }
                    switch ($metric) {
                        "share"   { if ($null -ne $kv.share -and [double]$kv.share -gt 0) { $present = $true } }
                        "contact" { if ([double]$kv.contact -ge 0) { $present = $true } }
                        "skill"   { if (@($kv.specials).Count -gt 0) { $present = $true } }
                        default   { $present = $true }
                    }
                }
                if (-not $present) { continue }
                $o = [ordered]@{
                    key = "$prefix|$metric|$key"; map = $m.name; pos = [int]$p.pos; tier = [int]$p.tier
                    metric = $metric; kind = $key; name = $kindInfo[$key].name; parent = (ParentName $key)
                }
                for ($c = 0; $c -lt $rows.Count; ++$c) {
                    $kv = $rows[$c].kinds.$key
                    $v = $null
                    if ($kv) {
                        switch ($metric) {
                            "share"   { if ($null -ne $kv.share) { $v = RoundTo ([double]$kv.share * 100.0) 1 } }
                            "hp"      { $v = [double]$kv.hp }
                            "contact" { if ([double]$kv.contact -ge 0) { $v = [double]$kv.contact } }
                            "skill"   { $sp = @($kv.specials); if ($sp.Count -gt 0) { $v = [double]$sp[0] } }
                            "speed"   { $v = RoundTo $kv.speed 2 }
                            "exp"     { $v = [double]$kv.exp }
                        }
                    }
                    $o["c$c"] = $v
                }
                $pivot.Add([pscustomobject]$o)
            }
        }
    }
}

$enc = New-Object System.Text.UTF8Encoding($false)
function Save($name, $list) {
    $json = ConvertTo-Json -InputObject $(if ($list -is [System.Collections.Generic.List[object]]) { $list.ToArray() } else { @($list) }) -Depth 4 -Compress
    [System.IO.File]::WriteAllText((Join-Path $Out $name), $json, $enc)
    "{0}: {1} rows, {2} bytes" -f $name, $list.Count, (Get-Item (Join-Path $Out $name)).Length
}
Save "enemy_base.json" $base
Save "elites.json" $elites
Save "stage_rates.json" $rates
Save "enemy_pivot.json" $pivot
