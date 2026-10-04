# FINAL GATE for the next integration milestone: every Experimental suite against ONE integration commit.
#
#   .\tools\fidelity\final-gate.ps1 -Ref origin/integration/<milestone> [-Name <n>] [-Build] [-Quick] [-Only a,b]
#                                   [-LaneRendering <Rendering-lane playtest dir>] [-Baseline <previous final dir>]
#
# Suites (in order; timing-sensitive real-time runs first, the long lockstep stress suites after):
#   gate     m05-e2e-gate.ps1 Release -Full: cold boot / intro / keys through the shipped menus / TDM lifecycle /
#            time-limit match / score-limit x3 / cycles / map + mode ownership / owners' self-tests
#   accept   playtest-acceptance.ps1: the human-playtest issues (intro audio, title background + keys, back
#            navigation, loading updates + overlay, match state vs HUD table, kill feed, result state, minimap)
#   jitter   motion-jitter.ps1: sim / anim discontinuities, presentation ping-pong, frame pacing at 60/144/240 Hz,
#            per-frame vehicle frames, vehicle states vs RE
#   debug    m05-e2e-gate.ps1 Debug R1,R2,R4
#   stress   transform-stress.ps1 all scenarios (boost -> robot under-map regression)
#   vehicle  vehicle-collision.ps1 (pass-through + ramp hard stops)
#   visual   playtest-regressions.ps1 + m05\visual-compare.ps1 (int-04 baseline / Rendering lane / merged)
#   soak     m05-e2e-gate.ps1 R4 x SoakCycles
# Report FINAL.md: PRODUCT FAIL (owner lane) / TEST-HARNESS FAIL (owner Experimental) / KNOWN / UNKNOWN + WAITING /
# HUMAN CHECK, per area, with the exact commit and exe paths. A Debug and a Release build of the same sha are required.
param([Parameter(Mandatory)][string]$Ref, [string]$Name = "", [switch]$Build, [switch]$Quick, [string]$OutDir = "",
      [string]$LaneRendering = "", [string]$BaselineVisual = "", [int]$SoakCycles = 20, [string[]]$Only = @())
$ErrorActionPreference = "Stop"
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ }); function Want($k) { return -not $Only.Count -or $Only -contains $k }
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
$sha = (& git -C $root rev-parse --verify "$Ref^{commit}").Trim()
if (-not $Name) { $Name = "int_" + $sha.Substring(0, 7) }
$tgt = Join-Path $root "work\ab\$Name"
if ($Build -or -not (Test-Path (Join-Path $tgt "build-release\bin\wfc_rebuild.exe"))) { & (Join-Path $PSScriptRoot "m05\build-target.ps1") -Ref $sha -Name $Name }
$built = if (Test-Path (Join-Path $tgt "M05_TARGET.txt")) { (Get-Content (Join-Path $tgt "M05_TARGET.txt") | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "" }
if ($built -ne $sha) { throw "work\ab\$Name holds ${built}, not ${sha}: rerun with -Build (never mix product revisions)" }
$rel = Join-Path $tgt "build-release\bin\wfc_rebuild.exe"; $dbg = Join-Path $tgt "build\bin\wfc_rebuild.exe"; $rd = Join-Path $tgt "work\render"
if (-not $OutDir) { $OutDir = Join-Path $root ("work\final\" + $sha.Substring(0, 7) + "-" + (Get-Date -Format "yyyyMMdd-HHmm")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$log = Join-Path $OutDir "final.log"
function Note($s) { Add-Content -Encoding UTF8 $log $s; Write-Host $s }
function Step($name, [scriptblock]$sb) { $t0 = Get-Date; Note ("[{0}] {1} ..." -f (Get-Date -Format HH:mm:ss), $name); try { & $sb 2>&1 | Select-Object -Last 3 | ForEach-Object { Note "  $_" } } catch { Note "  ERROR: $_" }; Note ("  ({0:N0} min)" -f ((Get-Date) - $t0).TotalMinutes) }
$starts = if ($Quick) { "0,5,12,20,33,41,47,60,71,77" } else { "" }
$cyc = if ($Quick) { 4 } else { 8 }
Note "FINAL GATE $Ref = $sha; Release $rel; Debug $dbg"
if (Want "gate")    { Step "gate Release" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Release -Full -Cycles $cyc -OutDir (Join-Path $OutDir "gate_release") } }
if (Want "accept")  { Step "playtest acceptance" { & (Join-Path $PSScriptRoot "playtest-acceptance.ps1") -Root $tgt -OutDir (Join-Path $OutDir "acceptance") } }
if (Want "jitter")  { Step "motion jitter" { & (Join-Path $PSScriptRoot "motion-jitter.ps1") -Exe $rel -RenderData $rd -OutDir (Join-Path $OutDir "jitter") } }
if (Want "debug")   { if (Test-Path $dbg) { Step "gate Debug" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Debug -Runs "R1,R2,R4" -Cycles 3 -OutDir (Join-Path $OutDir "gate_debug") } } else { Note "no Debug exe: the Debug leg is missing" } }
if (Want "stress")  { Step "transform stress" { $a = @{ Exe = $rel; RenderData = $rd; OutDir = (Join-Path $OutDir "transform_stress"); Scenarios = @("all") }; if ($starts) { $a.Starts = @($starts) }; & (Join-Path $PSScriptRoot "transform-stress.ps1") @a } }
if (Want "vehicle") { Step "vehicle collision" { $a = @{ Exe = $rel; RenderData = $rd; OutDir = (Join-Path $OutDir "vehicle_collision") }; if ($starts) { $a.Starts = @($starts) }; & (Join-Path $PSScriptRoot "vehicle-collision.ps1") @a } }
if (Want "visual")  {
    Step "playtest regressions (merged)" { & (Join-Path $PSScriptRoot "playtest-regressions.ps1") -Exe $rel -RenderData $rd -OutDir (Join-Path $OutDir "visual_playtest") }
    $base = if ($BaselineVisual) { $BaselineVisual } else { Join-Path $root "work\m05\vc_base" }
    $lane = if ($LaneRendering) { $LaneRendering } else { Join-Path $root "tools\fidelity\results\m05-prep\lane_rendering_187e6e9" }
    if (Test-Path $lane) { Step "visual compare" { & (Join-Path $PSScriptRoot "m05\visual-compare.ps1") -Baseline $base -Lane $lane -Merged (Join-Path $OutDir "visual_playtest") -OutDir (Join-Path $OutDir "visual_compare") } }
}
if (Want "soak")    { Step "lifecycle soak" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Release -Runs "R4" -Cycles $SoakCycles -OutDir (Join-Path $OutDir "soak") } }

