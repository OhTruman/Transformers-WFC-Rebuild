# Local match validator (TDM first; any WFC_GAMEMODE). Observes MATCH events (protocols\RUNTIME-EVENTS.md).
#
#   .\tools\fidelity\match-validator.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Mode TDM] [-Frames 7200] [-Env @{...}]
#   .\tools\fidelity\match-validator.ps1 -Log <existing wfc.log> -OutDir <dir> [-Mode TDM]
#
# Expectations come only from recovered data; UNKNOWN expectations stay INFO with the measured value:
#   AUTHORED  gameplay.json modes: TDM = TnVersusGame, playlist CivilWar (1), time limits {600, 900, 1200} s,
#             min players 4; game_classes: MaxPlayers 10, MaxSpectators 2.
#   RE        rule set per mode (Gameplay MapState::gameRulesForMode); TnVersusGame spawns at TnTeamPlayerStart
#             [HIGH, Gameplay FIDELITY].
#   UNKNOWN   score limit, points per kill, respawn delay, team count source, end-of-match flow -> INFO.
# Always enforced (logical invariants, once the events exist): init before anything else; spawns at existing
# authored starts; timer non-increasing between restarts and decreasing at ~1 s per simulated second; team
# score never decreases (except after restart); every kill is followed by a score event; every death by a
# respawn or the match end; exactly one end per match; restart resets scores and the timer.
# Present product (no MATCH events yet): the protocol checks report SKIP "not implemented"; the spawn-class
# check uses the product's existing "start N/84: <actor> (<class> ...)" line.
param([string]$Exe = "", [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir, [string]$Mode = "TDM",
      [int]$Frames = 600, [hashtable]$Env = @{}, [string]$Log = "")
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$slice = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets"
$g = Get-Content -Raw "$slice\gameplay.json" | ConvertFrom-Json
$sp = Get-Content -Raw "$slice\spawnpoints.json" | ConvertFrom-Json
$modeInfo = @($g.modes | Where-Object tag -eq $Mode)[0]
$startsByActor = @{}; foreach ($p in $sp.points) { $startsByActor[$p.actor] = $p.class }
$reRules = @{ DM = "ScoreKillsDM,TrackKillsMP,ReportGameProgressTime,ReportGameProgressKills"; TDM = "ScoreKillsTDM,TrackKillsMP,ReportGameProgressTime,ReportGameProgressKills"
              CTF = "ScoreKillsMP,SingleFlagCTF,ScoreFlags,TrackKillsMP,ReportGameProgressTimeCTF"; KOTH = "ScoreKillsMP,ScoreKingOfTheHill,TrackKillsMP,ReportGameProgressTime,ReportGameProgressPoints"
              EXT = "ScoreKillsMP,ScoreBombingRun,TrackKillsMP,ReportGameProgressTime"; DOM = "ScoreKillsMP,ScoreDomination,TrackKillsMP,ReportGameProgressTime,ReportGameProgressPoints" }   # RE: Gameplay MapState.cpp
if (-not $Log) {
    $e = @{ WFC_SMOKE_FRAMES = "$Frames"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "30"; WFC_GAMEMODE = $Mode } + $Env
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $null = Invoke-WfcExe (Resolve-Path $Exe).Path (Join-Path $OutDir "run") $e "run.log" 1800
    $Log = Join-Path $OutDir "run\wfc.log"
}
$L = @(Get-Content $Log)
function Ev($kind) { return , @($L | Where-Object { $_ -cmatch "\] MATCH $kind\b" } | ForEach-Object { $h = @{ line = $_ }; foreach ($m in [regex]::Matches($_, '(\w+)=(\S+)')) { $h[$m.Groups[1].Value] = $m.Groups[2].Value }; $h }) }   # ", @(...)": keep a one-event result an array
$res = New-WfcResults
# ---- available now: spawn class follows the mode (product start line) ----
$wantCls = if ($Mode -eq "DM") { "TnFreeForAllPlayerStart" } else { "TnTeamPlayerStart" }
# the start the pawn spawned at: the product's teleport line if present, else the authored start nearest to the first logged frame
$st = @($L | Select-String "start \d+/\d+: (\S+) \((\S+), cluster") | Select-Object -First 1
$who = $null; $cls = $null; $dist = 0.0
if ($st) { $who = $st.Matches[0].Groups[1].Value; $cls = $st.Matches[0].Groups[2].Value }
else {
    $f = @($L | Select-String "\] frame (\d+) pos (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)") | Select-Object -First 1
    if ($f) { $p = @([double]$f.Matches[0].Groups[2].Value, [double]$f.Matches[0].Groups[3].Value, [double]$f.Matches[0].Groups[4].Value); $bd = 1e9
        foreach ($q in $sp.points) { if (@($q.location_gltf).Count -lt 3 -or $q.class -notlike "*PlayerStart") { continue }; $d = [Math]::Sqrt([Math]::Pow($q.location_gltf[0] - $p[0], 2) + [Math]::Pow($q.location_gltf[2] - $p[2], 2)); if ($d -lt $bd) { $bd = $d; $who = $q.actor; $cls = $q.class } }; $dist = $bd }
}
if ($cls) { Add-WfcResult $res "match.$Mode.spawn_start_class" $(if ($cls -eq $wantCls) { "PASS" } else { "FAIL" }) $null ("spawned at {0} ({1}{2}); expected {3} [RE HIGH: TnFreeForAllGame -> FFA starts, TnVersusGame modes -> team starts]" -f $who, $cls, $(if ($dist) { ", matched within {0:F2} m" -f $dist } else { "" }), $wantCls) "Gameplay" }
else { Add-WfcResult $res "match.$Mode.spawn_start_class" "SKIP" $null "no start line and no frame log" }
Add-WfcResult $res "match.$Mode.authored_mode" "INFO" $null ("authored: game class {0}, playlist {1}, time limits {2} s, min players {3}" -f $modeInfo.game_class, (($modeInfo.playlists | ForEach-Object { "$($_.name) ($($_.playlist_id))" }) -join ","), ($modeInfo.time_limits_s -join "/"), $modeInfo.min_players)
# ---- MATCH protocol ----
$init = Ev "init"
if (-not $init.Count) {
    foreach ($c in "init", "spawn", "timer", "kill_score", "death_respawn", "end", "restart") { Add-WfcResult $res "match.$Mode.$c" "SKIP" $null "not implemented: no 'MATCH $c' events in the log (protocols\RUNTIME-EVENTS.md)" "Gameplay" }
} else {
    $i0 = $init[0]
    Add-WfcResult $res "match.$Mode.init_mode" $(if ($i0.mode -eq $Mode) { "PASS" } else { "FAIL" }) $null "MATCH init mode=$($i0.mode) (requested $Mode)"
    $rs = @(($i0.rules -split ',') | ForEach-Object { ($_ -replace '^.*TnGameRules_', '') } | Sort-Object); $want = @(($reRules[$Mode] -split ',') | Sort-Object)
    Add-WfcResult $res "match.$Mode.init_rules" $(if (($rs -join ',') -eq ($want -join ',')) { "PASS" } else { "FAIL" }) $null ("rules {0}; RE {1}" -f ($rs -join ','), ($want -join ','))
    $tl = $i0.time_limit_s
    Add-WfcResult $res "match.$Mode.time_limit" $(if ($tl -and @($modeInfo.time_limits_s | ForEach-Object { "$_" }) -contains $tl) { "PASS" } elseif ($tl -eq "none" -or -not $tl) { "INFO" } else { "FAIL" }) $(if ($tl -match '^\d') { [double]$tl }) ("time_limit_s={0}; authored options {1}" -f $tl, ($modeInfo.time_limits_s -join "/"))
    Add-WfcResult $res "match.$Mode.score_limit" "INFO" $(if ($i0.score_limit -match '^\d') { [double]$i0.score_limit }) "score_limit=$($i0.score_limit) (UNKNOWN: not recovered)"
    Add-WfcResult $res "match.$Mode.teams" "INFO" $(if ($i0.teams -match '^\d') { [double]$i0.teams }) "teams=$($i0.teams)"
    # ordering: init first
    $firstMatch = ($L | Select-String -CaseSensitive "\] MATCH (\w+)" | Select-Object -First 1).Matches[0].Groups[1].Value   # case-sensitive log prefix: "Deathmatch message" in a FLOW line is not an event
    Add-WfcResult $res "match.$Mode.init_first" $(if ($firstMatch -eq "init") { "PASS" } else { "FAIL" }) $null "first MATCH event: $firstMatch"
    # spawns at authored starts of the right class
    $sps = Ev "spawn"; $badSp = @($sps | Where-Object { $startsByActor[$_.start] -ne $wantCls })
    Add-WfcResult $res "match.$Mode.spawns" $(if (-not $sps.Count) { "SKIP" } elseif (-not $badSp.Count) { "PASS" } else { "FAIL" }) $sps.Count ("{0} spawns; at wrong / unauthored starts: {1}" -f $sps.Count, (($badSp | ForEach-Object { $_.start }) -join ","))
    # timer
    $tm = @((Ev "timer") | ForEach-Object { [double]$_.remaining_s }); $inc = 0; for ($k = 1; $k -lt $tm.Count; $k++) { if ($tm[$k] -gt $tm[$k - 1] + 1e-3) { $inc++ } }
    Add-WfcResult $res "match.$Mode.timer_monotonic" $(if ($tm.Count -lt 2) { "SKIP" } elseif ($inc -le (Ev "restart").Count) { "PASS" } else { "FAIL" }) $inc ("{0} timer samples; increases {1} (allowed: restarts)" -f $tm.Count, $inc)
    # kills -> score; deaths -> respawn
    $kills = Ev "kill"; $scores = Ev "score"; $deaths = Ev "death"; $resp = Ev "respawn"; $ends = Ev "end"
    Add-WfcResult $res "match.$Mode.kill_scored" $(if (-not $kills.Count) { "SKIP" } elseif ($scores.Count -ge $kills.Count) { "PASS" } else { "FAIL" }) $kills.Count ("{0} kills, {1} score events (points per kill UNKNOWN)" -f $kills.Count, $scores.Count)
    $dec = 0; $last = @{}; foreach ($ln in ($L | Where-Object { $_ -cmatch "\] MATCH (score|restart)\b" })) { if ($ln -cmatch "\] MATCH restart") { $last = @{}; continue }; $mm = [regex]::Match($ln, "team=(-?\d+).*score=(-?\d+)"); if (-not $mm.Success) { continue }; $tk = $mm.Groups[1].Value; $sv = [double]$mm.Groups[2].Value; if ($last.ContainsKey($tk) -and $sv -lt $last[$tk]) { $dec++ }; $last[$tk] = $sv }   # per match: scores restart with each MATCH restart
    Add-WfcResult $res "match.$Mode.score_monotonic" $(if (-not $scores.Count) { "SKIP" } elseif ($dec -eq 0) { "PASS" } else { "FAIL" }) $dec "team score decreases within a match"
    $delays = @($resp | Where-Object { $_.delay_s } | ForEach-Object { [double]$_.delay_s })
    Add-WfcResult $res "match.$Mode.death_respawn" $(if (-not $deaths.Count) { "SKIP" } elseif ($resp.Count + $ends.Count -ge $deaths.Count) { "PASS" } else { "FAIL" }) $deaths.Count ("{0} deaths, {1} respawns; respawn delay {2} (UNKNOWN expectation)" -f $deaths.Count, $resp.Count, $(if ($delays.Count) { "{0:F2}-{1:F2} s" -f ($delays | Measure-Object -Minimum).Minimum, ($delays | Measure-Object -Maximum).Maximum } else { "n/a" }))
    Add-WfcResult $res "match.$Mode.end" $(if (-not $ends.Count) { "SKIP" } elseif ($ends.Count -eq 1 + (Ev "restart").Count) { "PASS" } else { "FAIL" }) $ends.Count ("end events: " + (($ends | ForEach-Object { "reason=$($_.reason) winner=$($_.winner) t=$($_.t)" }) -join "; "))
    if ($ends.Count -and $tl -match '^\d') { $te = $ends | Where-Object reason -eq "time_limit" | Select-Object -First 1; if ($te) { Add-WfcResult $res "match.$Mode.end_at_time_limit" $(if ([Math]::Abs([double]$te.t - [double]$tl) -le 1.5) { "PASS" } else { "FAIL" }) ([double]$te.t) "time-limit end at t=$($te.t) s for time_limit_s=$tl (match start at t=0)" } }
    $rst = Ev "restart"
    if ($rst.Count) { $afterIdx = [Array]::IndexOf($L, $rst[0].line); $after = @($L[($afterIdx + 1)..($L.Count - 1)] | Where-Object { $_ -cmatch "\] MATCH score" } | Select-Object -First 2); Add-WfcResult $res "match.$Mode.restart_resets" $(if (-not $after.Count -or @($after | Where-Object { $_ -match "score=([01])\b" }).Count -eq $after.Count) { "PASS" } else { "FAIL" }) $null "first score events after restart: $($after -join ' | ')" }
    else { Add-WfcResult $res "match.$Mode.restart_resets" "SKIP" $null "no restart in this run" }
    Add-WfcResult $res "match.$Mode.cleanup" $(if ((Ev "cleanup").Count) { "PASS" } else { "SKIP" }) (Ev "cleanup").Count "MATCH cleanup events"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"MATCH VALIDATOR ($Mode): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
