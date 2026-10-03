# One command for Experimental's playtest-regression / living-map suites against any built tree.
#
#   .\tools\fidelity\checkpoint-suite.ps1 -Root <tree with build-release\bin\wfc_rebuild.exe and work\render> -OutDir <dir>
#        [-Baseline <previous checkpoint-suite OutDir>] [-Quick] [-SoakMinutes 10]
#
# Runs, in this order (one product process at a time):
#   playtest-regressions  smoke/fog veil, close-range change, red/purple, static particles, glass, ramps
#   mode-presentation     DM/TDM/CTF/EXT/DOM/KOTH objective presentation
#   match-validator       TDM + DM (MATCH protocol; spawn class today)
#   frontend-validator    FRONTEND protocol (startup -> ... -> return)
#   streets-sweep         full-footprint stills; -Baseline gives the before/after sheet
#   transform-stress      84 starts x 9 scenarios (-Quick: every 6th start)
#   soak-monitor          Release soak + duplicate-render + repeated-launch checks (skipped with -Quick)
# Writes <OutDir>\SUITE.md with PASS / FAIL / KNOWN / INFO / SKIP / HUMAN per suite, and each suite's report.json.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string]$Baseline = "", [switch]$Quick, [double]$SoakMinutes = 10)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Root = (Resolve-Path $Root).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; if (-not (Test-Path $exe)) { $exe = Join-Path $Root "build\release\bin\wfc_rebuild.exe" }
$rd = Join-Path $Root "work\render"
$t = $PSScriptRoot
$runs = [ordered]@{
    playtest  = { & "$t\playtest-regressions.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\playtest" }
    modes     = { & "$t\mode-presentation.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\modes" }
    match_tdm = { & "$t\match-validator.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\match_tdm" -Mode TDM -Frames 600 }
    match_dm  = { & "$t\match-validator.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\match_dm" -Mode DM -Frames 600 }
    frontend  = { & "$t\frontend-validator.ps1" -Exe $exe -OutDir "$OutDir\frontend" -Frames 600 }
    sweep     = { & "$t\streets-sweep.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\sweep" -Baseline $(if ($Baseline) { Join-Path $Baseline "sweep" } else { "" }) }
    stress    = { if ($Quick) { & "$t\transform-stress.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\stress" -Starts (@(0..13 | ForEach-Object { $_ * 6 }) -join ",") } else { & "$t\transform-stress.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\stress" } }
    soak      = { if (-not $Quick) { & "$t\soak-monitor.ps1" -Exe $exe -RenderData $rd -OutDir "$OutDir\soak" -Minutes $SoakMinutes } }
}
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Experimental checkpoint suite - $Root ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))"); $md.Add("")
$md.Add("| suite | PASS | FAIL | KNOWN | INFO | SKIP | HUMAN CHECK | note |"); $md.Add("|---|---|---|---|---|---|---|---|")
foreach ($k in $runs.Keys) {
    $sw = [Diagnostics.Stopwatch]::StartNew(); $err = ""
    try { & $runs[$k] | ForEach-Object { Write-Host "  [$k] $_" } } catch { $err = $_.Exception.Message }
    $rp = Join-Path (Join-Path $OutDir $k) "report.json"
    if (Test-Path $rp) { $s = (Get-Content -Raw $rp | ConvertFrom-Json).summary; $md.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7:F0} s{8} |" -f $k, $s.pass, $s.fail, $s.known, $s.info, $s.skip, $s.human, $sw.Elapsed.TotalSeconds, $(if ($err) { "; error: $err" } else { "" }))) }
    else { $md.Add("| $k | - | - | - | - | - | - | no report (tool error: $err) |") }
}
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "SUITE.md")
Get-Content (Join-Path $OutDir "SUITE.md")