# ---------------------------------------------------------------- FINAL.md
function Rep($dir) { $p = Join-Path $OutDir "$dir\report.json"; if (Test-Path $p) { return @((Get-Content $p -Raw | ConvertFrom-Json).results) } else { return @() } }
$suites = [ordered]@{ gate_release = "lifecycle gate (Release)"; acceptance = "playtest acceptance"; jitter = "motion / jitter"; gate_debug = "lifecycle gate (Debug)"; transform_stress = "transform stress"; vehicle_collision = "vehicle collision"; visual_compare = "visual (merged vs baseline vs lane)"; soak = "lifetime soak" }
$all = @(); $per = [ordered]@{}
foreach ($k in $suites.Keys) { $rows = @(Rep $k | Where-Object { $_.id -notlike "transform_stress.run.*" -and $_.id -notlike "vehicle_collision.actor.*" }); $per[$k] = $rows; $all += $rows }
# classification: a FAIL owned by Experimental (or flagged as a tool fault) is a test / harness failure, not a product failure
function Kind($r) { if ($r.status -ne "FAIL") { return $r.status }; if ("$($r.owner)" -match '^Experimental$' -or "$($r.note)" -match 'TEST FAULT|harness') { return "TEST" }; return "PRODUCT" }
function Cnt($rows) { $c = [ordered]@{ "PRODUCT FAIL" = 0; "TEST/HARNESS FAIL" = 0; PASS = 0; KNOWN = 0; "UNKNOWN/WAITING" = 0; HUMAN = 0; INFO = 0; SKIP = 0; PARTIAL = 0 }
    foreach ($r in $rows) { switch (Kind $r) { "PRODUCT" { $c["PRODUCT FAIL"]++ } "TEST" { $c["TEST/HARNESS FAIL"]++ } "UNKNOWN" { $c["UNKNOWN/WAITING"]++ } "WAITING" { $c["UNKNOWN/WAITING"]++ } default { if ($c.Contains($_)) { $c[$_]++ } } } }
    return (($c.Keys | ForEach-Object { "$_ $($c[$_])" }) -join " / ") }
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Final gate - $Ref"); $md.Add("")
$md.Add("| | |"); $md.Add("|---|---|"); $md.Add("| integration commit | ``$sha`` |"); $md.Add("| Release exe | ``$rel`` |"); $md.Add("| Debug exe | ``$dbg`` $(if (-not (Test-Path $dbg)) { '(MISSING)' }) |"); $md.Add("| run | $(Get-Date -Format 'yyyy-MM-dd HH:mm')$(if ($Quick) { ' QUICK subset' }) |"); $md.Add("")
$md.Add("**" + (Cnt $all) + "**"); $md.Add("")
$md.Add("| suite | result |"); $md.Add("|---|---|"); foreach ($k in $suites.Keys) { $md.Add("| $($suites[$k]) | $(if ($per[$k].Count) { Cnt $per[$k] } else { 'not run' }) |") }; $md.Add("")
foreach ($sec in @(@{ t = "PRODUCT FAIL (owner lane)"; f = { (Kind $args[0]) -eq "PRODUCT" } }, @{ t = "TEST / HARNESS FAIL (Experimental)"; f = { (Kind $args[0]) -eq "TEST" } }, @{ t = "KNOWN (documented gaps)"; f = { $args[0].status -eq "KNOWN" } }, @{ t = "UNKNOWN / WAITING (evidence or presentation pending)"; f = { $args[0].status -in "UNKNOWN", "WAITING", "PARTIAL" } }, @{ t = "HUMAN CHECK (framing evidence)"; f = { $args[0].status -eq "HUMAN" } })) {
    $rows = @($all | Where-Object { & $sec.f $_ }); $md.Add("## $($sec.t): $($rows.Count)"); $md.Add("")
    foreach ($r in $rows) { $md.Add("- **$($r.id)** [$($r.owner)]: " + (("$($r.note)" -replace "`n", ' ') -replace '\|', '/')) }; $md.Add("")
}
$md.Add("Human playtest list: ``tools/fidelity/HUMAN-CHECK-NEXT.md``.")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "FINAL.md")
Note ("FINAL GATE ${sha}: " + (Cnt $all) + " -> $OutDir\FINAL.md")
