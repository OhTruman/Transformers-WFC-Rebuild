# Map visual regressions (human-reported smoke / fog / steam / glass / ramps) on the MERGED build, three ways:
#   baseline = int-04 a03d7f7 playtest-regressions run (where the human saw the bugs)
#   lane     = Rendering's own branch run (where Rendering measured its fixes)
#   merged   = the integration build under test
# for every measured viewpoint (same cameras: playtest-regressions.ps1 is deterministic in its view list).
#   .\tools\fidelity\m05\visual-compare.ps1 -Baseline <dir> -Lane <dir> -Merged <dir> -OutDir <dir>
# Each <dir> holds playtest-regressions' report.json files (one or more, e.g. glass run + sheets/particles/ramps run).
#   PASS  fix_present      merged matches the lane (within tolerance) where the lane changed from the baseline
#   FAIL  fix_lost         the lane changed it, the merged build is back at (or near) the baseline value
#   FAIL  merge_diverged   merged differs from both lane and baseline (something else in the merge changed the view)
#   INFO  unchanged        all three agree
# Whether the fixed look is the original look is HUMAN (HUMAN-CHECK-M05.md); this script never decides that.
param([Parameter(Mandatory)][string]$Baseline, [Parameter(Mandatory)][string]$Lane, [Parameter(Mandatory)][string]$Merged,
      [Parameter(Mandatory)][string]$OutDir, [double]$RelTol = 0.15, [double]$AbsTol = 0.04)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "..\lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
function Load-Measures([string]$dir) {
    $m = @{}
    foreach ($f in Get-ChildItem $dir -Recurse -Filter report.json) {
        foreach ($r in @((Get-Content $f.FullName -Raw | ConvertFrom-Json).results)) {
            if ($r.id -notlike "playtest.*") { continue }
            if ($r.measured -ne $null -and "$($r.measured)" -ne "" -and $r.id -notlike "playtest.glass.*") { $m[$r.id] = [double]$r.measured }
            if ($r.id -like "playtest.glass.*") {   # per-view-class glass contribution from the note
                foreach ($k in "aboveclose", "above", "grazing", "below") { $mm = [regex]::Match($r.note, "\b$k (\d+)%"); if ($mm.Success) { $m["$($r.id).$k"] = [double]$mm.Groups[1].Value / 100 } }
            }
        }
    }
    return $m
}
$mapBase = Load-Measures $Baseline; $mapLane = Load-Measures $Lane; $mapMerged = Load-Measures $Merged
function Same($a, $b) { if ($a -eq $null -or $b -eq $null) { return $false }; $d = [Math]::Abs($a - $b); return $d -le $AbsTol -or $d -le $RelTol * [Math]::Max([Math]::Abs($a), [Math]::Abs($b)) }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($id in ($mapMerged.Keys | Sort-Object)) {
    $vb = $mapBase[$id]; $vl = $mapLane[$id]; $vm = $mapMerged[$id]
    $cls = if ($vl -eq $null -or $vb -eq $null) { "no_reference" } elseif (Same $vl $vb) { if (Same $vm $vl) { "unchanged" } else { "merge_diverged" } } elseif (Same $vm $vl) { "fix_present" } elseif (Same $vm $vb) { "fix_lost" } else { "merge_diverged" }
    $fam = ($id -split '\.')[1]; $metric = ($id -split '\.')[-1]
    $rows.Add([pscustomobject][ordered]@{ id = $id; family = $fam; metric = $metric; baseline = $vb; lane = $vl; merged = $vm; class = $cls })
}
Write-WfcCsv $rows (Join-Path $OutDir "visual_compare.csv")
$res = New-WfcResults
foreach ($g in ($rows | Group-Object family)) {
    $lost = @($g.Group | Where-Object class -eq "fix_lost"); $div = @($g.Group | Where-Object class -eq "merge_diverged"); $fix = @($g.Group | Where-Object class -eq "fix_present")
    $st = if ($lost.Count) { "FAIL" } elseif ($div.Count) { "FAIL" } elseif ($fix.Count) { "PASS" } else { "INFO" }
    Add-WfcResult $res "visual.$($g.Name).merge" $st ($lost.Count + $div.Count) ("{0} measurements: fix present {1}, fix lost {2}, merge diverged {3}, unchanged {4}, no reference {5}.{6}" -f $g.Count, $fix.Count, $lost.Count, $div.Count, @($g.Group | Where-Object class -eq "unchanged").Count, @($g.Group | Where-Object class -eq "no_reference").Count, $(if ($lost.Count + $div.Count) { " " + ((@($lost) + @($div) | Select-Object -First 6 | ForEach-Object { "{0}: base {1:N2} lane {2:N2} merged {3:N2}" -f $_.id, $_.baseline, $_.lane, $_.merged }) -join "; ") } else { "" })) "Rendering/Integration"
}
# headline numbers the human reported against
$veil = @($rows | Where-Object metric -eq "far_veil"); if ($veil.Count) { $worst = $veil | Sort-Object merged -Descending | Select-Object -First 3; Add-WfcResult $res "visual.far_veil_worst" "HUMAN" $worst[0].merged ("largest screen coverage by one smoke / steam / fog effect at 30-60 m on the merged build: " + (($worst | ForEach-Object { "{0} {1:P0} (int-04 {2:P0})" -f ($_.id -replace 'playtest\.\w+\.', '' -replace '\.far_veil', ''), $_.merged, $_.baseline }) -join ", ") + ". Whether distant smoke still veils the geometry is the human check") "Rendering" }
$glass = @($rows | Where-Object { $_.family -eq "glass" -and $_.metric -in "above", "aboveclose" }); if ($glass.Count) { Add-WfcResult $res "visual.glass_from_above" "HUMAN" (($glass | Measure-Object merged -Average).Average) ("glass contribution over its own surface seen from above, merged vs int-04: " + (($glass | ForEach-Object { "{0} {1:P0} (was {2:P0})" -f ($_.id -replace 'playtest\.glass\.', ''), $_.merged, $_.baseline }) -join ", ")) "Rendering" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"VISUAL COMPARE: " + (($rows | Group-Object class | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", ")
