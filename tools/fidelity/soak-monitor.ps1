# Long-running Release soak: frame time, memory, voices, map FX, draws over real (wall-clock) play.
#
#   .\tools\fidelity\soak-monitor.ps1 -Exe <Release wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Minutes 10] [-Scenarios robot,vehicle]
#
# Per scenario, one Release process runs real time (no lockstep) with scripted play; the monitor samples the
# process every 5 s (private bytes, working set, handles, threads) and parses the product's own periodic
# lines: WFC_RENDERSTATS ("avg frame", "draws", "map FX per frame ... sprites") and WFC_AMBLOG ("AMB ...
# voices=.. cues=.."), plus the frame log (WFC_LOGEVERY).
# Trend = least-squares slope over the run after a 60 s warm-up, and last-quartile vs first-quartile means.
#   FAIL leak        private bytes grow > 4 MB/min with R^2 > 0.8 (steady growth)        [genuine regression class]
#   FAIL runaway     map-FX sprites, live cues or mixer voices: last quartile > 2x first + 20
#   HUMAN perf       frame-time last quartile > first quartile x 1.3 + 0.5 ms (degradation over time)
#   INFO otherwise, with the series in soak_<scenario>.csv
# Separately: duplicated renders (one frame report per start: draws per material vs that material's authored
# component sections in the active dump) and repeated launches (memory after load across 4 launches).
# Not testable in-process (no product hook): map reload, repeated transforms (only WFC_PRESSTRANSFORM +
# WFC_AUTOTRANSFORM = 2 per run; proposal WFC_AUTOTRANSFORM_EVERY=N).
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [double]$Minutes = 10, [string[]]$Scenarios = @("robot", "vehicle"), [switch]$SkipSoak)
$ErrorActionPreference = "Stop"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$res = New-WfcResults
function Fit($xs, $ys) {   # slope, R^2
    $n = $xs.Count; if ($n -lt 3) { return @(0, 0) }
    $mx = ($xs | Measure-Object -Average).Average; $my = ($ys | Measure-Object -Average).Average
    $sxx = 0.0; $sxy = 0.0; $syy = 0.0; for ($i = 0; $i -lt $n; $i++) { $dx = $xs[$i] - $mx; $dy = $ys[$i] - $my; $sxx += $dx * $dx; $sxy += $dx * $dy; $syy += $dy * $dy }
    if ($sxx -eq 0 -or $syy -eq 0) { return @(0, 0) }
    return @(($sxy / $sxx), (($sxy * $sxy) / ($sxx * $syy)))
}
function Quart($v) { $v = @($v); $q = [Math]::Max(1, [int]($v.Count / 4)); return @((($v[0..($q - 1)] | Measure-Object -Average).Average), (($v[($v.Count - $q)..($v.Count - 1)] | Measure-Object -Average).Average)) }
function Start-Wfc($dir, $envs) {
    New-Item -ItemType Directory -Force $dir | Out-Null
    $saved = @{}; foreach ($k in $envs.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, $envs[$k]) }
    try { $p = Start-Process -FilePath $Exe -WorkingDirectory $dir -PassThru -WindowStyle Normal -RedirectStandardOutput (Join-Path $dir "run.log") -RedirectStandardError (Join-Path $dir "run.log.err") }
    finally { foreach ($k in $envs.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) } }
    return $p
}
$defs = @{
    robot   = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.3"; WFC_AUTOFIRE = "1"; WFC_AUTOJUMP_EVERY = "420"; WFC_PRESSTRANSFORM = "3000"; WFC_AUTOTRANSFORM = "6000"; WFC_SPAWN_INDEX = "3" }
    vehicle = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.25"; WFC_AUTOBOOST_CYCLE = "300"; WFC_AUTOJUMP_EVERY = "500"; WFC_PRESSTRANSFORM = "4000"; WFC_AUTOTRANSFORM = "8000"; WFC_SPAWN_INDEX = "18" }
}
# ---------------- soak ----------------
if (-not $SkipSoak) { foreach ($sc in $Scenarios) {
    $dir = Join-Path $OutDir "soak_$sc"
    $envs = @{ WFC_RENDERSTATS = "1"; WFC_AMBLOG = "1"; WFC_LOGEVERY = "600"; WFC_NOMOUSE = "1"; WFC_SMOKE_FRAMES = "100000000" } + $defs[$sc]
    if ($RenderData) { $envs.WFC_RENDER_DATA = $RenderData }
    $p = Start-Wfc $dir $envs
    $t0 = Get-Date; $samples = New-Object System.Collections.Generic.List[object]
    while (-not $p.HasExited -and ((Get-Date) - $t0).TotalMinutes -lt $Minutes) {
        Start-Sleep -Seconds 5
        try { $p.Refresh(); $samples.Add([pscustomobject]@{ t = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1); private_mb = [Math]::Round($p.PrivateMemorySize64 / 1MB, 1); ws_mb = [Math]::Round($p.WorkingSet64 / 1MB, 1); handles = $p.HandleCount; threads = $p.Threads.Count }) } catch { }
    }
    $crashed = $p.HasExited
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2 }
    Write-WfcCsv $samples (Join-Path $OutDir "soak_${sc}_process.csv")
    $L = @(Get-Content (Join-Path $dir "wfc.log") -ErrorAction SilentlyContinue)
    $fr = @($L | Select-String "avg frame ([\d.]+) ms .* per frame: ([\d.]+) draws" | ForEach-Object { @([double]$_.Matches[0].Groups[1].Value, [double]$_.Matches[0].Groups[2].Value) })
    $fx = @($L | Select-String "map FX per frame: [\d.]+ ms, ([\d.]+) sprites, ([\d.]+) mesh" | ForEach-Object { [double]$_.Matches[0].Groups[1].Value + [double]$_.Matches[0].Groups[2].Value })
    $amb = @($L | Select-String "AMB zone=\S+ emitters=\d+/\d+ oneShots=\d+ cues=(\d+) .*voices=(-?\d+)" | ForEach-Object { @([double]$_.Matches[0].Groups[1].Value, [double]$_.Matches[0].Groups[2].Value) })
    $id = "soak.$sc"
    Add-WfcResult $res "$id.ran" $(if ($crashed -and ((Get-Date) - $t0).TotalMinutes -lt $Minutes * 0.95) { "FAIL" } else { "PASS" }) ([Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) ("{0:F1} min wall clock; {1} RENDERSTATS windows; exited early: {2} (exit {3})" -f ((Get-Date) - $t0).TotalMinutes, $fr.Count, $crashed, $(if ($crashed) { $p.ExitCode } else { "-" }))
    $warm = @($samples | Where-Object { $_.t -ge 60 })
    if ($warm.Count -ge 6) {
        $f = Fit @($warm | ForEach-Object { $_.t / 60.0 }) @($warm | ForEach-Object { $_.private_mb })
        $leak = $f[0] -gt 4 -and $f[1] -gt 0.8
        Add-WfcResult $res "$id.memory_growth" $(if ($leak) { "FAIL" } else { "PASS" }) ([Math]::Round($f[0], 2)) ("private bytes slope {0:F2} MB/min (R^2 {1:F2}) after warm-up; {2:F0} -> {3:F0} MB; handles {4} -> {5}" -f $f[0], $f[1], $warm[0].private_mb, $warm[-1].private_mb, $warm[0].handles, $warm[-1].handles) "" $null "MB/min"
        $h = Fit @($warm | ForEach-Object { $_.t / 60.0 }) @($warm | ForEach-Object { [double]$_.handles })
        Add-WfcResult $res "$id.handle_growth" $(if ($h[0] -gt 20 -and $h[1] -gt 0.8) { "FAIL" } else { "PASS" }) ([Math]::Round($h[0], 1)) ("handle slope {0:F1}/min (R^2 {1:F2})" -f $h[0], $h[1])
    }
    if ($fr.Count -ge 8) {
        $ms = Quart ($fr | ForEach-Object { $_[0] }); $dr = Quart ($fr | ForEach-Object { $_[1] })
        Add-WfcResult $res "$id.frame_time_trend" $(if ($ms[1] -gt $ms[0] * 1.3 + 0.5) { "HUMAN" } else { "PASS" }) ([Math]::Round($ms[1], 2)) ("avg frame first quartile {0:F2} ms -> last quartile {1:F2} ms (view-dependent; flagged if > x1.3 + 0.5 ms)" -f $ms[0], $ms[1]) "" $null "ms"
        Add-WfcResult $res "$id.draws_trend" $(if ($dr[1] -gt 2 * $dr[0] + 200) { "FAIL" } else { "PASS" }) ([Math]::Round($dr[1], 0)) ("draws per frame first quartile {0:F0} -> last {1:F0}" -f $dr[0], $dr[1])
    }
    if ($fx.Count -ge 8) { $q = Quart $fx; Add-WfcResult $res "$id.map_fx_runaway" $(if ($q[1] -gt 2 * $q[0] + 20) { "FAIL" } else { "PASS" }) ([Math]::Round($q[1], 1)) ("map FX sprites + mesh particles per frame: first quartile {0:F1} -> last {1:F1}" -f $q[0], $q[1]) }
    if ($amb.Count -ge 8) {
        $c = Quart ($amb | ForEach-Object { $_[0] }); $v = Quart ($amb | ForEach-Object { $_[1] })
        Add-WfcResult $res "$id.cue_runaway" $(if ($c[1] -gt 2 * $c[0] + 20) { "FAIL" } else { "PASS" }) ([Math]::Round($c[1], 1)) ("live cues first quartile {0:F1} -> last {1:F1}" -f $c[0], $c[1])
        Add-WfcResult $res "$id.voice_runaway" $(if ($v[1] -gt 2 * $v[0] + 20) { "FAIL" } else { "PASS" }) ([Math]::Round($v[1], 1)) ("mixer voices first quartile {0:F1} -> last {1:F1} (96-channel budget)" -f $v[0], $v[1])
    }
    $fst = @($L | Select-String "\] frame \d+ pos" ); Add-WfcResult $res "$id.state_samples" "INFO" $fst.Count ("frame-state samples (every 600 frames); transforms requested at frames {0} and {1}; forms seen: {2}" -f $defs[$sc].WFC_PRESSTRANSFORM, $defs[$sc].WFC_AUTOTRANSFORM, ((@($L | Select-String "form=(\w+)" | ForEach-Object { $_.Matches[0].Groups[1].Value }) | Group-Object | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", "))
    $rows = for ($i = 0; $i -lt $fr.Count; $i++) { [pscustomobject]@{ window = $i; frame_ms = $fr[$i][0]; draws = $fr[$i][1]; mapfx = $(if ($i -lt $fx.Count) { $fx[$i] }) } }
    Write-WfcCsv @($rows) (Join-Path $OutDir "soak_${sc}_render.csv")
    Remove-Item (Join-Path $dir "wfc.log") -ErrorAction SilentlyContinue   # large; series kept in the CSVs
} }
# ---------------- duplicated renders ----------------
foreach ($k in 0, 9, 18) {
    $d = Join-Path $OutDir "drawcheck_s$k"
    $e = @{ WFC_SMOKE_FRAMES = "60"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0"; WFC_SPAWN_INDEX = "$k"; WFC_FRAMEREPORT = (Join-Path $d "frame.txt"); WFC_AUDIT_DUMP = (Join-Path $d "dump.jsonl") }
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $null = Invoke-WfcExe $Exe $d $e "run.log" 600
    if (-not (Test-Path (Join-Path $d "frame.txt"))) { Add-WfcResult $res "drawcheck.s$k" "SKIP" $null "no frame report"; continue }
    $sections = @{}; foreach ($ln in [IO.File]::ReadLines((Join-Path $d "dump.jsonl"))) { if ($ln -match '"material":"([^"]*)"') { $sections[$Matches[1]] = [int]$sections[$Matches[1]] + 1 } }
    $over = @()
    foreach ($ln in Get-Content (Join-Path $d "frame.txt")) {
        if ($ln -match '^\s{2}(\S+)\s+(\d+)\s+(opaque|masked|translucent|additive|modulate)\s' ) {
            $m = $Matches[1]; $n = [int]$Matches[2]
            if ($sections.ContainsKey($m) -and $n -gt $sections[$m]) { $over += "$m drew $n x for $($sections[$m]) sections" }
        }
    }
    Add-WfcResult $res "drawcheck.s$k" $(if ($over.Count) { "HUMAN" } else { "PASS" }) $over.Count ("materials drawn more times in one frame than they have uploaded component sections (duplicate-render candidates; multi-pass materials can legitimately exceed): " + (($over | Select-Object -First 8) -join "; "))
}
# ---------------- repeated launches ----------------
$mem = @()
for ($k = 0; $k -lt 4; $k++) {
    $d = Join-Path $OutDir "launch$k"
    $envs = @{ WFC_SMOKE_FRAMES = "100000000"; WFC_NOMOUSE = "1" }; if ($RenderData) { $envs.WFC_RENDER_DATA = $RenderData }
    $p = Start-Wfc $d $envs; $t0 = Get-Date; $peak = 0
    while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt 25) { Start-Sleep -Seconds 1; try { $p.Refresh(); $peak = [Math]::Max($peak, $p.PrivateMemorySize64 / 1MB) } catch { } }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2 }
    $mem += [Math]::Round($peak, 0)
}
$spread = ($mem | Measure-Object -Maximum).Maximum - ($mem | Measure-Object -Minimum).Minimum
Add-WfcResult $res "launch.memory_after_load" $(if ($spread -gt 200) { "HUMAN" } else { "PASS" }) $spread ("peak private MB over the first 25 s of 4 launches: {0} (spread {1} MB). In-process map reload: no product hook (not testable)" -f ($mem -join ", "), $spread) "" $null "MB"
Add-WfcResult $res "soak.not_testable" "INFO" $null "repeated in-process transforms beyond 2 per run and map reload need product hooks (proposal: WFC_AUTOTRANSFORM_EVERY=N); the transform stress matrix covers transforms across 756 processes"
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"SOAK: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
