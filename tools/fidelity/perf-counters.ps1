# Exact per-frame call counters for the remaining performance handoffs (wfc_rebuild_count:
# lockstep clock + -finstrument-functions on collision / world / FX / skinning / WFC pipeline).
#
#   .\tools\fidelity\perf-counters.ps1                    # idle / held_fire -> work\fidelity\counters\
#
# Counters per frame (watched functions, resolved by name with llvm-nm, so they follow code moves):
#   visibility_rays      World::loadVerticalSlice visibility-query lambda (one per light visibility test)
#   segment_hits         CollisionWorld::segmentHit (all: visibility pieces + hitscan + aim + camera)
#   light_envs           Pipeline::computeEnv (dynamic-light selection + env per dynamic object)
#   skin_calls           assets::skinPose (CPU skinning of one model pose)
#   dynamic_draws        Pipeline::drawDynamic (skinned mesh vertex build + upload + draw)
#   vertex_builds        Pipeline::buildVertices
#   mesh_draws           Pipeline::draw (static/mesh-particle draws: shells, magazines, weapon)
#   fx_draws             WeaponFx::draw
#   hitscans             World::fireHitscan
# Counts are exact and deterministic (lockstep). Inclusive times are inflated by instrumentation:
# use them only as relative shares; real frame costs come from perf-profile.ps1 / perf-report.ps1.
param([string[]]$Scenarios = @("idle", "held_fire"), [string]$Exe = "", [string]$OutDir = "", [int]$TimeoutSec = 1800)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_count.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\counters" }
if (-not (Test-Path $Exe)) { throw "measurement build missing: $Exe (cmake -DWFC_BUILD_MEASURE=ON; target wfc_rebuild_count)" }
$nm = Find-WfcTool "llvm-nm" $root
$watch = [ordered]@{
    visibility_rays = 'World::loadVerticalSlice\(.*\)::\$_\d+::operator\(\)\(core::Vec3 const&, core::Vec3 const&\) const$'
    segment_hits    = 'CollisionWorld::segmentHit\('
    light_envs      = 'Pipeline::computeEnv\('
    skin_calls      = '^assets::skinPose\('
    dynamic_draws   = 'Pipeline::drawDynamic\('
    vertex_builds   = 'Pipeline::buildVertices\('
    mesh_draws      = 'Pipeline::draw\(int, core::Mat4 const&\)'
    fx_draws        = 'WeaponFx::draw\('
    hitscans        = 'World::fireHitscan\('
}
$base = [UInt64]"0x140000000"
$syms = & $nm -C --defined-only $Exe | Where-Object { $_ -match "^[0-9a-f]+ [Tt] " } | ForEach-Object { $p = $_.Split(" ", 3); [pscustomobject]@{ addr = $p[0]; name = $p[2] } }
$rvaOf = [ordered]@{}
foreach ($k in $watch.Keys) {
    $hit = $syms | Where-Object { $_.name -match $watch[$k] } | Select-Object -First 1
    if ($hit) { $rvaOf[$k] = "{0:x}" -f ([Convert]::ToUInt64($hit.addr, 16) - $base) }
}
$defs = [ordered]@{
    idle      = @{ frames = 300; env = @{} }
    held_fire = @{ frames = 120; env = @{ WFC_AUTOFIRE = "1" } }   # short: pre-fix traces are millions of calls
    walk      = @{ frames = 300; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.0" } }
}
$res = New-WfcResults
$summary = [ordered]@{}
foreach ($name in $Scenarios) {
    $d = $defs[$name]; $dir = Join-Path $OutDir $name
    $out = Join-Path $dir "counts.txt"
    $envs = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_COUNT = $out; WFC_COUNT_WATCH = (($rvaOf.Values) -join ",") } + $d.env
    $rc = Invoke-WfcExe $Exe $dir $envs "run.log" $TimeoutSec   # -999: killed (pre-fix traces under instrumentation)
    $nameOf = @{}; foreach ($k in $rvaOf.Keys) { $nameOf[$rvaOf[$k]] = $k }
    $per = @{}   # frame -> counter -> calls
    $frameUs = @{}
    if (-not (Test-Path $out)) {   # the counting exe produced nothing (crash / missing build): a tool problem, not a product result
        Add-WfcResult $res "perf_counters.$name.ran" "SKIP" $rc ("no counts.txt (exit {0}); see {1}" -f $rc, (Join-Path $dir "run.log"))
        continue
    }
    foreach ($ln in [IO.File]::ReadLines($out)) {
        $q = $ln.Split(" ")
        if ($q.Length -lt 3) { continue }   # truncated last line of a killed run
        if ($q[0] -eq "F") { $frameUs[[int]$q[1]] = [double]$q[2] }
        elseif ($q[0] -eq "W" -and $q.Length -ge 4 -and $nameOf[$q[2]]) { $f = [int]$q[1]; if (-not $per[$f]) { $per[$f] = @{} }; $per[$f][$nameOf[$q[2]]] = [int]$q[3] }
    }
    # shots per frame from the frame log (ammo drop)
    $log = Read-WfcFrames (Join-Path $dir "wfc.log")
    $rows = New-Object System.Collections.Generic.List[object]
    $prevAmmo = $null
    foreach ($fr in $log) {
        $f = [int]$fr.frame
        $ammo = [int](($fr.ammo -split "/")[0])
        $shots = if ($null -ne $prevAmmo -and $ammo -lt $prevAmmo) { $prevAmmo - $ammo } else { 0 }
        $prevAmmo = $ammo
        $row = [ordered]@{ frame = $f; shots = $shots }
        foreach ($k in $watch.Keys) { $row[$k] = $(if ($per[$f] -and $per[$f][$k]) { $per[$f][$k] } else { 0 }) }
        $row.instrumented_frame_us = $frameUs[$f]
        $rows.Add([pscustomobject]$row)
    }
    Write-WfcCsv $rows (Join-Path $dir "counters.csv")
    $steady = @($rows | Where-Object { $_.frame -gt 20 })
    $firing = @($steady | Where-Object { $_.shots -gt 0 })
    $s = [ordered]@{}
    foreach ($k in $watch.Keys) {
        $all = ($steady | Measure-Object $k -Average).Average
        $s[$k] = [ordered]@{ per_frame = [Math]::Round($all, 2) }
        if ($firing.Count) { $s[$k].per_firing_frame = [Math]::Round(($firing | Measure-Object $k -Average).Average, 2) }
        Add-WfcResult $res "perf_counters.$name.$k" "INFO" $all ("mean calls per frame (frames 21+){0}" -f $(if ($firing.Count) { "; firing frames " + $s[$k].per_firing_frame } else { "" })) "" $null "calls"
    }
    $summary[$name] = $s
    $st = if ($rc -eq 0) { "PASS" } elseif ($rc -eq -999) { "INFO" } else { "FAIL" }
    Add-WfcResult $res "perf_counters.$name.ran" $st $rows.Count $(if ($rc -eq -999) { "timed out after $TimeoutSec s: partial counts (pre-fix collision under instrumentation is impractical)" } else { "counted frames (exit $rc)" })
    "{0}: " -f $name + (($watch.Keys | ForEach-Object { "{0}={1}" -f $_, $s[$_].per_frame }) -join " ")
}
$summary | ConvertTo-Json -Depth 5 | Set-Content -Encoding UTF8 (Join-Path $OutDir "counters.json")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"watched: " + (($rvaOf.Keys | ForEach-Object { "$_@$($rvaOf[$_])" }) -join ", ")
"missing symbols: " + (@($watch.Keys | Where-Object { -not $rvaOf.Contains($_) }) -join ", ")
