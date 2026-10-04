# Milestone 05 FINAL validation: every suite against ONE integration commit, one hand-off document.
#
#   .\tools\fidelity\m05-final.ps1 -Ref origin/integration/milestone-05 [-Name m5int] [-Build] [-Quick]
#                                  [-LaneRendering <playtest dir of Rendering's branch>] [-Only gate,debug,stress,vehicle,visual,soak]
#
# 1. build-target  : isolated export of the exact commit (Debug + Release + render data) in work\ab\<Name>
# 2. gate          : m05-e2e-gate.ps1 Release -Full (R1..R6 + owners' self-tests), 8 cycles
#    debug         : m05-e2e-gate.ps1 Debug R1,R2,R4 (3 cycles): asserts / debug-only lifetime differences
# 3. stress        : transform-stress.ps1 all scenarios x 84 starts (boost / nitro / turning / airborne / long / rapid)
# 4. vehicle       : vehicle-collision.ps1 8 scenarios x 84 starts (authored-collision sweep)
# 5. visual        : playtest-regressions.ps1 on the merged build + m05\visual-compare.ps1 vs int-04 and Rendering's lane
# 6. soak          : m05-e2e-gate.ps1 R4 with -SoakCycles cycles (memory / audio / GL ownership over many travels)
# Writes <OutDir>\M05-FINAL.md (hand-off: commit, exe path, per-area results, owners, HUMAN-CHECK list).
param([Parameter(Mandatory)][string]$Ref, [string]$Name = "m5int", [switch]$Build, [switch]$Quick, [string]$OutDir = "",
      [string]$LaneRendering = "", [string]$BaselineVisual = "", [int]$SoakCycles = 20, [string[]]$Only = @())
