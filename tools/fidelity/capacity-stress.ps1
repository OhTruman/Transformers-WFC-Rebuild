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
      [string[]]$Resolutions = @("1280x720"), [switch]$NoSplit,
      [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$Maps = @($Maps | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$Resolutions = @($Resolutions | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$Pops = @($Pops | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "capacity.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_LOBBY_OPTIONS")) { Res "hook" "UNKNOWN" "build has no WFC_LOBBY_OPTIONS (Frontend 2cf1ab1): bot counts cannot be set per faction" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$popDef = @{
    orig10 = @{ mode = "TDM"; want = 10; opts = "ExtendedPlayers=0;BotsAutobot=5;BotsDecepticon=5" }
    p16v16 = @{ mode = "TDM"; want = 32; opts = "ExtendedPlayers=1;BotsAutobot=16;BotsDecepticon=16" }
    p32v32 = @{ mode = "TDM"; want = 64; opts = "ExtendedPlayers=1;BotsAutobot=32;BotsDecepticon=32" }
    ffa64  = @{ mode = "DM";  want = 64; opts = "ExtendedPlayers=1;BotsEnemy=63" }
}
function Pct($v, $p) { if (-not $v.Count) { return $null }; $s = @($v | Sort-Object); return [Math]::Round($s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))], 2) }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($resol in $Resolutions) { $rw = [int]($resol -split 'x')[0]; $rh = [int]($resol -split 'x')[1]   # NOT $res (the results collection)
foreach ($map in $Maps) { foreach ($pop in $Pops) {
    $P = $popDef[$pop]; if (-not $P) { continue }
    $tag = "{0}_{1}_{2}" -f $map, $pop, $resol; $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null
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
                     WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_RENDERSTATS = "1"; WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_AUTOJUMP_EVERY = "150"
                     WFC_LOBBY_OPTIONS = "$($P.opts);PointsToWin=9999;TimeLimit=600" }
            if ($H.Contains("WFC_CHARSELECT")) { $e2.WFC_CHARSELECT = "1" }
            $null = Invoke-WfcExe $exe $ds $e2 "run.log" 700
        }
    }
    $split = @(if (Test-Path $lgs) { $inG = $false; foreach ($sl in [IO.File]::ReadLines($lgs)) { if ($sl.Contains('to=InGame')) { $inG = $true; continue }
        if ($inG) { $sm = [regex]::Match($sl, 'wfc: avg frame ([\d.]+) ms \(\d+ fps\); scene submit ([\d.]+) ms, gpu wait ([\d.]+) ms'); if ($sm.Success) { [pscustomobject]@{ frame = [double]$sm.Groups[1].Value; submit = [double]$sm.Groups[2].Value; gpu = [double]$sm.Groups[3].Value } } } } })
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
        $inPlay = $false; $ft = New-Object System.Collections.Generic.List[double]; $sim = New-Object System.Collections.Generic.List[double]
        $voices = 0; $dropped = 0; $stolen = 0; $mixMs = 0.0; $ai = @(); $bl = @()
        foreach ($l in $segL) {
            if (-not $inPlay -and $l.Contains('to=InGame')) { $inPlay = $true; continue }
            if ($inPlay -and $l.Contains('to=GameEnded')) { $inPlay = $false }
            if (-not $inPlay) { continue }
            $m = [regex]::Match($l, 'PERF f\d+ frame=([\d.]+)ms sim=([\d.]+)ms'); if ($m.Success) { $ft.Add([double]$m.Groups[1].Value); $sim.Add([double]$m.Groups[2].Value); continue }
            $m = [regex]::Match($l, 'AMB .*voices=(\d+) \(max (\d+), dropped (\d+), stolen (\d+).*mix=([\d.]+)ms'); if ($m.Success) { $voices = [Math]::Max($voices, [int]$m.Groups[2].Value); $dropped = [int]$m.Groups[3].Value; $stolen = [int]$m.Groups[4].Value; $mixMs = [Math]::Max($mixMs, [double]$m.Groups[5].Value); continue }
            $m = [regex]::Match($l, 'BOTPERF .*AI ([\d.]+) ms avg ([\d.]+)'); if ($m.Success) { $ai += [pscustomobject]@{ avg = [double]$m.Groups[1].Value; max = [double]$m.Groups[2].Value }; continue }
            $m = [regex]::Match($l, 'BOTLOG \S+ p(\d+) \(([-\d.]+) [-\d.]+ ([-\d.]+)\) cell (-?\d+) \S+ goal (\S+) .* tgt (-?\d+) .* stuck (\d+) .* nopath (\d+)')
            if ($m.Success) { $bl += [pscustomobject]@{ p = [int]$m.Groups[1].Value; x = [double]$m.Groups[2].Value; z = [double]$m.Groups[3].Value; cell = [int]$m.Groups[4].Value; goal = $m.Groups[5].Value; tgt = [int]$m.Groups[6].Value; stuck = [int]$m.Groups[7].Value; nopath = [int]$m.Groups[8].Value } }
        }
        $ftv = @($ft | Select-Object -Skip ([Math]::Min($ft.Count, 180))); $simv = @($sim | Select-Object -Skip ([Math]::Min($sim.Count, 180)))
        $players = @($segL | Where-Object { $_ -match '\] MATCH spawn player=(\d+)' } | ForEach-Object { [regex]::Match($_, 'player=(\d+)').Groups[1].Value } | Select-Object -Unique).Count
        $endL = @($segL | Where-Object { $_ -match '\] MATCH end ' })[0]; $reason = if ($endL) { [regex]::Match($endL, 'reason=(\S+)').Groups[1].Value } else { "" }; if ($endL -and -not $reason) { $reason = "Time" }   # an EMPTY reason is the original's time-limit path (EndGame(none, ""), Gameplay 2026-10-07; printed as Time from their next push)
        $kills = @($segL | Where-Object { $_ -match '\] MATCH kill ' }).Count
        $broken = 0; $strug = 0
        foreach ($g in @($bl | Group-Object p)) { $gg = @($g.Group); $run = 0; $fz = 0
            for ($i = 1; $i -lt $gg.Count; $i++) { $mv = [Math]::Sqrt([Math]::Pow($gg[$i].x - $gg[$i - 1].x, 2) + [Math]::Pow($gg[$i].z - $gg[$i - 1].z, 2))
                if ($mv -lt 0.1 -and $gg[$i].tgt -lt 0 -and $gg[$i].goal -match '^(Roam|Attack|Retrieve)') { $run++; $fz = [Math]::Max($fz, $run) } else { $run = 0 } }
            if ($fz -ge 20) { $broken++ } elseif (@($gg | Where-Object { $_.stuck -ge 2 }).Count / [Math]::Max(1, $gg.Count) -gt 0.25) { $strug++ } }
        $row = [pscustomobject][ordered]@{ res = $resol; map = $map; pop = $pop; match = $k + 1; spawned = "$players/$($P.want)"; frames = $ftv.Count
            p50_ms = (Pct $ftv 0.50); p90_ms = (Pct $ftv 0.90); p95_ms = (Pct $ftv 0.95)
            pct_under_3_33 = $(if ($ftv.Count) { [Math]::Round(100.0 * @($ftv | Where-Object { $_ -le 3.333 }).Count / $ftv.Count, 1) })
            cpu_submit_ms = $(if ($split.Count) { [Math]::Round(($split | Measure-Object submit -Average).Average, 2) }); gpu_wait_ms = $(if ($split.Count) { [Math]::Round(($split | Measure-Object gpu -Average).Average, 2) }); p99_ms = (Pct $ftv 0.99); max_ms = $(if ($ftv.Count) { [Math]::Round(($ftv | Measure-Object -Maximum).Maximum, 2) })
            over33 = @($ftv | Where-Object { $_ -gt 33.4 }).Count; over50 = @($ftv | Where-Object { $_ -gt 50 }).Count
            sim_p50_ms = (Pct $simv 0.50); sim_p99_ms = (Pct $simv 0.99); ai_avg_ms = $(if ($ai.Count) { [Math]::Round(($ai | Measure-Object avg -Average).Average, 3) }); ai_max_ms = $(if ($ai.Count) { ($ai | Measure-Object max -Maximum).Maximum })
            loaded_mb = $(if ($k -lt $ld.Count) { [Math]::Round([double]$ld[$k].privateMB) }); unloaded_mb = $(if ($k -lt $ul.Count) { [Math]::Round([double]$ul[$k].privateMB) })
            voices_max = $voices; voices_dropped = $dropped; voices_stolen = $stolen; mix_ms_max = $mixMs
            kills = $kills; broken_bots = $broken; struggling_bots = $strug; nopath_max = ($bl | Measure-Object nopath -Maximum).Maximum; off_mesh = @($bl | Where-Object { $_.cell -lt 0 }).Count
            end_reason = $reason }
        $rows.Add($row)
        Res "$mt.spawned" $(if ($players -ge $P.want) { "PASS" } elseif ($players) { "FAIL" } else { "UNKNOWN" }) ("spawned {0} of {1} participants" -f $players, $P.want) "Gameplay"
        Res "$mt.end" $(if ($reason) { "PASS" } else { "FAIL" }) ("MATCH end reason={0} (TimeLimit {1} s)" -f $(if ($reason) { $reason } else { "(none)" }), $TimeLimit) "Gameplay"
        Res "$mt.bots" $(if ($broken) { "FAIL" } elseif ($strug -gt [Math]::Max(1, [int]($P.want * 0.1))) { "PARTIAL" } else { "PASS" }) ("broken {0}, struggling {1}, max no-path {2}, off-mesh samples {3}" -f $broken, $strug, $row.nopath_max, $row.off_mesh) "Gameplay"
        Res "$mt.frame" "INFO" ("frame p50 {0} / p95 {1} / p99 {2} / max {3} ms over {4} frames; > 33 ms: {5}, > 50 ms: {6}; sim p50 {7} / p99 {8} ms; AI {9}" -f $row.p50_ms, $row.p95_ms, $row.p99_ms, $row.max_ms, $row.frames, $row.over33, $row.over50, $row.sim_p50_ms, $row.sim_p99_ms, $(if ($ai.Count) { "avg $($row.ai_avg_ms) / max $($row.ai_max_ms) ms" } else { "not logged by this build (WFC_BOTPERF)" })) "Gameplay/Rendering"
        if ($k -eq 1 -and $pop -ne "orig10") {
            $t300 = if ($row.p90_ms -le 3.333) { "PASS" } elseif ($row.p50_ms -le 3.333) { "PARTIAL" } else { "FAIL" }
            Res "$mt.target_300fps" $t300 ("{0} {1} {2}: p50 {3} / p90 {4} / p99 {5} ms; frames under 3.33 ms {6} %; {7}. MET = p90 <= 3.33 ms, PARTIAL = p50 <= 3.33 ms" -f $resol, $map, $pop, $row.p50_ms, $row.p90_ms, $row.p99_ms, $row.pct_under_3_33, $splitTxt) "Rendering/Gameplay" }
        # the original 96-channel FMOD rule (Systems): steals / refusals at 64 participants are by design; MORE than 96 heard
        # voices is the defect (fixed in agents/systems e1fa3c0, M09l)
        Res "$mt.audio" $(if ($voices -gt 96) { "FAIL" } else { "INFO" }) ("voices max {0} (cap 96), dropped {1}, stolen {2} (priority culling by design), mix max {3} ms / block" -f $voices, $dropped, $stolen, $mixMs) "Systems"
    }
    Res "$tag.second_match_and_exit" $(if ($seg.Count -ge 2 -and $clean) { "PASS" } else { "FAIL" }) ("{0} matches started; clean exit {1}" -f $seg.Count, $clean) "Frontend/Gameplay"
} } }
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
Write-WfcCsv $rows (Join-Path $OutDir "capacity.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("res", "map", "pop", "match", "spawned", "p50_ms", "p90_ms", "p95_ms", "p99_ms", "pct_under_3_33", "cpu_submit_ms", "gpu_wait_ms", "max_ms", "over33", "over50", "sim_p50_ms", "sim_p99_ms", "ai_avg_ms", "loaded_mb", "unloaded_mb", "voices_max", "voices_dropped", "kills", "broken_bots", "struggling_bots", "end_reason") (Join-Path $OutDir "CAPACITY.md") "Capacity stress" @("build: ``$sha`` ($Config); TDM / DM private matches, TimeLimit $TimeLimit s, two matches per population in one process; difficulty $Difficulty; walk + strafe + periodic jump scripted player. Second-match numbers are the warm-cache comparison.")
"CAPACITY: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
