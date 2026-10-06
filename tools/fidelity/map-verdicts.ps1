# Per-map verdict table from existing evidence (map-matrix, m06-maps-tdm, per-map stress), judged with the presentation
# measures: STRUCTURAL / COLLISION / MATERIAL-SHADER / VISUAL COVERAGE / LIGHTING / HUMAN CHECK NEEDED.
#
#   .\tools\fidelity\map-verdicts.ps1 -Matrix <map_matrix dir> -Tdm <tdm_maps dir> [-Stress <stress dir>] -Out <MAP-VERDICTS.md>
#
# VISUALLY PLAYABLE requires BOTH the direct-boot views (map-matrix) and the frontend-launched match frames (tdm) to show
# a textured world with the HUD and the player excluded. Spawn + collision + loading are not enough.
param([Parameter(Mandatory)][string]$Matrix, [string]$Tdm = "", [string]$Stress = "", [Parameter(Mandatory)][string]$Out)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1")
$mx = @{}; if (Test-Path (Join-Path $Matrix "report.json")) { foreach ($r in (Get-Content -Raw (Join-Path $Matrix "report.json") | ConvertFrom-Json).results) { $mx[$r.id] = $r } }
$tdmRes = @{}; if ($Tdm -and (Test-Path (Join-Path $Tdm "report.json"))) { foreach ($r in (Get-Content -Raw (Join-Path $Tdm "report.json") | ConvertFrom-Json).results) { $tdmRes[$r.id] = $r } }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($md in @(Get-ChildItem $Matrix -Directory | Where-Object { $_.Name -like "MP_*" } | Sort-Object Name)) {
    $map = $md.Name
    $views = @(Get-ChildItem (Join-Path $md.FullName "shots") -Filter *.jpg -ErrorAction SilentlyContinue | Where-Object { $_.BaseName -notlike "warm*" })
    $vm = @($views | ForEach-Object { $m = [WfcPresent]::Measure($_.FullName, 0, 0, 1, 1); [pscustomobject]@{ detail = $m[1]; black = $m[0]; maxFlat = $m[3]; mean = $m[6] } })
    $darkEmpty = @($vm | Where-Object { $_.black -gt 0.6 -or ($_.detail -lt 0.10 -and $_.mean -lt 15) }).Count; $litEmpty = @($vm | Where-Object { $_.detail -lt 0.10 -and $_.mean -ge 15 -and $_.black -le 0.6 }).Count; $empty = $darkEmpty + $litEmpty; $dark = @($vm | Where-Object { $_.black -gt 0.6 }).Count
    $medD = if ($vm.Count) { [Math]::Round((($vm | ForEach-Object { $_.detail } | Sort-Object)[[int]($vm.Count / 2)]), 3) } else { $null }
    $medL = if ($vm.Count) { [Math]::Round((($vm | ForEach-Object { $_.mean } | Sort-Object)[[int]($vm.Count / 2)]), 1) } else { $null }
    $direct = if (-not $vm.Count) { "SKIP" } elseif ($darkEmpty -gt $vm.Count * 0.25) { "FAIL" } elseif ($litEmpty -gt $vm.Count * 0.4 -or $medD -lt 0.15) { "PARTIAL" } else { "PASS" }
    $route = "n/a"; $routeNote = "no frontend route (mode not offered)"
    if ($Tdm -and (Test-Path (Join-Path $Tdm $map))) {
        $ws = @("d_ingame", "e_ingame_later" | ForEach-Object { Present-World (Join-Path (Join-Path $Tdm $map) "$_.bmp") } | Where-Object { $_ })
        $route = Present-WorldSetVerdict $ws
        $routeNote = "frontend-launched match world detail " + (($ws | ForEach-Object { $_.detail }) -join " / ")
    }
    $cov = if ($direct -eq "FAIL" -or $route -eq "FAIL") { "FAIL" } elseif ($direct -eq "PARTIAL" -or $route -eq "PARTIAL") { "PARTIAL" } else { "PASS" }
    $light = if (-not $vm.Count) { "SKIP" } elseif ($dark -gt $vm.Count * 0.25 -or $medL -lt 16) { "FAIL" } else { "PASS" }
    $ch = $mx["maps.$map.chaos"]; $xf = $mx["maps.$map.xform"]; $mat = $mx["maps.$map.materials"]
    $struct = if (-not $ch) { "SKIP" } elseif ($ch.status -eq "FAIL" -or ($xf -and $xf.status -eq "FAIL")) { "FAIL" } elseif ($ch.status -eq "PARTIAL") { "PARTIAL" } else { "PASS" }
    $coll = "not run"; $collNote = ""
    if ($Stress -and (Test-Path (Join-Path $Stress "$map\vehicle\vehicle_collision.csv"))) {
        $v = Import-Csv (Join-Path $Stress "$map\vehicle\vehicle_collision.csv"); $tr = Import-Csv (Join-Path $Stress "$map\transform\stress.csv")
        $pt = @($v | Where-Object { $_.verdict -like "*passed_through*" }).Count; $hs = @($v | Where-Object { $_.verdict -like "*ramp_hard_stop*" }).Count; $ft = @($tr | Where-Object { $_.verdict -like "*fell_through*" }).Count
        $coll = if ($ft) { "FAIL" } elseif ($pt -or $hs -gt 10) { "PARTIAL" } else { "PASS" }; $collNote = "fall-through $ft / $($tr.Count), pass-through $pt / $($v.Count), hard stops $hs"
    }
    $matV = if ($mat) { $mat.status } else { "SKIP" }
    $tdmLife = $tdmRes["tdm.$map.lifecycle"]
    $cat = if ($struct -eq "SKIP") { "NOT RUN" } elseif ($cov -eq "FAIL" -or $light -eq "FAIL") { "NOT VISUALLY PLAYABLE" } elseif ($cov -eq "PASS" -and $light -eq "PASS" -and $matV -eq "PASS") { "VISUALLY PLAYABLE (pending human look)" } else { "VISUALLY PARTIAL" }
    if ($tdmLife -and $tdmLife.status -eq "PASS" -and $cat -like "VISUALLY PLAYABLE*") { $cat = "TDM PLAYABLE (pending human look)" }
    $rows.Add([pscustomobject][ordered]@{ map = $map; STRUCTURAL = $struct; COLLISION = $coll; MATERIAL_SHADER = $matV; VISUAL_COVERAGE = $cov; LIGHTING = $light; HUMAN_CHECK_NEEDED = "yes"; category = $cat
        direct_empty_views = "dark $darkEmpty + lit (fog / sky) $litEmpty of $($vm.Count)"; direct_median_detail = $medD; route = $routeNote; collision_note = $collNote; median_luma = $medL })
}
Write-WfcCsv $rows ([IO.Path]::ChangeExtension($Out, ".csv"))
$md = @("| map | STRUCTURAL | COLLISION | MATERIAL / SHADER | VISUAL COVERAGE | LIGHTING | HUMAN CHECK NEEDED | category | evidence |", "|---|---|---|---|---|---|---|---|---|")
foreach ($r in $rows) { $md += "| $($r.map) | $($r.STRUCTURAL) | $($r.COLLISION) | $($r.MATERIAL_SHADER) | **$($r.VISUAL_COVERAGE)** | $($r.LIGHTING) | yes | **$($r.category)** | direct: empty views $($r.direct_empty_views), median detail $($r.direct_median_detail), luma $($r.median_luma); $($r.route); $($r.collision_note) |" }
$md | Set-Content -Encoding UTF8 $Out
$md
