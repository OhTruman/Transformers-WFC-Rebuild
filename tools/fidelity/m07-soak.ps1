# MILESTONE 07 LONG SOAK over several maps (not Streets only): one process, frontend -> map -> frontend cycles through
# every versus map, several passes. Distinguishes a one-time plateau (first pass loads and caches each map once) from
# unbounded growth (still rising on later passes, or a map costing more on each revisit).
#
#   .\tools\fidelity\m07-soak.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Passes 3] [-Config Release|Debug] [-ReportOnly]
#
# Runs the lifecycle gate's R4 with -ChainMaps (all versus maps, from the M07 expectations) and WFC_VISUALCHECK, then
# adds per-cycle series parsed from the run:
#   working set / private MB / handles / threads (R4 samples), GL objects released per return (map render data:
#   meshes / programs / textures), VISUALCHECK in-play world draws per map visit, audio after every unload (voices /
#   instances / level cues / PCM), map audio loaded per visit, GFx movies per cycle, frontend nav.check resources
#   (AS heap, display nodes, GL shape cache) when the build has navcheck:.
# Classification per series: PLATEAU (pass 2+ slope within tolerance), GROWTH (pass 2+ slope above it), or per-map
# REVISIT GROWTH (the same map costs more on each visit).
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Passes = 3, [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$X = Get-M07Expectations
$ids = @($X.maps | Where-Object { $_.versus_launchable } | Sort-Object { [int]$_.mapId } | ForEach-Object { "$($_.mapId)" })
$cycles = $ids.Count * $Passes
if (-not $ReportOnly) { if (Wait-WfcGpu) { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $Root -Config $Config -Runs "R4" -Cycles $cycles -ChainMaps ($ids -join ",") -ExtraEnv "WFC_VISUALCHECK=1" -OutDir $OutDir | Select-Object -Last 2 } }
$d = Join-Path $OutDir "R4_cycles"; $log = Join-Path $d "wfc.log"
$res = New-WfcResults
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "m07soak.$id" $status $null $note $owner }
if (-not (Test-Path $log)) { Res "run" "SKIP" "no R4 run (GPU busy or report-only without data)" "Experimental"; $sum = Write-WfcReport $res (Join-Path $OutDir "report_m07.json"); "M07 SOAK: no data"; return }
$cyc = @(Import-Csv (Join-Path $d "cycles.csv"))
# per-cycle extra series from the log, in visit order
$rel = @(Grep-Log $log 'released map render data \((\d+) meshes, (\d+) programs, (\d+) textures\)' | ForEach-Object { $m = [regex]::Match($_.text, '\((\d+) meshes, (\d+) programs, (\d+) textures\)'); [pscustomobject]@{ meshes = [int]$m.Groups[1].Value; programs = [int]$m.Groups[2].Value; textures = [int]$m.Groups[3].Value } })
$vc = @(Read-VisualCheckByVisit $log | Where-Object { $_.level -like "Match:*" -and $_.ui -eq "InGame" })
$nav = @(Grep-Log $log 'FLOW nav\.check ' | ForEach-Object { $kv = @{}; foreach ($t in ($_.text -replace '^.*FLOW nav\.check ', '') -split ' ') { $p = $t -split '=', 2; if ($p.Count -eq 2) { $kv[$p[0]] = $p[1] } }; [pscustomobject]$kv })
$rows = New-Object System.Collections.Generic.List[object]
for ($c = 0; $c -lt $cyc.Count; $c++) {
    $mid = $ids[$c % $ids.Count]; $map = @($X.maps | Where-Object { "$($_.mapId)" -eq $mid })[0].runtime
    $mv = @($vc | Where-Object { $_.level -like "*$map*" }); $visitN = [int][Math]::Floor($c / $ids.Count)
    $rows.Add([pscustomobject][ordered]@{ cycle = $c + 1; pass = $visitN + 1; map = $map; frontend_private_mb = $cyc[$c].frontend_private_mb; frontend_ws_mb = $cyc[$c].frontend_ws_mb; unloaded_private_mb = $cyc[$c].unloaded_private_mb
        handles = $cyc[$c].handles; threads = $cyc[$c].threads; gl_textures_released = $(if ($c -lt $rel.Count) { $rel[$c].textures }); gl_meshes_released = $(if ($c -lt $rel.Count) { $rel[$c].meshes })
        world_draws = $(if ($mv.Count) { Median ($mv | ForEach-Object { $_.world }) }); nav_heap = $(if ($c -lt $nav.Count) { $nav[$c].asHeap }); nav_nodes = $(if ($c -lt $nav.Count) { $nav[$c].displayNodes }); nav_shapes = $(if ($c -lt $nav.Count) { $nav[$c].glShapes }); nav_graveyard = $(if ($c -lt $nav.Count) { $nav[$c].graveyard }) })
}
Write-WfcCsv $rows (Join-Path $OutDir "soak_series.csv")
function Slope($v) { $v = @($v | Where-Object { $_ -ne $null -and "$_" -ne "" } | ForEach-Object { [double]$_ }); if ($v.Count -lt 3) { return $null }; $n = $v.Count; $mx = ($n - 1) / 2.0; $my = ($v | Measure-Object -Average).Average; $num = 0.0; $den = 0.0; for ($i = 0; $i -lt $n; $i++) { $num += ($i - $mx) * ($v[$i] - $my); $den += ($i - $mx) * ($i - $mx) }; return [Math]::Round($num / [Math]::Max(1e-9, $den), 2) }
$later = @($rows | Where-Object { $_.pass -ge 2 })
foreach ($s in @(@("frontend_private_mb", 10, "MB per cycle", "Integration"), @("frontend_ws_mb", 10, "MB per cycle", "Integration"), @("handles", 2, "handles per cycle", "Frontend/Rendering"), @("threads", 0.3, "threads per cycle", "Integration"), @("nav_heap", 0, "AS heap per cycle", "Frontend"), @("nav_nodes", 0, "display nodes per cycle", "Frontend"), @("nav_shapes", 0, "GL shape cache per cycle", "Frontend"), @("nav_graveyard", 0, "AS graveyard per cycle", "Frontend"))) {
    $name, $tol, $unit, $owner = $s; $all = @($rows | ForEach-Object { $_.$name }); if (-not @($all | Where-Object { $_ -ne $null -and "$_" -ne "" }).Count) { Res "$name" "SKIP" "no data ($name)" "Experimental"; continue }
    $sl = Slope ($later | ForEach-Object { $_.$name }); $first = $all[0]; $passOneEnd = $all[[Math]::Min($ids.Count - 1, $all.Count - 1)]; $lastV = $all[-1]
    $cls = if ($sl -eq $null) { "SKIP" } elseif ([double]$sl -gt [double]$tol -and $tol -gt 0) { "GROWTH" } elseif ($tol -eq 0 -and [double]$sl -gt 0.5) { "GROWTH" } else { "PLATEAU" }
    Res "$name" $(if ($cls -eq "GROWTH") { "FAIL" } elseif ($cls -eq "PLATEAU") { "PASS" } else { "SKIP" }) ("{0}: first cycle {1}, end of pass 1 {2}, last {3}; slope over passes 2-{4}: {5} {6} -> {7} (one-time plateau vs unbounded growth)" -f $name, $first, $passOneEnd, $lastV, $Passes, $sl, $unit, $cls) $owner
}
# per map: the same map on successive visits must not cost more (memory after unload, textures released, world draws stable)
foreach ($map in @($rows | ForEach-Object { $_.map } | Select-Object -Unique)) {
    $v = @($rows | Where-Object { $_.map -eq $map }); if ($v.Count -lt 2) { continue }
    $mem = @($v | ForEach-Object { [double]$_.unloaded_private_mb }); $tex = @($v | ForEach-Object { $_.gl_textures_released } | Select-Object -Unique); $wd = @($v | ForEach-Object { $_.world_draws } | Where-Object { $_ })
    $memGrow = if ($mem.Count -ge 3) { [Math]::Round(($mem[-1] - $mem[1]) / ($mem.Count - 2), 1) } else { $null }
    $wdRatio = if ($wd.Count -ge 2) { [Math]::Round([double]$wd[-1] / [Math]::Max(1, [double]$wd[0]), 2) } else { $null }
    Res "map.$map" $(if (($memGrow -ne $null -and $memGrow -gt 25) -or $tex.Count -gt 1 -or ($wdRatio -ne $null -and ($wdRatio -lt 0.8 -or $wdRatio -gt 1.25))) { "FAIL" } else { "PASS" }) ("{0} visits {1}: private MB after unload {2} (revisit growth {3} MB from pass 2); GL textures released {4}; in-play world draws {5}" -f $map, $v.Count, ($mem -join " > "), $memGrow, ($tex -join "/"), ($wd -join " > ")) "Rendering/Integration"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report_m07.json")
Write-M07Matrix $rows @("cycle", "pass", "map", "frontend_private_mb", "frontend_ws_mb", "unloaded_private_mb", "handles", "threads", "gl_textures_released", "world_draws", "nav_heap", "nav_nodes") (Join-Path $OutDir "SOAK.md") "M07 multi-map soak" @("$cycles cycles = $Passes passes over $($ids.Count) maps. Pass 1 loads every map once (plateau expected); growth is judged from pass 2 on and per map revisit. R4 checks (audio release, UI movies, launches): report.json.")
"M07 SOAK: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
