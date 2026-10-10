# 8-MAP LOCKSTEP REGRESSION SUITE (tier TARGETED; MILESTONES_09c "Regression suite": after every merge batch that touches
# rendering, sim or UI, one lockstep pass on all 8 TDM maps vs the last baseline; a row worse by > 0.15 ms p90 BLOCKS the next
# playtest build until explained or reverted).
# Per map: tools/fidelity/lockstep-ab.ps1 with arms base (baseline exe) and head (new exe), 64p TDM real play (WFC_PLAYERBOT),
# 3D 3840x2160, the same seed, 2 runs per arm (lockstep A/A validation 2026-10-10: noise <= 0.04 ms but ~1 run in 6 is an
# outlier of ~0.15 ms, so a single run can false-flag); the verdict compares the BETTER (lower p90) valid run of each arm.
# A map is UNKNOWN when the two builds do not play the same match (SIMHASH differs: a sim change in the batch - use the
# free-running verdict instead) or the workload counts differ, or a row was contaminated.
#
#   .\tools\fidelity\regress-8map.ps1 -Base work\ab\<baseline> -Head work\ab\<head> -OutDir <dir> [-Maps 501,...] [-Steps 9600]
param([Parameter(Mandatory)][string]$Base, [Parameter(Mandatory)][string]$Head, [Parameter(Mandatory)][string]$OutDir,
      [string[]]$Maps = @("501", "502", "503", "504", "507", "508", "509", "510"), [int]$Steps = 9600, [int]$From = 600,
      [string]$RenderSize = "3840x2160", [double]$Block = 0.15, [int]$Seed = 1234, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$Maps = @($Maps | ForEach-Object { "$_" -split '[,;\s]+' } | Where-Object { $_ })
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "regress.$id" $status $null $note $owner }
$ab = Join-Path $PSScriptRoot "lockstep-ab.ps1"
$table = New-Object System.Collections.Generic.List[string]
$table.Add("| map | base p90 (best of 2) | head p90 (best of 2) | delta | verdict | note |"); $table.Add("|---|---|---|---|---|---|")
foreach ($m in $Maps) {
    $d = Join-Path $OutDir "map-$m"
    $args = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $ab, "-OutDir", $d, "-Map", $m, "-Arms", "base|$Base|;;head|$Head|",
              "-Steps", "$Steps", "-From", "$From", "-RenderSize", $RenderSize, "-PlayerBot", "1", "-Reps", "2", "-Seed", "$Seed")
    if ($ReportOnly) { $args += "-ReportOnly" }
    & powershell @args | Set-Content -Encoding UTF8 (Join-Path $OutDir "map-$m.out")
    $rep = Join-Path $d "report.json"; $csv = Join-Path $d "lockstep.csv"
    if (-not (Test-Path $rep) -or -not (Test-Path $csv)) { Res "$m" "UNKNOWN" "no lockstep result" "Experimental"; $table.Add("| $m | - | - | - | UNKNOWN | no result |"); continue }
    $rr = (Get-Content -Raw $rep | ConvertFrom-Json).results
    $ok = @{}; foreach ($x in $rr) { if ($x.id -match '^lockstep\.(base|head)\.r(\d)$') { $ok["$($Matches[1]).$($Matches[2])"] = ($x.status -eq "PASS") } }
    $rows = @(Import-Csv $csv)
    $best = { param($arm) @($rows | Where-Object { $_.arm -eq $arm -and $ok["$arm.$($_.rep)"] } | ForEach-Object { [double]$_.p90 } | Sort-Object)[0] }
    $b = & $best "base"; $h = & $best "head"
    $bad = @($rr | Where-Object { $_.status -ne "PASS" -and $_.id -match '^lockstep\.' } | ForEach-Object { "$($_.id) $($_.status)" })
    if ($null -eq $b -or $null -eq $h) {
        Res "$m" "UNKNOWN" ("no valid run pair (" + ($bad -join "; ") + ") - sim change in the batch or contamination: use the free-running verdict") "Experimental"
        $table.Add("| $m | $b | $h | - | UNKNOWN | $($bad -join '; ') |"); continue
    }
    $delta = [Math]::Round($h - $b, 2); $st = if ($delta -gt $Block) { "FAIL" } else { "PASS" }
    Res "$m" $st ("head p90 {0} vs base {1} (best valid of 2 runs each; delta {2:+0.00;-0.00} ms; BLOCK above +{3}){4}" -f $h, $b, $delta, $Block, $(if ($bad.Count) { "; invalid runs: " + ($bad -join "; ") } else { "" })) "Rendering"
    $table.Add(("| {0} | {1} | {2} | {3:+0.00;-0.00} | {4} | {5} |" -f $m, $b, $h, $delta, $(if ($st -eq "FAIL") { "**BLOCK**" } else { "PASS" }), ($bad -join '; ')))
}
$table | Set-Content -Encoding UTF8 (Join-Path $OutDir "REGRESSION.md")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"REGRESSION (8 maps, lockstep): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$table