$ErrorActionPreference = "Stop"
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ }); function Want($k) { return -not $Only.Count -or $Only -contains $k }
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
$sha = (& git -C $root rev-parse --verify "$Ref^{commit}").Trim()
$tgt = Join-Path $root "work\ab\$Name"
if ($Build -or -not (Test-Path (Join-Path $tgt "build-release\bin\wfc_rebuild.exe"))) { & (Join-Path $PSScriptRoot "m05\build-target.ps1") -Ref $sha -Name $Name }
$built = if (Test-Path (Join-Path $tgt "M05_TARGET.txt")) { (Get-Content (Join-Path $tgt "M05_TARGET.txt") | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "" }
if ($built -and $built -ne $sha) { throw "work\ab\$Name was built from $built, not ${sha}: rebuild with -Build" }
if (-not $OutDir) { $OutDir = Join-Path $root ("work\fidelity\m05final\" + $sha.Substring(0, 7) + "-" + (Get-Date -Format "yyyyMMdd-HHmm")) }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$rel = Join-Path $tgt "build-release\bin\wfc_rebuild.exe"; $dbg = Join-Path $tgt "build\bin\wfc_rebuild.exe"; $rd = Join-Path $tgt "work\render"
$log = Join-Path $OutDir "final.log"
function Step($name, [scriptblock]$sb) { $t0 = Get-Date; "[{0}] {1} ..." -f (Get-Date -Format HH:mm:ss), $name | Tee-Object -Append $log | Out-Host; try { & $sb 2>&1 | Select-Object -Last 4 | Tee-Object -Append $log | Out-Host } catch { "  ERROR: $_" | Tee-Object -Append $log | Out-Host }; "  ({0:N0} min)" -f ((Get-Date) - $t0).TotalMinutes | Tee-Object -Append $log | Out-Host }
$starts = if ($Quick) { "0,5,12,20,33,41,47,60,71,77" } else { "" }
$cyc = if ($Quick) { 4 } else { 8 }
if (Want "gate")    { Step "gate Release" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Release -Full -Cycles $cyc -OutDir (Join-Path $OutDir "gate_release") } }
if (Want "debug")   { if (Test-Path $dbg) { Step "gate Debug" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Debug -Runs "R1,R2,R4" -Cycles 3 -OutDir (Join-Path $OutDir "gate_debug") } } }
if (Want "stress")  { Step "transform stress" { $a = @{ Exe = $rel; RenderData = $rd; OutDir = (Join-Path $OutDir "transform_stress"); Scenarios = @("all") }; if ($starts) { $a.Starts = @($starts) }; & (Join-Path $PSScriptRoot "transform-stress.ps1") @a } }
if (Want "vehicle") { Step "vehicle collision" { $a = @{ Exe = $rel; RenderData = $rd; OutDir = (Join-Path $OutDir "vehicle_collision") }; if ($starts) { $a.Starts = @($starts) }; & (Join-Path $PSScriptRoot "vehicle-collision.ps1") @a } }
if (Want "visual")  {
    Step "playtest regressions (merged)" { & (Join-Path $PSScriptRoot "playtest-regressions.ps1") -Exe $rel -RenderData $rd -OutDir (Join-Path $OutDir "visual_playtest") }
    $base = if ($BaselineVisual) { $BaselineVisual } else { Join-Path $root "work\m05\vc_base" }
    if ($LaneRendering -and (Test-Path $LaneRendering)) { Step "visual compare" { & (Join-Path $PSScriptRoot "m05\visual-compare.ps1") -Baseline $base -Lane $LaneRendering -Merged (Join-Path $OutDir "visual_playtest") -OutDir (Join-Path $OutDir "visual_compare") } }
}
if (Want "soak")    { Step "lifecycle soak" { & (Join-Path $PSScriptRoot "m05-e2e-gate.ps1") -Root $tgt -Config Release -Runs "R4" -Cycles $SoakCycles -OutDir (Join-Path $OutDir "soak") } }

# ---- hand-off document ----
function Rep($dir) { $p = Join-Path $OutDir "$dir\report.json"; if (Test-Path $p) { return @((Get-Content $p -Raw | ConvertFrom-Json).results) } else { return @() } }
function Count($rows) { $g = @{}; foreach ($s in "PASS", "FAIL", "KNOWN", "INFO", "SKIP", "HUMAN") { $g[$s] = @($rows | Where-Object status -eq $s).Count }; return ("PASS {0} / FAIL {1} / KNOWN {2} / INFO {3} / SKIP {4} / HUMAN {5}" -f $g.PASS, $g.FAIL, $g.KNOWN, $g.INFO, $g.SKIP, $g.HUMAN) }
function Area($rows, [string]$pat) { $r = @($rows | Where-Object { $_.id -match $pat }); if (-not $r.Count) { return "not run" }; $f = @($r | Where-Object status -eq "FAIL"); return ("{0}{1}" -f (Count $r), $(if ($f.Count) { "`n  - FAIL: " + (($f | Select-Object -First 8 | ForEach-Object { "``$($_.id -replace '^e2e\.', '')`` ($($_.owner)): " + ($_.note -replace "`n", ' ').Substring(0, [Math]::Min(220, $_.note.Length)) }) -join "`n  - FAIL: ") } else { "" })) }
$G = Rep "gate_release"; $GD = Rep "gate_debug"; $TS = Rep "transform_stress"; $VC = Rep "vehicle_collision"; $VP = Rep "visual_playtest"; $VX = Rep "visual_compare"; $SK = Rep "soak"
$all = @($G) + @($GD) + @($TS | Where-Object { $_.id -notlike "transform_stress.run.*" }) + @($VC | Where-Object { $_.id -notlike "vehicle_collision.actor.*" }) + @($VX) + @($SK)
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Milestone 05 final validation"); $md.Add("")
$md.Add("| | |"); $md.Add("|---|---|")
$md.Add("| integration commit | ``$sha`` ($Ref) |"); $md.Add("| executable (Release) | ``$rel`` |"); $md.Add("| executable (Debug) | ``$dbg`` |"); $md.Add("| render data | ``$rd`` |"); $md.Add("| run | $(Get-Date -Format 'yyyy-MM-dd HH:mm') $(if ($Quick) { '(QUICK subset)' }) |"); $md.Add("")
$md.Add("**Overall automated result: " + (Count $all) + "**"); $md.Add("")
$md.Add("| area | result |"); $md.Add("|---|---|")
$md.Add("| frontend lifecycle | " + ((Area $G '^e2e\.(STATE|PRESENTED|DATA)\.') -replace "`n", "<br>") + " |")
$md.Add("| TDM lifecycle | " + ((Area $G '^e2e\.MATCH\.|^e2e\.SELFTEST\.WFC_(TDM|MATCH)TEST') -replace "`n", "<br>") + " |")
$md.Add("| map / mode ownership | " + ((Area $G '^e2e\.OWNERSHIP\.') -replace "`n", "<br>") + " |")
$md.Add("| audio lifecycle | " + ((Area (@($G) + @($SK)) '^e2e\.AUDIO\.') -replace "`n", "<br>") + " |")
$md.Add("| memory / resource soak ($SoakCycles cycles) | " + ((Area $SK '^e2e\.LIFETIME\.') -replace "`n", "<br>") + " |")
$md.Add("| boost-transform stress | " + ((Area (@($TS) + @($G)) '^transform_stress\.[a-z0-9_]+$|^e2e\.SELFTEST\.WFC_(XFORMTEST|CHAOS)') -replace "`n", "<br>") + " |")
$md.Add("| vehicle collision | " + ((Area $VC '^vehicle_collision\.[a-z_]+$') -replace "`n", "<br>") + " |")
$md.Add("| visual regressions (merged vs int-04 vs Rendering lane) | " + ((Area $VX '^visual\.') -replace "`n", "<br>") + " |")
$md.Add("| Debug build | " + ((Area $GD '.') -replace "`n", "<br>") + " |"); $md.Add("")
$md.Add("## Every FAIL (owner, evidence)"); $md.Add("")
foreach ($f in @($all | Where-Object status -eq "FAIL")) { $md.Add("- **$($f.id)** ($($f.owner)): " + ($f.note -replace "`n", ' ')) }
$md.Add(""); $md.Add("## KNOWN (documented, not regressions)"); $md.Add("")
foreach ($f in @($all | Where-Object status -eq "KNOWN")) { $md.Add("- $($f.id) ($($f.owner)): " + ($f.note -replace "`n", ' ')) }
$md.Add(""); $md.Add("## HUMAN-CHECK"); $md.Add(""); $md.Add("See ``tools/fidelity/HUMAN-CHECK-M05.md``; the automated items that framed them:"); $md.Add("")
foreach ($f in @($all | Where-Object status -eq "HUMAN")) { $md.Add("- $($f.id): " + ($f.note -replace "`n", ' ')) }
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "M05-FINAL.md")
"M05 FINAL ($sha): " + (Count $all) + " -> $OutDir\M05-FINAL.md"
