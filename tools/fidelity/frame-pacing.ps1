# FRAME PACING / CAMERA JUDDER (tier TARGETED; M09 "hundreds of FPS but uneven camera").
# Per FPS limit (0 = uncapped, 60, 144, 240; WFC_FPS_LIMIT = the PC FrameLimit setting): one frontend-launched TDM match on
# Streets, scripted walk + strafe + turn for -Seconds, then from the log of the in-play frames:
#   frame time   WFC_PERFLOG=1: every rendered frame's wall time -> p50 / p95 / p99 / max, effective fps, limiter accuracy
#   stale camera WFC_CAMLOG: the pawn's projected screen position per rendered frame. Without render interpolation the
#                camera and pawn advance only on the 60 Hz simulation steps, so frames between steps repeat the tuple
#                EXACTLY while moving (translation only: walk + strafe, no scripted turn) -> "stale fraction" (expected ~ 1 - 60/fps without interpolation, ~0 with it).
#   cadence      lengths of identical runs (how many frames each simulation state is shown: uneven runs = judder)
#   PACING       Rendering's WFC_PACINGLOG lines when the build has them (agents/rendering 43bcb50+), reported alongside.
# GPU: a run of all four limits is ~4 min of renderer time; ask Integration first (milestone 09 GPU arbitration).
#
#   .\tools\fidelity\frame-pacing.ps1 -Root work\ab\<target> -OutDir <dir> [-Limits 0,60,144,240] [-Seconds 15] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Limits = @("0", "60", "144", "240"), [int]$Seconds = 15,
      [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$NoInterp, [switch]$ReportOnly)   # -NoInterp: WFC_NOINTERP A/B (09a+): render alpha forced to 1
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Limits = @($Limits | ForEach-Object { "$_" -split "," } | Where-Object { $_ -ne "" } | ForEach-Object { [int]$_ })   # "0,60" from bash arrives as one string
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "pacing.$id" $status $null $note $owner }
function Pct($v, $p) { $s = @($v | Sort-Object); if (-not $s.Count) { return $null }; return $s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))] }
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($lim in $Limits) {
    $d = Join-Path $OutDir "limit_$lim"; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "limit_$lim.gpu" "UNKNOWN" "GPU busy - not run (not a product result)" "Experimental"; continue }
        $s = @("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
               "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,508", "wait:t=1",
               "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=$Seconds", "quit") -join ";"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "300";
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_PERFLOG = "1"; WFC_CAMLOG = "1"; WFC_PACINGLOG = "600";
                WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_FPS_LIMIT = "$lim" }
        if ($NoInterp) { $e.WFC_NOINTERP = "1" }
        # NO scripted turn: WFC_AUTOTURN rotates the camera every RENDER frame, which changes the pawn's projection on every
        # frame regardless of the 60 Hz simulation and hides the tick-stepping this measures (2026-10-06 harness defect).
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        $null = Invoke-WfcExe $exe $d $e "run.log" 360
    }
    if (-not (Test-Path $lg)) { continue }
    # in-play window: lines after the last "to=InGame" ui.state (log order), excluding the first 2 s (spawn / warm-up)
    $lines = @([IO.File]::ReadLines($lg)); $start = -1
    for ($i = $lines.Count - 1; $i -ge 0; --$i) { if ($lines[$i] -match 'FLOW ui\.state .*to=InGame') { $start = $i; break } }
    if ($start -lt 0) { Res "limit_$lim" "UNKNOWN" "the match never reached InGame (script)" "Experimental"; continue }
    $ft = New-Object System.Collections.Generic.List[double]; $cam = New-Object System.Collections.Generic.List[string]; $pacing = @()
    for ($i = $start; $i -lt $lines.Count; ++$i) {
        $l = $lines[$i]
        if ($l -match 'PERF f\d+ frame=([\d.]+)ms') { $ft.Add([double]$Matches[1]) }
        elseif ($l -match 'CAMLOG \d+ (\S+ \S+ \S+)') { $cam.Add($Matches[1]) }
        elseif ($l -match '\] PACING ') { $pacing += ($l -replace '^.*\] PACING ', '') }
    }
    $skip = [Math]::Min($ft.Count, 240); $ftv = @($ft | Select-Object -Skip $skip); $camv = @($cam | Select-Object -Skip ([Math]::Min($cam.Count, 240)))
    $stale = 0; $runs = New-Object System.Collections.Generic.List[int]; $run = 1
    for ($i = 1; $i -lt $camv.Count; ++$i) { if ($camv[$i] -eq $camv[$i - 1]) { ++$stale; ++$run } else { $runs.Add($run); $run = 1 } }
    # WFC_CAMLOG reads Application::camera_, which is NOT the rendered camera in frontend-launched matches (depth 0, constant):
    # the camera metric comes from Rendering's WFC_PACINGLOG (presented frames, per-frame camera change) when the build has it
    $camlogValid = @($camv | Where-Object { [Math]::Abs([double](($_ -split ' ')[2])) -gt 0.01 }).Count -gt ($camv.Count / 2)
    $pu = 0; $pc = 0; $yaw = @()
    foreach ($pl in $pacing) { $mm = [regex]::Match($pl, 'camera unchanged on (\d+), changed on (\d+); yaw rate on change p10 ([\d.-]+) p50 ([\d.-]+) p90 ([\d.-]+)')
        if ($mm.Success) { $pu += [int]$mm.Groups[1].Value; $pc += [int]$mm.Groups[2].Value; $yaw += "p10 $($mm.Groups[3].Value) p50 $($mm.Groups[4].Value) p90 $($mm.Groups[5].Value)" } }
    $staleFrac = if ($pu + $pc -gt 0) { [Math]::Round($pu / ($pu + $pc), 3) } else { $null }   # CAMLOG is never used for the verdict (not the rendered camera in frontend-launched matches; scripted turn moves it every frame)
    $staleSrc = if ($pu + $pc -gt 0) { "PACINGLOG" } elseif ($camlogValid) { "CAMLOG" } else { "none" }
    $p50 = Pct $ftv 0.50; $p95 = Pct $ftv 0.95; $p99 = Pct $ftv 0.99; $mx = if ($ftv.Count) { ($ftv | Measure-Object -Maximum).Maximum } else { $null }
    $mean = if ($ftv.Count) { ($ftv | Measure-Object -Average).Average } else { $null }; $fps = if ($mean) { [Math]::Round(1000 / $mean, 0) } else { $null }
    $runSet = @($runs | Group-Object | Sort-Object { [int]$_.Name } | ForEach-Object { "$($_.Name)x$($_.Count)" })
    $expectStale = if ($fps -and $fps -gt 60) { [Math]::Round(1 - 60 / $fps, 2) } else { 0 }
    $row = [pscustomobject][ordered]@{ limit = $lim; frames = $ftv.Count; fps = $fps; p50_ms = $p50; p95_ms = $p95; p99_ms = $p99; max_ms = $mx; stale_camera_frac = $staleFrac; camera_source = $staleSrc
        no_interp_expect = $expectStale; frames_per_cam_state = (($runSet | Select-Object -First 6) -join " "); pacing_log = (($pacing | Select-Object -Last 1)) }
    $rows.Add($row)
    # limiter accuracy: the mean frame time should be the cap's period (+- 10 %)
    if ($lim -gt 0 -and $mean) { $want = 1000.0 / $lim
        Res "limit_$lim.limiter" $(if ([Math]::Abs($mean - $want) / $want -le 0.10) { "PASS" } else { "FAIL" }) ("FrameLimit {0}: mean frame {1:N2} ms (target {2:N2} ms, {3} fps measured); p99 {4} ms" -f $lim, $mean, $want, $fps, $p99) "Frontend/Rendering" }
    # presentation evenness: p99 within 2x the median (no hitches) at every limit
    Res "limit_$lim.even_presentation" $(if ($p50 -and $p99 -le [Math]::Max(2 * $p50, $p50 + 2)) { "PASS" } elseif ($p50) { "PARTIAL" } else { "UNKNOWN" }) ("frame time p50 {0} / p95 {1} / p99 {2} / max {3} ms over {4} frames" -f $p50, $p95, $p99, $mx, $ftv.Count) "Rendering"
    # camera judder: with render interpolation every rendered frame shows a new camera; without it ~(1 - 60/fps) repeat
    if ($staleFrac -eq $null) { Res "limit_$lim.camera_updates_every_frame" "UNKNOWN" "no valid camera measurement: this build has no WFC_PACINGLOG (Rendering 43bcb50+) and WFC_CAMLOG is not the rendered camera in frontend-launched matches" "Experimental" }
    else {
        Res "limit_$lim.camera_updates_every_frame" $(if ($fps -le 62) { "INFO" } elseif ($staleFrac -le 0.05) { "PASS" } else { "FAIL" }) ("stale camera on {0:P1} of presented frames at {1} fps (source {4}; no-interpolation expectation {2:P0}); {3}" -f $staleFrac, $fps, $expectStale, $(if ($yaw.Count) { "yaw rate on change " + $yaw[-1] } else { "frames per camera state " + (($runSet | Select-Object -First 6) -join " ") }), $staleSrc) "Gameplay/Rendering"
    }
}
Write-WfcCsv $rows (Join-Path $OutDir "pacing.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("limit", "fps", "p50_ms", "p95_ms", "p99_ms", "max_ms", "stale_camera_frac", "no_interp_expect", "frames_per_cam_state") (Join-Path $OutDir "PACING.md") "Frame pacing / camera judder" @("build: ``$sha`` ($Config)", "", "Frontend-launched TDM on Streets, walk + strafe (no turn), $Seconds s per FPS limit (0 = uncapped). stale_camera_frac = rendered frames whose camera / pawn projection is identical to the previous frame while moving (no render interpolation -> ~1 - 60/fps). Logging every frame (PERFLOG / CAMLOG) costs some throughput itself.")
"FRAME PACING: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
