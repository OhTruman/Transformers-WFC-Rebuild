# Sustained-fire / scenario performance attribution for the REAL game loop.
#
#   .\tools\fidelity\perf-profile.ps1                      # all scenarios -> work\fidelity\perf\
#   .\tools\fidelity\perf-profile.ps1 -Scenarios held_fire,idle
#
# Needs the opt-in measurement build (cmake -DWFC_BUILD_MEASURE=ON; target wfc_rebuild_prof): the
# unmodified product sources + tools/fidelity/measure/Profiler.cpp (1 kHz main-thread stack
# sampler). Per scenario it records:
#   frames.csv   one row per rendered frame (wall-clock timestamp of the flushed frame log line):
#                dt, ammo, reloading, fine aim, speed, live particles / mesh particles / impacts
#   samples.txt  main-thread call stacks (profiler)
# then perf-report.ps1 symbolizes and attributes cost by category and by origin.
# Scenario input comes only from the product's own WFC_* test hooks; nothing is patched.
param(
    [string[]]$Scenarios = @("idle", "walk", "fineaim", "burst", "held_fire"),
    [string]$Exe = "",
    [string]$OutDir = ""
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_prof.exe" }
if (-not (Test-Path $Exe)) { throw "measurement build missing: $Exe (configure with -DWFC_BUILD_MEASURE=ON)" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\perf" }

# frames = rendered frames to run (the sim is real-time; held fire runs ~60 ms/frame)
$defs = [ordered]@{
    idle      = @{ frames = 900;  env = @{} }
    walk      = @{ frames = 900;  env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5" } }
    fineaim   = @{ frames = 900;  env = @{ WFC_FINEAIM_ON = "30" } }
    burst     = @{ frames = 900;  env = @{ WFC_AUTORELOAD = "1" } }              # fire frames 1-12, then stop
    held_fire = @{ frames = 420;  env = @{ WFC_AUTOFIRE = "1" } }                # trigger held the whole run
}

foreach ($name in $Scenarios) {
    $d = $defs[$name]
    if (-not $d) { throw "unknown scenario $name" }
    $dir = Join-Path $OutDir $name
    New-Item -ItemType Directory -Force $dir | Out-Null
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $Exe
    $psi.WorkingDirectory = $dir
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $envs = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOGEVERY = "1"; WFC_ANIMLOG = "1"; WFC_PROF = (Join-Path $dir "samples.txt") } + $d.env
    foreach ($k in $envs.Keys) { $psi.EnvironmentVariables[$k] = [string]$envs[$k] }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $p = [System.Diagnostics.Process]::Start($psi)
    $rows = New-Object System.Collections.Generic.List[string]
    $rows.Add("frame,t,dt,ammo,reserve,reloading,fineaim,hspeed,particles,meshes,impacts,fov")
    $log = New-Object System.Collections.Generic.List[string]
    $pending = $null; $fx = "0,0,0"; $prevT = $null
    while (($line = $p.StandardOutput.ReadLine()) -ne $null) {
        $t = $sw.Elapsed.TotalSeconds
        $log.Add(("{0:F4} {1}" -f $t, $line))
        if ($line -match "FX particles=(\d+) meshes=(\d+) impacts=(\d+)") { $fx = "$($Matches[1]),$($Matches[2]),$($Matches[3])"; continue }
        if ($line -match "\] frame (\d+) .* ammo=(\d+)/(\d+) reloading=(\d) .* hspeed=([\d.]+) .* fineAim=(\d) fov=([\d.]+)") {
            $dt = if ($null -ne $prevT) { $t - $prevT } else { 0 }
            $prevT = $t
            # The FX counter line of a frame follows its frame line, so a row is emitted one line later.
            if ($pending) { $rows.Add(("{0},{1},{2}" -f $pending.head, $fx, $pending.fov)) }
            $pending = @{ head = ("{0},{1:F4},{2:F4},{3},{4},{5},{6},{7}" -f $Matches[1], $t, $dt, $Matches[2], $Matches[3], $Matches[4], $Matches[6], $Matches[5]); fov = $Matches[7] }
        }
    }
    $p.WaitForExit()
    if ($pending) { $rows.Add(("{0},{1},{2}" -f $pending.head, $fx, $pending.fov)) }
    $rows | Set-Content (Join-Path $dir "frames.csv")
    $log | Set-Content (Join-Path $dir "run.log")
    Write-Host ("{0}: exit {1}, {2} frames, {3:F1} s" -f $name, $p.ExitCode, ($rows.Count - 1), $sw.Elapsed.TotalSeconds)
}
