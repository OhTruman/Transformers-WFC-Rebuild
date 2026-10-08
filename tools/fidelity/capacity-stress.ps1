# CAPACITY STRESS (tier TARGETED, PERF WINDOW REQUIRED; Milestone E extended capacity, PC ADAPTATION). Per map and population:
# ONE process, a frontend-launched private match with the bot counts set through the lobby URL (Frontend WFC_LOBBY_OPTIONS:
# ?ExtendedPlayers / ?BotsAutobot / ?BotsDecepticon / ?BotsEnemy, Gameplay's per-faction options), a short TimeLimit so the
# match ENDS on time, back to the lobby, and a SECOND match (re-selected map) -> quit. Populations (participants incl. human):
#   orig10    original 5 v 5 (TDM, 4 + 5 bots)              p16v16   16 v 16 (TDM extended, 15 + 16 bots)
#   p32v32    32 v 32 (TDM extended, 31 + 32 bots)          ffa64    FFA 64 (DM extended, 63 bots)
# Per match (in-play frames only, after the first 3 s): frame time p50 / p95 / p99 / max (WFC_PERFLOG=1 per frame) and
# frames over 33 / 50 ms, sim ms (PERF), privateMB at load and after unload, spawned participants vs requested, kills,
# audio voices / dropped / stolen / mix ms (WFC_AMBLOG), bots broken / struggling / no-path / off-mesh (WFC_BOTLOG), the
# bot AI cost when the build logs it (Gameplay "BOTPERF"), match end reason, clean return + second match.
# Verdict per population vs orig10 on the same map: the frame-time tax (p50 / p99 ratios) and memory delta, plus hard
# failures (spawn short, broken bots, no end, no second match, crash).
# Announce "PERF RUN experimental ~N min" to ALL lanes first; nothing else may run.
#
# 300 FPS TARGET (user, 2026-10-07: "300+ fps in big bot lobbies"): uncapped (VSync 0, FrameLimit 0), per -Resolutions;
# reports p50 / p90 / p99 and the % of frames under 3.33 ms; TARGET MET when p90 <= 3.33 ms. The CPU / GPU split comes from a
# SEPARATE one-match run with WFC_RENDERSTATS (it glFinish-es every frame, which would distort the timing run): "scene
# submit" = CPU render submission ms, "gpu wait" = time the CPU then waits for the GPU (GPU-bound remainder).
#
#   .\tools\fidelity\capacity-stress.ps1 -Root work\ab\<target> -OutDir <dir> [-Maps 508,507,509] [-Pops p32v32,ffa64] [-Resolutions 1920x1080,2560x1440] [-TimeLimit 60] [-NoSplit] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @("508", "507"),
      [string[]]$Pops = @("orig10", "p16v16", "p32v32", "ffa64"), [int]$TimeLimit = 75, [int]$Difficulty = 1,
      [string[]]$Resolutions = @("1280x720"), [switch]$NoSplit, [switch]$FixedCam, [hashtable]$CamByMap = @{}, [ValidateSet("overview", "legacy")][string]$CamSet = "overview",
      # -PlayerBot <0..2>: REAL-PLAY row (Integration 2026-10-07) - the local player is driven by the bot brain through its input
      # (Gameplay's WFC_PLAYERBOT) with the normal follow camera / HUD; use WITHOUT -FixedCam. Builds without the hook fall back to
      # scripted input (walk / strafe / jump / turn / fire) and the row says "scripted (approximation)".
      [int]$PlayerBot = -1,
      # -AsyncModes "0,1": every row with WFC_ASYNCSTEP=0 and =1 (Gameplay's async sim step); =1 rows also log WFC_ASYNCLOG
      # (local part / background part / join wait), reported per row
      [string[]]$AsyncModes = @(),
      # -PerfLog <md>: append this run's table to the running PERFORMANCE LOG (Integration's scalability brief)
      [string]$PerfLog = "", [string]$Note = "",
      # -Exe: an exe outside <Root>\build-release (e.g. a lane's profiling build, read-only); -ExtraEnv "K=V;K=V" for the timing
      # runs (e.g. WFC_SLOWFRAME=3.33: Rendering's per-slow-frame breakdown, classified per row below)
      [string]$Exe = "", [string]$ExtraEnv = "",
      [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = if ($Exe) { (Resolve-Path $Exe).Path } else { Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" }) }
$extraEnvMap = @{}; foreach ($kv in @($ExtraEnv -split ';' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $extraEnvMap[$kv.Substring(0, $i).Trim()] = $kv.Substring($i + 1) }
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$Maps = @($Maps | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$AsyncModes = @($AsyncModes | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
if (-not $AsyncModes.Count) { $AsyncModes = @("") }
$Resolutions = @($Resolutions | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
# -FixedCam: Rendering's WFC_FIXEDCAM="x,y,z,yawDeg,pitchDeg" holds every match frame at one view, so runs are comparable
# (frame cost follows what the camera sees). Defaults: Streets overview from above team 0's spawn into team 1's (Rendering).
# Debris / Molten: derived the same way (16 m above team 0's spawn, 20 m behind it, yaw = atan2(-dx, -dz) toward team 1's spawn;
# the method reproduces Rendering's Streets view within 3 deg). Spawns: Debris from MATCH spawn logs, Molten from spawnpoints.json.
# Fixed cams. "legacy" = the cams of the earlier PERFORMANCE_LOG rows (2026-10-07: the Streets one sits ~7 UU above ground at the
# edge of the map and sees a near-black wall - kept only for continuity). "overview" = verified by cam-sweep.ps1 (a lit view across
# the arena centre, checked by eye on the contact sheet); maps without a verified overview fall back to legacy and say so.
$camLegacy = @{ "508" = "100,-700,-680,-141.6,-12"; "507" = "247.1,148.8,-63.4,91.4,-12"; "509" = "-25.2,18.4,-100.6,-103.5,-12" }
$camOverview = @{ "508" = "44.3,-606.9,-475.7,-90.0,-31.3" }
$camDefaults = @{}; foreach ($k in $camLegacy.Keys) { $camDefaults[$k] = $(if ($CamSet -eq "overview" -and $camOverview.ContainsKey($k)) { $camOverview[$k] } else { $camLegacy[$k] }) }
foreach ($k in $CamByMap.Keys) { $camDefaults["$k"] = $CamByMap[$k] }
if ($FixedCam -and -not $H.Contains("WFC_FIXEDCAM")) { Write-Warning "build has no WFC_FIXEDCAM - runs use the scripted player's camera" }
$Pops = @($Pops | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "capacity.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_LOBBY_OPTIONS")) { Res "hook" "UNKNOWN" "build has no WFC_LOBBY_OPTIONS (Frontend 2cf1ab1): bot counts cannot be set per faction" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$popDef = @{
    orig10 = @{ mode = "TDM"; want = 10; opts = "ExtendedPlayers=0;BotsAutobot=5;BotsDecepticon=5" }
    p16v16 = @{ mode = "TDM"; want = 32; opts = "ExtendedPlayers=1;BotsAutobot=16;BotsDecepticon=16" }
    p32v32 = @{ mode = "TDM"; want = 64; opts = "ExtendedPlayers=1;BotsAutobot=32;BotsDecepticon=32" }
    ffa64  = @{ mode = "DM";  want = 64; opts = "ExtendedPlayers=1;BotsEnemy=63" }
}
# "rec": the map's RECOMMENDED Extended size (AssetTools, Integration 2026-10-07) - the default the user will play. N per side
# including the human: the human's side gets N - 1 bots, the other side N (human-relative ?BotsFriendly / ?BotsEnemy; the run
# profile has no faction keys, so these apply).
$recPerSide = @{ "502" = 14; "504" = 12; "510" = 11; "501" = 11; "508" = 10; "507" = 10; "509" = 9; "503" = 9 }
# "recffa": the map's recommended FFA TOTAL incl. the human (AssetTools recommended_players.json) -> BotsEnemy = total - 1
$recFfa = @{ "502" = 21; "504" = 18; "510" = 16; "501" = 16; "508" = 15; "507" = 15; "509" = 14; "503" = 14 }
function Pct($v, $p) { if (-not $v.Count) { return $null }; $s = @($v | Sort-Object); return [Math]::Round($s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))], 2) }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($asyncM in $AsyncModes) {
foreach ($resol in $Resolutions) { $rw = [int]($resol -split 'x')[0]; $rh = [int]($resol -split 'x')[1]   # NOT $res (the results collection)
foreach ($map in $Maps) { foreach ($pop in $Pops) {
    $P = $popDef[$pop]
    if ($pop -eq "rec") { $n = $recPerSide["$map"]; if (-not $n) { continue }
        $P = @{ mode = "TDM"; want = 2 * $n; opts = "ExtendedPlayers=1;BotsFriendly=$($n - 1);BotsEnemy=$n" } }
    if ($pop -match '^t(\d+)$') { $n = [int]$Matches[1]; $half = [int]($n / 2)
        $P = @{ mode = "TDM"; want = $n; opts = "ExtendedPlayers=$(if ($n -gt 10) { 1 } else { 0 });BotsFriendly=$($half - 1);BotsEnemy=$($n - $half)" } }
    if ($pop -eq "recffa") { $n = $recFfa["$map"]; if (-not $n) { continue }
        $P = @{ mode = "DM"; want = $n; opts = "ExtendedPlayers=1;BotsEnemy=$($n - 1)" } }
    if (-not $P) { continue }
    $tag = "{0}_{1}_{2}{3}" -f $map, $pop, $resol, $(if ($asyncM -ne "") { "_as$asyncM" } else { "" }); $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"; $fl = Join-Path $d "flow.jsonl"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$tag.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        (Get-BotProfile 0 0 $Difficulty -Width $rw -Height $rh) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $party = if ($P.mode -eq "DM") { "GTS_FreeForAllGame" } else { "GTS_TeamGame" }
        $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
        $one = "call:Online.SetSelectedMapID,$map;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:ui=GameEnded;wait:t=4;wait:level=GameLobby;wait:ui=InLobby;wait:t=3"
        $s = (@((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,$party", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
                "call:Online.EditGameMode,$($P.mode)", "call:Online.PlayPrivateGame,$($P.mode)", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", $one, $one, "quit")) -join ";"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = $fl; WFC_FLOW_TIMEOUT = "$(2 * $TimeLimit + 600)";
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_PERFLOG = "1"; WFC_AMBLOG = "1"; WFC_BOTLOG = "all"; WFC_BOTPERF = "5"
                WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_AUTOJUMP_EVERY = "150"; WFC_LOBBY_OPTIONS = "$($P.opts);PointsToWin=9999;TimeLimit=$TimeLimit" }
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        if ($H.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "1" }   # GameFlow RNG (team pick, rotation, tips) is clock-seeded otherwise (Frontend 2026-10-07)
        if ($PlayerBot -ge 0) {
            if ($H.Contains("WFC_PLAYERBOT")) { foreach ($k in "WFC_AUTOWALK", "WFC_AUTOSTRAFE", "WFC_AUTOJUMP_EVERY") { $e.Remove($k) }; $e.WFC_PLAYERBOT = "$PlayerBot"; $e.WFC_PLAYERBOTLOG = "1" }
            else { $e.WFC_AUTOTURN = "0.6"; $e.WFC_AUTOFIRE = "1" } }
        if ($FixedCam -and $H.Contains("WFC_FIXEDCAM") -and $camDefaults["$map"]) { $e.WFC_FIXEDCAM = $camDefaults["$map"]
            if ($H.Contains("WFC_SHOTMATCH")) { $e.WFC_SHOTMATCH = "$d,600,600,1" } }   # one capture of the measured view per match
        if ($asyncM -ne "") { $e.WFC_ASYNCSTEP = $asyncM; if ($asyncM -eq "1") { $e.WFC_ASYNCLOG = "1" } }
        foreach ($k in $extraEnvMap.Keys) { $e[$k] = $extraEnvMap[$k] }
        $null = Invoke-WfcExe $exe $d $e "run.log" (2 * $TimeLimit + 900)
    }
    # CPU / GPU split: a separate ONE-match run with WFC_RENDERSTATS (glFinish per frame distorts timing, so never in the timing run)
    $ds = Join-Path $OutDir "${tag}_split"; $lgs = Join-Path $ds "wfc.log"
    if (-not $NoSplit -and -not $ReportOnly -and -not (Test-Path $lgs)) {
        New-Item -ItemType Directory -Force $ds | Out-Null
        if (Wait-WfcGpu) {
            (Get-BotProfile 0 0 $Difficulty -Width $rw -Height $rh) | Set-Content -Encoding ASCII (Join-Path $ds "wfc_profile.ini")
            $party = if ($P.mode -eq "DM") { "GTS_FreeForAllGame" } else { "GTS_TeamGame" }
            $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
            $s1 = (@((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,$party", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
                     "call:Online.EditGameMode,$($P.mode)", "call:Online.PlayPrivateGame,$($P.mode)", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5",
                     "call:Online.SetSelectedMapID,$map", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=40", "quit")) -join ";"
            $e2 = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s1; WFC_FLOWLOG = (Join-Path $ds "flow.jsonl"); WFC_FLOW_TIMEOUT = "600"
                     WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_RENDERSTATS = "1"; WFC_TICKPROF = "1"; WFC_DRAWPROF = "1"; WFC_FRAMEPROF = "1"; WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_AUTOJUMP_EVERY = "150"
                     WFC_LOBBY_OPTIONS = "$($P.opts);PointsToWin=9999;TimeLimit=600" }
            if ($H.Contains("WFC_CHARSELECT")) { $e2.WFC_CHARSELECT = "1" }
            if ($FixedCam -and $H.Contains("WFC_FIXEDCAM") -and $camDefaults["$map"]) { $e2.WFC_FIXEDCAM = $camDefaults["$map"] }
            if ($asyncM -ne "") { $e2.WFC_ASYNCSTEP = $asyncM }
            $null = Invoke-WfcExe $exe $ds $e2 "run.log" 700
        }
    }
    $split = @(if (Test-Path $lgs) { $inG = $false; foreach ($sl in [IO.File]::ReadLines($lgs)) { if ($sl.Contains('to=InGame')) { $inG = $true; continue }
        if ($inG) { $sm = [regex]::Match($sl, 'wfc: avg frame ([\d.]+) ms \(\d+ fps\); scene submit ([\d.]+) ms, gpu wait ([\d.]+) ms'); if ($sm.Success) { [pscustomobject]@{ frame = [double]$sm.Groups[1].Value; submit = [double]$sm.Groups[2].Value; gpu = [double]$sm.Groups[3].Value } } } } })
    # per-phase split (Integration 2026-10-07: always name the biggest remaining item)
    $ph = @{ simStep = @(); simTop = @{}; chars = @(); fx = @(); actors = @(); weapons = @(); hud = @(); present = @() }
    if (Test-Path $lgs) { $inG = $false; foreach ($sl in [IO.File]::ReadLines($lgs)) { if ($sl.Contains('to=InGame')) { $inG = $true; continue }; if (-not $inG) { continue }
        $m1 = [regex]::Match($sl, 'TICKPROF ms/step \(\d+ participants\):(.*)$')
        if ($m1.Success) { $toks = $m1.Groups[1].Value.Trim() -split '\s+'; for ($i = 0; $i + 1 -lt $toks.Count; $i += 2) { $nm = $toks[$i]; $v = [double]$toks[$i + 1]
                if ($nm -eq "TOTAL") { $ph.simStep += $v } elseif ($nm -notmatch '^(ab\.|bot\.)') { $ph.simTop[$nm] = [double]$ph.simTop[$nm] + $v } }; continue }
        $m2 = [regex]::Match($sl, 'wfc: dynamic meshes: .* total ([\d.]+) ms'); if ($m2.Success) { $ph.chars += [double]$m2.Groups[1].Value; continue }
        $m3 = [regex]::Match($sl, 'wfc: map FX per frame: ([\d.]+) ms'); if ($m3.Success) { $ph.fx += [double]$m3.Groups[1].Value; continue }
        $m4 = [regex]::Match($sl, 'DRAWPROF ms/frame .*: actors ([\d.]+), participant weapons ([\d.]+)'); if ($m4.Success) { $ph.actors += [double]$m4.Groups[1].Value; $ph.weapons += [double]$m4.Groups[2].Value; continue }
        $m5 = [regex]::Match($sl, 'frame\.hitch ms=[\d.]+(.*?) level='); if ($m5.Success) { $u = [regex]::Match($m5.Groups[1].Value, ' ui\.draw=([\d.]+)'); $pr = [regex]::Match($m5.Groups[1].Value, ' present=([\d.]+)')
            if ($u.Success) { $ph.hud += [double]$u.Groups[1].Value }; if ($pr.Success) { $ph.present += [double]$pr.Groups[1].Value } }
    } }
    function Avg($a) { if ($a.Count) { [Math]::Round(($a | Measure-Object -Average).Average, 2) } else { $null } }
    $nTick = [Math]::Max(1, $ph.simStep.Count)
    $simTop3 = (@($ph.simTop.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 3 | ForEach-Object { "{0} {1:N2}" -f $_.Key, ($_.Value / $nTick) }) -join ", ")
    $phase = [ordered]@{ sim_step_ms = (Avg $ph.simStep); chars_ms = (Avg $ph.chars); fx_ms = (Avg $ph.fx); actors_ms = (Avg $ph.actors); weapons_ms = (Avg $ph.weapons); hud_ms = (Avg $ph.hud); present_ms = (Avg $ph.present) }
    $splitTxt = if ($split.Count) { "split (RENDERSTATS run, glFinish-serialised): frame {0:N2} ms = scene submit (CPU) {1:N2} ms + gpu wait {2:N2} ms over {3} samples" -f ($split | Measure-Object frame -Average).Average, ($split | Measure-Object submit -Average).Average, ($split | Measure-Object gpu -Average).Average, $split.Count } else { "split: not measured" }
    if (-not (Test-Path $lg)) { continue }
    $lines = @([IO.File]::ReadLines($lg)); $clean = [bool]($lines | Where-Object { $_ -match 'Shutdown complete' } | Select-Object -First 1)
    $seg = @(); $cur = $null
    foreach ($l in $lines) { if ($l.Contains('] MATCH init ')) { if ($null -ne $cur) { $seg += , $cur }; $cur = New-Object System.Collections.Generic.List[string] }; if ($null -ne $cur) { $cur.Add($l) } }
    if ($null -ne $cur) { $seg += , $cur }
    $F = Read-FlowLog $fl; $ld = @(Flow-Ev $F "match.loaded"); $ul = @(Flow-Ev $F "match.unloaded")
    for ($k = 0; $k -lt 2; $k++) {
        $mt = "$tag.m$($k + 1)"
        if ($k -ge $seg.Count) { Res $mt "FAIL" ("match {0} of 2 never started" -f ($k + 1)) "Gameplay/Frontend"; continue }
        $segL = $seg[$k]   # NOT $L: PowerShell names are case-insensitive ($l is the line variable)
        # in-play frames: after the local InGame state, skipping the first 3 s (~ by time stamps is unavailable; skip 180 frames)
        $inPlay = $false; $ft = New-Object System.Collections.Generic.List[double]; $sim = New-Object System.Collections.Generic.List[double]; $stp = New-Object System.Collections.Generic.List[double]
        $voices = 0; $dropped = 0; $stolen = 0; $mixMs = 0.0; $ai = @(); $bl = @()
        foreach ($l in $segL) {
            if (-not $inPlay -and $l.Contains('to=InGame')) { $inPlay = $true; continue }
            if ($inPlay -and ($l.Contains('to=GameEnded') -or $l.Contains('] MATCH end '))) { $inPlay = $false }   # InProgress only: the UI enters InGame after the countdown (MATCH start); stop at MATCH end, before the results screen
            if (-not $inPlay) { continue }
            $m = [regex]::Match($l, 'PERF f\d+ frame=([\d.]+)ms sim=([\d.]+)ms(?: \(max [\d.]+\) steps/frame ([\d.]+))?'); if ($m.Success) { $ft.Add([double]$m.Groups[1].Value); $sim.Add([double]$m.Groups[2].Value); $stp.Add($(if ($m.Groups[3].Success) { [double]$m.Groups[3].Value } else { -1.0 })); continue }
            $m = [regex]::Match($l, 'AMB .*voices=(\d+) \(max (\d+), dropped (\d+), stolen (\d+).*mix=([\d.]+)ms'); if ($m.Success) { $voices = [Math]::Max($voices, [int]$m.Groups[2].Value); $dropped = [int]$m.Groups[3].Value; $stolen = [int]$m.Groups[4].Value; $mixMs = [Math]::Max($mixMs, [double]$m.Groups[5].Value); continue }
            $m = [regex]::Match($l, 'BOTPERF .*AI ([\d.]+) ms avg ([\d.]+)'); if ($m.Success) { $ai += [pscustomobject]@{ avg = [double]$m.Groups[1].Value; max = [double]$m.Groups[2].Value }; continue }
            $m = [regex]::Match($l, 'BOTLOG \S+ p(\d+) \(([-\d.]+) [-\d.]+ ([-\d.]+)\) cell (-?\d+) \S+ goal (\S+) .* tgt (-?\d+) .* stuck (\d+) .* nopath (\d+)')
            if ($m.Success) { $bl += [pscustomobject]@{ p = [int]$m.Groups[1].Value; x = [double]$m.Groups[2].Value; z = [double]$m.Groups[3].Value; cell = [int]$m.Groups[4].Value; goal = $m.Groups[5].Value; tgt = [int]$m.Groups[6].Value; stuck = [int]$m.Groups[7].Value; nopath = [int]$m.Groups[8].Value } }
        }
        $skipN = [Math]::Min($ft.Count, 180)
        $warm = @($ft | Select-Object -First $skipN)   # first 180 in-play frames: one-time warm-up (first uses, uploads)
        $ftv = @($ft | Select-Object -Skip $skipN); $simv = @($sim | Select-Object -Skip $skipN); $stpv = @($stp | Select-Object -Skip $skipN)
        # steady vs hitch: frames > 50 ms are hitch EVENTS (reported separately); the steady stats exclude them
        $hitch = @($ftv | Where-Object { $_ -gt 50 }); $steady = @($ftv | Where-Object { $_ -le 50 })
        $sorted = @($steady | Sort-Object -Descending)
        function LowFps($frac) { if (-not $sorted.Count) { return $null }; $n = [Math]::Max(1, [int][Math]::Ceiling($sorted.Count * $frac)); $avgMs = ($sorted | Select-Object -First $n | Measure-Object -Average).Average; return [Math]::Round(1000.0 / $avgMs, 1) }
        # slow frames (> 3.33 ms): how many contain a sim step (steps/frame >= 1) - the step-in-frame share of the 300 fps misses
        $slowIdx = @(for ($i = 0; $i -lt $ftv.Count; $i++) { if ($ftv[$i] -gt 3.333) { $i } })
        $slowWithStep = @($slowIdx | Where-Object { $_ -lt $stpv.Count -and $stpv[$_] -ge 1 }).Count
        $fastWithStep = @(for ($i = 0; $i -lt $ftv.Count; $i++) { if ($ftv[$i] -le 3.333 -and $i -lt $stpv.Count -and $stpv[$i] -ge 1) { 1 } }).Count
        $players = @($segL | Where-Object { $_ -match '\] MATCH spawn player=(\d+)' } | ForEach-Object { [regex]::Match($_, 'player=(\d+)').Groups[1].Value } | Select-Object -Unique).Count
        $endL = @($segL | Where-Object { $_ -match '\] MATCH end ' })[0]; $reason = if ($endL) { [regex]::Match($endL, 'reason=(\S+)').Groups[1].Value } else { "" }; if ($endL -and -not $reason) { $reason = "Time" }   # an EMPTY reason is the original's time-limit path (EndGame(none, ""), Gameplay 2026-10-07; printed as Time from their next push)
        $kills = @($segL | Where-Object { $_ -match '\] MATCH kill ' }).Count
        $broken = 0; $strug = 0
        foreach ($g in @($bl | Group-Object p)) { $gg = @($g.Group); $run = 0; $fz = 0
            for ($i = 1; $i -lt $gg.Count; $i++) { $mv = [Math]::Sqrt([Math]::Pow($gg[$i].x - $gg[$i - 1].x, 2) + [Math]::Pow($gg[$i].z - $gg[$i - 1].z, 2))
                if ($mv -lt 0.1 -and $gg[$i].tgt -lt 0 -and $gg[$i].goal -match '^(Roam|Attack|Retrieve)') { $run++; $fz = [Math]::Max($fz, $run) } else { $run = 0 } }
            if ($fz -ge 20) { $broken++ } elseif (@($gg | Where-Object { $_.stuck -ge 2 }).Count / [Math]::Max(1, $gg.Count) -gt 0.25) { $strug++ } }
        # Rendering's WFC_SLOWFRAME lines: "SLOWFRAME f<n> interval <ms>: render <ms> (world, chars, fx, transl, post), outside <ms>;
        # gpu <ms>|n/a (...); draws <n> (dyn, fx), program binds <n>, buffer upload <KB>, new textures <n>, shader compiles <n>, map FX cpu <ms> (sim <ms>)"
        $sfBins = [ordered]@{ "shader compile" = 0; "new textures" = 0; "buffer upload spike > 2 MB" = 0; "GPU-bound" = 0; "outside render (sim / UI / present)" = 0; "render: world" = 0; "render: chars" = 0; "render: fx" = 0; "render: transl" = 0; "render: post" = 0 }
        $sfN = 0
        # in-play SLOWFRAME lines only (the renderer's frame numbers also count front-end frames)
        $sfIn = $false; $sfLines = New-Object System.Collections.Generic.List[string]
        foreach ($sl in $segL) { if ($sl.Contains('to=InGame')) { $sfIn = $true; continue }; if ($sl.Contains('to=GameEnded')) { $sfIn = $false }; if ($sfIn -and $sl.Contains('SLOWFRAME f')) { $sfLines.Add($sl) } }
        $sfRe = 'interval ([\d.]+) ms: render ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)\), outside ([-\d.]+); (?:gpu ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)\)|gpu n/a); draws (\d+) \(dyn (\d+), fx (\d+)\), program binds (\d+), buffer upload (\d+) KB, new textures (\d+), shader compiles (\d+), map FX cpu ([\d.]+) \(sim ([\d.]+)\)'
        $sfSum = @{ interval = 0.0; render = 0.0; world = 0.0; chars = 0.0; fx = 0.0; transl = 0.0; outside = 0.0; gpu = 0.0; gpuN = 0; draws = 0.0; upKB = 0.0 }
        foreach ($sl in $sfLines) {
            $mm = [regex]::Match($sl, $sfRe); if (-not $mm.Success) { continue }; $sfN++
            $g = $mm.Groups; $rnd = [double]$g[2].Value; $parts = @(3..7 | ForEach-Object { [double]$g[$_].Value }); $outside = [double]$g[8].Value
            $gpu = if ($g[9].Success) { [double]$g[9].Value } else { -1.0 }
            $sfSum.interval += [double]$g[1].Value; $sfSum.render += $rnd; $sfSum.world += $parts[0]; $sfSum.chars += $parts[1]; $sfSum.fx += $parts[2]; $sfSum.transl += $parts[3]; $sfSum.outside += $outside
            if ($gpu -ge 0) { $sfSum.gpu += $gpu; $sfSum.gpuN++ }; $sfSum.draws += [double]$g[15].Value; $sfSum.upKB += [double]$g[19].Value
            if ([int]$g[21].Value -gt 0) { $sfBins["shader compile"]++ }
            elseif ([int]$g[20].Value -gt 0) { $sfBins["new textures"]++ }
            elseif ([double]$g[19].Value -gt 2048) { $sfBins["buffer upload spike > 2 MB"]++ }   # steady frames upload ~1.5 MB (Rendering): only the tail is a cause
            elseif ($gpu -gt $rnd) { $sfBins["GPU-bound"]++ }
            elseif ($outside -gt $rnd) { $sfBins["outside render (sim / UI / present)"]++ }
            else { $names = @("world", "chars", "fx", "transl", "post"); $mi = 0; for ($q = 1; $q -lt 5; $q++) { if ($parts[$q] -gt $parts[$mi]) { $mi = $q } }; $sfBins["render: $($names[$mi])"]++ }
        }
        $sfAvg = if ($sfN) { "avg slow frame {0:N2} ms = render {1:N2} (world {2:N2}, chars {3:N2}, fx {4:N2}, transl {5:N2}) + outside {6:N2}; gpu {7}; draws {8:N0}; buffer upload {9:N0} KB" -f ($sfSum.interval / $sfN), ($sfSum.render / $sfN), ($sfSum.world / $sfN), ($sfSum.chars / $sfN), ($sfSum.fx / $sfN), ($sfSum.transl / $sfN), ($sfSum.outside / $sfN), $(if ($sfSum.gpuN) { "{0:N2} ms" -f ($sfSum.gpu / $sfSum.gpuN) } else { "n/a" }), ($sfSum.draws / $sfN), ($sfSum.upKB / $sfN) } else { "" }
        $asyncLines = @($segL | Where-Object { $_ -match '\] ASYNC(STEP|LOG) ' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\s*', '' })
        $joinVals = @($asyncLines | ForEach-Object { $mj = [regex]::Match($_, 'join(?: wait)?[ =:]+([\d.]+)'); if ($mj.Success) { [double]$mj.Groups[1].Value } })
        $bgVals = @($asyncLines | ForEach-Object { $mb = [regex]::Match($_, 'background(?: part)?[ =:]+([\d.]+)'); if ($mb.Success) { [double]$mb.Groups[1].Value } })
        $locVals = @($asyncLines | ForEach-Object { $ml = [regex]::Match($_, 'local(?: part)?[ =:]+([\d.]+)'); if ($ml.Success) { [double]$ml.Groups[1].Value } })
        $row = [pscustomobject][ordered]@{ async = $asyncM; res = $resol; map = $map; pop = $pop; match = $k + 1; spawned = "$players/$($P.want)"; frames = $ftv.Count
            commit = $sha.Substring(0, [Math]::Min(7, $sha.Length)); scenario = $P.mode; participants = $P.want; cap = "uncapped"
            avg_ms = $(if ($steady.Count) { [Math]::Round(($steady | Measure-Object -Average).Average, 2) }); avg_fps = $(if ($steady.Count) { [Math]::Round(1000.0 / ($steady | Measure-Object -Average).Average, 1) })
            low1_fps = (LowFps 0.01); low01_fps = (LowFps 0.001); worst_steady_ms = $(if ($steady.Count) { [Math]::Round(($steady | Measure-Object -Maximum).Maximum, 2) })
            hitches = $hitch.Count; hitch_max_ms = $(if ($hitch.Count) { [Math]::Round(($hitch | Measure-Object -Maximum).Maximum, 1) }); warmup_hitches = @($warm | Where-Object { $_ -gt 50 }).Count
            slow_frames = $slowIdx.Count; slow_with_step_pct = $(if ($slowIdx.Count) { [Math]::Round(100.0 * $slowWithStep / $slowIdx.Count, 1) }); fast_with_step_pct = $(if ($ftv.Count - $slowIdx.Count) { [Math]::Round(100.0 * $fastWithStep / ($ftv.Count - $slowIdx.Count), 1) })
            p50_ms = (Pct $ftv 0.50); p90_ms = (Pct $ftv 0.90); p95_ms = (Pct $ftv 0.95)
            pct_under_3_33 = $(if ($ftv.Count) { [Math]::Round(100.0 * @($ftv | Where-Object { $_ -le 3.333 }).Count / $ftv.Count, 1) })
            sim_step_ms = $phase.sim_step_ms; chars_ms = $phase.chars_ms; fx_ms = $phase.fx_ms; actors_ms = $phase.actors_ms; hud_ms = $phase.hud_ms; sim_top = $simTop3
            cpu_submit_ms = $(if ($split.Count) { [Math]::Round(($split | Measure-Object submit -Average).Average, 2) }); gpu_wait_ms = $(if ($split.Count) { [Math]::Round(($split | Measure-Object gpu -Average).Average, 2) }); p99_ms = (Pct $ftv 0.99); max_ms = $(if ($ftv.Count) { [Math]::Round(($ftv | Measure-Object -Maximum).Maximum, 2) })
            over33 = @($ftv | Where-Object { $_ -gt 33.4 }).Count; over50 = @($ftv | Where-Object { $_ -gt 50 }).Count
            sim_p50_ms = (Pct $simv 0.50); sim_p99_ms = (Pct $simv 0.99); ai_avg_ms = $(if ($ai.Count) { [Math]::Round(($ai | Measure-Object avg -Average).Average, 3) }); ai_max_ms = $(if ($ai.Count) { ($ai | Measure-Object max -Maximum).Maximum })
            loaded_mb = $(if ($k -lt $ld.Count) { [Math]::Round([double]$ld[$k].privateMB) }); unloaded_mb = $(if ($k -lt $ul.Count) { [Math]::Round([double]$ul[$k].privateMB) })
            voices_max = $voices; voices_dropped = $dropped; voices_stolen = $stolen; mix_ms_max = $mixMs
            kills = $kills; broken_bots = $broken; struggling_bots = $strug; nopath_max = ($bl | Measure-Object nopath -Maximum).Maximum; off_mesh = @($bl | Where-Object { $_.cell -lt 0 }).Count
            end_reason = $reason; slowframe_lines = $sfN
            slowframe_bins = $(if ($sfN) { (@($sfBins.GetEnumerator() | Where-Object { $_.Value -gt 0 } | Sort-Object Value -Descending | ForEach-Object { "$($_.Key) $($_.Value) ($([Math]::Round(100.0 * $_.Value / $sfN))%)" }) -join "; ") })
            async_local_ms = $(if ($locVals.Count) { [Math]::Round(($locVals | Measure-Object -Average).Average, 3) }); async_bg_ms = $(if ($bgVals.Count) { [Math]::Round(($bgVals | Measure-Object -Average).Average, 3) })
            async_join_ms = $(if ($joinVals.Count) { [Math]::Round(($joinVals | Measure-Object -Average).Average, 3) }); async_join_max_ms = $(if ($joinVals.Count) { ($joinVals | Measure-Object -Maximum).Maximum }) }
        $rows.Add($row)
        Res "$mt.spawned" $(if ($players -ge $P.want) { "PASS" } elseif ($players) { "FAIL" } else { "UNKNOWN" }) ("spawned {0} of {1} participants" -f $players, $P.want) "Gameplay"
        Res "$mt.end" $(if ($reason) { "PASS" } else { "FAIL" }) ("MATCH end reason={0} (TimeLimit {1} s)" -f $(if ($reason) { $reason } else { "(none)" }), $TimeLimit) "Gameplay"
        Res "$mt.bots" $(if ($broken) { "FAIL" } elseif ($strug -gt [Math]::Max(1, [int]($P.want * 0.1))) { "PARTIAL" } else { "PASS" }) ("broken {0}, struggling {1}, max no-path {2}, off-mesh samples {3}" -f $broken, $strug, $row.nopath_max, $row.off_mesh) "Gameplay"
        Res "$mt.frame" "INFO" ("frame p50 {0} / p95 {1} / p99 {2} / max {3} ms over {4} frames; > 33 ms: {5}, > 50 ms: {6}; sim p50 {7} / p99 {8} ms; AI {9}" -f $row.p50_ms, $row.p95_ms, $row.p99_ms, $row.max_ms, $row.frames, $row.over33, $row.over50, $row.sim_p50_ms, $row.sim_p99_ms, $(if ($ai.Count) { "avg $($row.ai_avg_ms) / max $($row.ai_max_ms) ms" } else { "not logged by this build (WFC_BOTPERF)" })) "Gameplay/Rendering"
        if ($k -eq 1) {   # every population, the original 5 v 5 first (Integration: it should be the first to clear 300)
            $fpsNow = if ($row.p50_ms) { 1000.0 / $row.p50_ms } else { 60.0 }
            $cands = [ordered]@{ "world/scene submit (CPU)" = $row.cpu_submit_ms; "characters (dynamic meshes)" = $row.chars_ms; "sim (per frame share)" = $(if ($row.sim_step_ms) { [Math]::Round($row.sim_step_ms * [Math]::Min(1.0, 60.0 / $fpsNow), 2) }); "map FX" = $row.fx_ms; "actors draw" = $row.actors_ms; "HUD (ui.draw)" = $row.hud_ms; "GPU wait" = $row.gpu_wait_ms }
            $big = @($cands.GetEnumerator() | Where-Object { $null -ne $_.Value } | Sort-Object Value -Descending | Select-Object -First 1)[0]
            $phaseTxt = "phases (split run): " + ((@($cands.GetEnumerator() | ForEach-Object { "$($_.Key) $(if ($null -ne $_.Value) { $_.Value } else { '-' })" })) -join "; ") + "; sim step $($row.sim_step_ms) ms (top: $($row.sim_top)); BIGGEST: $(if ($big) { "$($big.Key) $($big.Value) ms" } else { '-' })"
            $t300 = if ($row.p90_ms -le 3.333) { "PASS" } elseif ($row.p50_ms -le 3.333) { "PARTIAL" } else { "FAIL" }
            Res "$mt.target_300fps" $t300 ("{0} {1} {2}: p50 {3} / p90 {4} / p99 {5} ms; frames under 3.33 ms {6} %; {7}; {8}. MET = p90 <= 3.33 ms, PARTIAL = p50 <= 3.33 ms" -f $resol, $map, $pop, $row.p50_ms, $row.p90_ms, $row.p99_ms, $row.pct_under_3_33, $splitTxt, $phaseTxt) "Rendering/Gameplay" }
        # the original 96-channel FMOD rule (Systems): steals / refusals at 64 participants are by design; MORE than 96 heard
        # voices is the defect (fixed in agents/systems e1fa3c0, M09l)
        if ($asyncM -eq "1") { Res "$mt.async" "INFO" ("async step: local {0} / background {1} / join wait avg {2} max {3} ms ({4} lines); last: {5}" -f $row.async_local_ms, $row.async_bg_ms, $row.async_join_ms, $row.async_join_max_ms, $asyncLines.Count, $(if ($asyncLines.Count) { $asyncLines[-1] } else { "no ASYNC lines (WFC_ASYNCLOG not in this build?)" })) "Gameplay" }
        if ($sfN) { Res "$mt.slowframes" "INFO" ("{0} in-play SLOWFRAME lines (frames over the WFC_SLOWFRAME threshold), by cause: {1}; {2}" -f $sfN, $row.slowframe_bins, $sfAvg) "Rendering" }
        $viewShot = Join-Path $d "m00600.bmp"
        if ($k -eq 1 -and (Test-Path $viewShot)) {   # the measured view: near-black / flat = the fixed cam sees a wall, numbers unrepresentative
            Add-Type -AssemblyName System.Drawing; $vb = New-Object System.Drawing.Bitmap $viewShot; $ls = New-Object System.Collections.Generic.List[double]
            for ($vy = 0; $vy -lt $vb.Height; $vy += 24) { for ($vx = 0; $vx -lt $vb.Width; $vx += 24) { $c = $vb.GetPixel($vx, $vy); $ls.Add(0.299 * $c.R + 0.587 * $c.G + 0.114 * $c.B) } }
            $vs = New-Object System.Drawing.Bitmap $vb, 480, 270; $vs.Save((Join-Path $d "view.png"), [System.Drawing.Imaging.ImageFormat]::Png); $vs.Dispose(); $vb.Dispose()
            $lsrt = @($ls | Sort-Object); $med = $lsrt[[int]($lsrt.Count / 2)]; $flat = 100.0 * @($ls | Where-Object { [Math]::Abs($_ - $med) -le 6 }).Count / $ls.Count
            $lm = ($ls | Measure-Object -Average).Average
            Res "$mt.view" $(if ($lm -lt 30 -or $flat -gt 85) { "FAIL" } else { "INFO" }) ("measured view (match step 600, $(Split-Path $d -Leaf)\view.png): mean luma {0:N0}, {1:N0} % flat{2}" -f $lm, $flat, $(if ($lm -lt 30 -or $flat -gt 85) { " - NEAR-BLACK / FLAT: the fixed cam sees a wall; frame times are not representative of play" } else { "" })) "Experimental" }
        if ($PlayerBot -ge 0 -and $H.Contains("WFC_PLAYERBOT")) {   # the pilot must actually play: log lines + shots
            $pb = @($segL | Where-Object { $_ -match 'PLAYERBOT' }); $pbShots = @($pb | ForEach-Object { $mm = [regex]::Match($_, 'shots (\d+)'); if ($mm.Success) { [int]$mm.Groups[1].Value } }) | Measure-Object -Maximum
            Res "$mt.pilot" $(if (-not $pb.Count) { "UNKNOWN" } elseif ($pbShots.Count -and $pbShots.Maximum -eq 0) { "FAIL" } else { "INFO" }) ("player bot: {0} PLAYERBOT log lines, max shots {1}{2}" -f $pb.Count, $(if ($pbShots.Count) { $pbShots.Maximum } else { "n/a (no 'shots' field)" }), $(if ($pb.Count) { "; last: " + ($pb[-1] -replace '^\[[^\]]*\]\s*', '').Substring(0, [Math]::Min(160, ($pb[-1] -replace '^\[[^\]]*\]\s*', '').Length)) } else { " - the pilot never logged (hook not wired into the loop?)" })) "Gameplay" }
        Res "$mt.audio" $(if ($voices -gt 96) { "FAIL" } else { "INFO" }) ("voices max {0} (cap 96), dropped {1}, stolen {2} (priority culling by design), mix max {3} ms / block" -f $voices, $dropped, $stolen, $mixMs) "Systems"
    }
    Res "$tag.second_match_and_exit" $(if ($seg.Count -ge 2 -and $clean) { "PASS" } else { "FAIL" }) ("{0} matches started; clean exit {1}" -f $seg.Count, $clean) "Frontend/Gameplay"
} } } }
# the tax: per map, each population's p50 / p99 vs the original 10 (second match: warm caches)
foreach ($map in $Maps) {
    foreach ($r in @($rows | Where-Object { $_.map -eq $map -and $_.pop -ne "orig10" -and $_.match -eq 2 })) {
        $base = @($rows | Where-Object { $_.map -eq $map -and $_.res -eq $r.res -and $_.pop -eq "orig10" -and $_.match -eq 2 })[0]
        if (-not $base -or -not $base.p50_ms) { continue }
        $t50 = [Math]::Round($r.p50_ms / $base.p50_ms, 2); $t99 = [Math]::Round($r.p99_ms / $base.p99_ms, 2)
        $st = if ($r.p99_ms -gt 33.4) { "FAIL" } elseif ($r.p99_ms -gt 16.7) { "PARTIAL" } else { "PASS" }
        Res "tax.$map.$($r.pop).$($r.res)" $st ("vs original 10: frame p50 {0} -> {1} ms (x{2}), p99 {3} -> {4} ms (x{5}); memory at load {6} -> {7} MB; AI avg {8} ms. PASS = p99 under 16.7 ms (60 fps), PARTIAL = under 33.4 ms (30 fps)" -f $base.p50_ms, $r.p50_ms, $t50, $base.p99_ms, $r.p99_ms, $t99, $base.loaded_mb, $r.loaded_mb, $r.ai_avg_ms) "Gameplay/Rendering"
    }
}
$curve = @($rows | Where-Object { $_.match -eq 2 -and $_.pop -match '^t\d+$' } | Sort-Object participants)
if ($curve.Count -ge 3) {
    foreach ($g in @($curve | Group-Object { "$($_.map) $($_.res) async=$($_.async)" })) {
        $pts = @($g.Group | Sort-Object participants); $x = @($pts | ForEach-Object { [double]$_.participants }); $y = @($pts | ForEach-Object { [double]$_.p50_ms })
        $mx = ($x | Measure-Object -Average).Average; $my = ($y | Measure-Object -Average).Average; $num = 0.0; $den = 0.0
        for ($i = 0; $i -lt $x.Count; $i++) { $num += ($x[$i] - $mx) * ($y[$i] - $my); $den += ($x[$i] - $mx) * ($x[$i] - $mx) }
        $slope = $num / $den; $icpt = $my - $slope * $mx
        $res2 = @(for ($i = 0; $i -lt $x.Count; $i++) { [Math]::Round($y[$i] - ($icpt + $slope * $x[$i]), 2) })
        # superlinear if the last point sits well above the line through the points (residual > 10 % of its value)
        $shape = if ($res2[-1] -gt 0.1 * $y[-1]) { "SUPERLINEAR (the largest population costs more than the linear trend)" } elseif ($icpt -gt 0.5 * $y[0]) { "LINEAR with a large FIXED overhead" } else { "LINEAR" }
        Res "curve.$($g.Name -replace ' ', '_')" "INFO" ("p50 frame vs participants: {0}; fit p50 = {1:N2} ms fixed + {2:N4} ms per participant; residuals {3}; {4}" -f (($pts | ForEach-Object { "$($_.participants): $($_.p50_ms)" }) -join ", "), $icpt, $slope, ($res2 -join " / "), $shape) "Rendering/Gameplay"
    }
}
Write-WfcCsv $rows (Join-Path $OutDir "capacity.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("commit", "async", "res", "map", "pop", "participants", "match", "spawned", "avg_fps", "avg_ms", "p50_ms", "p90_ms", "p95_ms", "p99_ms", "worst_steady_ms", "low1_fps", "low01_fps", "hitches", "warmup_hitches", "slow_with_step_pct", "pct_under_3_33", "cpu_submit_ms", "gpu_wait_ms", "sim_step_ms", "chars_ms", "fx_ms", "actors_ms", "hud_ms", "async_join_ms", "async_join_max_ms", "max_ms", "over33", "over50", "sim_p50_ms", "sim_p99_ms", "ai_avg_ms", "loaded_mb", "unloaded_mb", "voices_max", "voices_dropped", "kills", "broken_bots", "struggling_bots", "end_reason") (Join-Path $OutDir "CAPACITY.md") "Capacity stress" @("build: ``$sha`` ($Config); camera: $(if ($FixedCam -and $H.Contains('WFC_FIXEDCAM')) { 'FIXED (WFC_FIXEDCAM per map: ' + (($Maps | ForEach-Object { "$_ = $($camDefaults["$_"])" }) -join '; ') + ')' } else { 'scripted player (view-dependent)' }); TDM / DM private matches, TimeLimit $TimeLimit s, two matches per population in one process; difficulty $Difficulty; walk + strafe + periodic jump scripted player. Second-match numbers are the warm-cache comparison.")
if ($PerfLog) {
    $lines = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path $PerfLog)) { $lines.Add("# PERFORMANCE LOG (Experimental; user scalability brief, Integration 2026-10-07)"); $lines.Add(""); $lines.Add("Uncapped, fixed cam (WFC_FIXEDCAM per map), frontend-launched private TDM with bots, second-match (warm) figures. Steady stats exclude hitch events (> 50 ms), which are counted separately; the first 180 in-play frames are warm-up. 1 % / 0.1 % low = fps of the slowest 1 % / 0.1 % of steady frames. Splits come from a separate profiling run (glFinish-serialised: ratios, not absolute).") }
    $lines.Add(""); $lines.Add("## $(Get-Date -Format 'yyyy-MM-dd HH:mm') - $($sha.Substring(0, [Math]::Min(7, $sha.Length)))$(if ($FixedCam) { " - cam $CamSet" })$(if ($PlayerBot -ge 0) { " - real play: " + $(if ($H.Contains("WFC_PLAYERBOT")) { "PLAYERBOT $PlayerBot, follow cam" } else { "scripted input (approximation), follow cam" }) })$(if ($Note) { " - $Note" })"); $lines.Add("")
    $lines.Add("| map | res | async | participants | avg fps | p50 | p90 | p95 | p99 | worst steady | 1% low | 0.1% low | hitches | <=3.33 ms | submit | GPU wait | sim step | chars | FX | MB at load |")
    $lines.Add("|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|")
    foreach ($r in @($rows | Where-Object { $_.match -eq 2 } | Sort-Object map, res, async, participants)) {
        $lines.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} | {11} | {12} | {13} % | {14} | {15} | {16} | {17} | {18} | {19} |" -f $r.map, $r.res, $(if ($r.async -ne "") { $r.async } else { "-" }), $r.participants, $r.avg_fps, $r.p50_ms, $r.p90_ms, $r.p95_ms, $r.p99_ms, $r.worst_steady_ms, $r.low1_fps, $r.low01_fps, $r.hitches, $r.pct_under_3_33, $r.cpu_submit_ms, $r.gpu_wait_ms, $r.sim_step_ms, $r.chars_ms, $r.fx_ms, $r.loaded_mb)) }
    $lines | Add-Content -Encoding UTF8 $PerfLog
}
"CAPACITY: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
