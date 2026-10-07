# BOT MATRIX (tier TARGETED; milestone 09 multiplayer bots, PC ADAPTATION). Frontend-launched private matches with the
# Private Match Bot Settings written into the run's own profile ([PCSettings] BotsFriendly / BotsEnemy / BotDifficulty;
# the profile is cwd-relative, so each run starts from its own), exactly as a player sets them.
# Default matrix (-Matrix default): TDM on Streets at 0v1, 2v3 and 7v8 (full 16-player team population); then DOM / KOTH /
# CTF / EXT on Streets at 3v4; then TDM 3v4 on Berth and Gorge. -Seconds of play each.
# Per run, from the MATCH protocol (frontend-launched), WFC_BOTLOG (each bot once a second), hud.killFeed and PERF:
#   spawned       distinct spawned players == friendly + enemy + 1; friendly bots on the local team, enemies on the other
#   combat        kills happen (with >= 1 enemy) within the run
#   navigation    each bot travels (path length over BOTLOG samples), stuck fraction, no-path count
#   kill_feed     a kill-feed line per kill (TnDeathMessage for every kill, as the original)
#   frame_time    mean frame ms with that population (PERF), and the bot AI cost when logged
#   clean_exit    the process shut down cleanly
# GPU: the default matrix is ~15 min of graphics - send Integration a "long GPU run" note first.
#
#   .\tools\fidelity\bot-matrix.ps1 -Root work\ab\<target> -OutDir <dir> [-Matrix default|quick] [-Seconds 60] [-Difficulty 1] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("default", "quick", "difficulty")][string]$Matrix = "default",
      [int]$Seconds = 60, [int]$Difficulty = 1, [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "bots.$id" $status $null $note $owner }
$mapId = @{ MP_IAC_Streets = 508; MP_IAC_Berth = 502; MP_UND_Gorge = 510; MP_IAC_Seed = 501; MP_ORB_Debris = 507 }
$runs = @(@{ mode = "TDM"; map = "MP_IAC_Streets"; f = 0; e = 1 }, @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 2; e = 3 }, @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 7; e = 8 })
if ($Matrix -eq "default") {
    $runs += @("DOM", "KOTH", "CTF", "EXT" | ForEach-Object { @{ mode = $_; map = "MP_IAC_Streets"; f = 3; e = 4 } })
    $runs += @("MP_IAC_Berth", "MP_UND_Gorge" | ForEach-Object { @{ mode = "TDM"; map = $_; f = 3; e = 4 } })
    $runs += @{ mode = "DM"; map = "MP_IAC_Streets"; f = 0; e = 15 }                                   # FFA, full 16 players
    $runs += @{ mode = "TDM"; map = "MP_ORB_Debris"; f = 3; e = 4; weaker = $true }                    # flight-only islands unreachable until the jet air layer
    $runs += @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 3; e = 4; diff = 0 }, @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 3; e = 4; diff = 2 }   # EASY vs HARD (mirror, INFO)
    # MIXED difficulty (Gameplay 42d991d ?BotDifficultyAutobot / ?BotDifficultyDecepticon via Frontend WFC_LOBBY_OPTIONS): 4 v 4 bots,
    # HARD on one faction and EASY on the other, then swapped (cancels side bias). Team 0 = Autobot, 1 = Decepticon.
}
if ($Matrix -eq "difficulty") { $runs = @() }
if ($Matrix -in "default", "difficulty") {
    if ($H.Contains("WFC_LOBBY_OPTIONS")) {
        $runs += @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 4; e = 4; tag = "mixA"; hardTeam = 0; lobby = "BotsAutobot=4;BotsDecepticon=4;BotDifficultyAutobot=2;BotDifficultyDecepticon=0" }
        $runs += @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 4; e = 4; tag = "mixB"; hardTeam = 1; lobby = "BotsAutobot=4;BotsDecepticon=4;BotDifficultyAutobot=0;BotDifficultyDecepticon=2" }
    }
}
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($r in $runs) {
    $diff = if ($null -ne $r.diff) { [int]$r.diff } else { $Difficulty }
    $tag = "{0}_{1}_{2}v{3}{4}" -f $r.mode, ($r.map -replace '^MP_', ''), $r.f, $r.e, $(if ($null -ne $r.diff) { "_d$diff" } elseif ($r.tag) { "_$($r.tag)" } else { "" }); $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$tag.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        "[PCSettings]`nWidth=1280`nHeight=720`nFullscreen=0`nBotsFriendly=$($r.f)`nBotsEnemy=$($r.e)`nBotDifficulty=$diff`n" | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $party = if ($r.mode -eq "DM") { "GTS_FreeForAllGame" } else { "GTS_TeamGame" }
        $s = @((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,$party", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
               "call:Online.EditGameMode,$($r.mode)", "call:Online.PlayPrivateGame,$($r.mode)", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,$($mapId[$r.map])", "wait:t=1",
               "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=$Seconds", "quit") -join ";"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "400";
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_BOTLOG = "all"; WFC_PERFLOG = "60"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        if ($r.lobby) { $e.WFC_LOBBY_OPTIONS = $r.lobby }
        $null = Invoke-WfcExe $exe $d $e "run.log" ($Seconds + 240)
    }
    if (-not (Test-Path $lg)) { continue }
    $lines = @([IO.File]::ReadLines($lg)); $clean = [bool]($lines | Where-Object { $_ -match 'Shutdown complete' } | Select-Object -First 1)
    $spawns = @($lines | Where-Object { $_ -match '\] MATCH spawn player=(\d+) team=(-?\d+)' } | ForEach-Object { $m = [regex]::Match($_, 'player=(\d+) team=(-?\d+)'); [pscustomobject]@{ p = [int]$m.Groups[1].Value; team = [int]$m.Groups[2].Value } })
    $players = @($spawns | Group-Object p | ForEach-Object { [pscustomobject]@{ p = [int]$_.Name; team = $_.Group[0].team } })
    $kl = @($lines | Where-Object { $_ -match '\] MATCH kill ' } | ForEach-Object { $m = [regex]::Match($_, 'killer=(-?\d+) victim=(-?\d+) killer_team=(-?\d+) victim_team=(-?\d+) weapon=(\S*)')
        if ($m.Success) { [pscustomobject]@{ k = [int]$m.Groups[1].Value; v = [int]$m.Groups[2].Value; kt = [int]$m.Groups[3].Value; vt = [int]$m.Groups[4].Value; w = $m.Groups[5].Value } } })
    $kills = $kl.Count
    $suicides = @($kl | Where-Object { $_.k -eq $_.v -or $_.k -lt 0 }).Count
    $teamKills = if ($r.mode -eq "DM") { 0 } else { @($kl | Where-Object { $_.k -ne $_.v -and $_.k -ge 0 -and $_.kt -eq $_.vt }).Count }
    $loadedMb = @(Flow-Ev (Read-FlowLog (Join-Path $d "flow.jsonl")) "match.loaded" | ForEach-Object { [double]$_.privateMB })[0]
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $feed = @(Flow-Ev $F "hud.killFeed").Count
    $bl = @($lines | Where-Object { $_ -match '\] BOTLOG ' } | ForEach-Object { $m = [regex]::Match($_, 'BOTLOG (\S+) p(\d+) \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) cell (-?\d+) \S+ goal (\S+) .* tgt (-?\d+) .* stuck (\d+) .* shots (\d+) hits (\d+) nopath (\d+)')
        if ($m.Success) { [pscustomobject]@{ p = [int]$m.Groups[2].Value; x = [double]$m.Groups[3].Value; y = [double]$m.Groups[4].Value; z = [double]$m.Groups[5].Value; cell = [int]$m.Groups[6].Value
            goal = $m.Groups[7].Value; tgt = [int]$m.Groups[8].Value; stuck = [int]$m.Groups[9].Value; shots = [int]$m.Groups[10].Value; hits = [int]$m.Groups[11].Value; nopath = [int]$m.Groups[12].Value } } })
    $nav = @($bl | Group-Object p | ForEach-Object { $g = @($_.Group); $dist = 0.0; for ($i = 1; $i -lt $g.Count; $i++) { $dx = $g[$i].x - $g[$i - 1].x; $dz = $g[$i].z - $g[$i - 1].z; $step = [Math]::Sqrt($dx * $dx + $dz * $dz); if ($step -lt 30) { $dist += $step } }
        $frozen = 0; $run = 0; for ($i = 1; $i -lt $g.Count; $i++) { $mv = [Math]::Sqrt([Math]::Pow($g[$i].x - $g[$i - 1].x, 2) + [Math]::Pow($g[$i].z - $g[$i - 1].z, 2))
            if ($mv -lt 0.1 -and $g[$i].tgt -lt 0 -and $g[$i].goal -match '^(Roam|Attack|Retrieve)') { $run++; $frozen = [Math]::Max($frozen, $run) } else { $run = 0 } }
        [pscustomobject]@{ p = [int]$_.Name; samples = $g.Count; dist = [Math]::Round($dist, 1); stuckFrac = [Math]::Round(@($g | Where-Object { $_.stuck -ge 2 }).Count / [Math]::Max(1, $g.Count), 2); nopath = ($g | Measure-Object nopath -Maximum).Maximum
            frozenS = $frozen; offMesh = @($g | Where-Object { $_.cell -lt 0 }).Count; shots = $g[-1].shots; hits = $g[-1].hits } })
    $perf = @($lines | Where-Object { $_ -match 'PERF f\d+ frame=([\d.]+)ms' } | ForEach-Object { [double]([regex]::Match($_, 'frame=([\d.]+)ms').Groups[1].Value) } | Select-Object -Skip 2)
    $frameMs = if ($perf.Count) { [Math]::Round(($perf | Measure-Object -Average).Average, 2) } else { $null }
    $want = $r.f + $r.e + 1
    $local = @($players | Sort-Object p | Select-Object -First 1)[0]
    $teamOk = if ($r.mode -eq "DM" -or -not $local -or $r.lobby) { $true } else { $fr = @($players | Where-Object { $_.p -ne $local.p -and $_.team -eq $local.team }).Count; $en = @($players | Where-Object { $_.team -ne $local.team }).Count; ($fr -eq $r.f -and $en -eq $r.e) }
    $stuckBots = @($nav | Where-Object { $_.stuckFrac -gt 0.25 -or $_.dist -lt 10 }); $noPathMax = ($nav | Measure-Object nopath -Maximum).Maximum
    $broken = @($nav | Where-Object { $_.frozenS -ge 20 })   # Gameplay's "broken" criterion: no displacement >= 20 s, no target, moving goal
    $offMesh = ($nav | Measure-Object offMesh -Sum).Sum
    $shotsT = ($nav | Measure-Object shots -Sum).Sum; $hitsT = ($nav | Measure-Object hits -Sum).Sum; $acc = if ($shotsT) { [Math]::Round($hitsT / $shotsT, 3) } else { $null }
    $rows.Add([pscustomobject][ordered]@{ run = $tag; spawned = "$($players.Count)/$want"; teams = $teamOk; kills = $kills; kill_feed = $feed; bots_logged = $nav.Count
        median_bot_path_m = $(if ($nav.Count) { ($nav | ForEach-Object { $_.dist } | Sort-Object)[[int]($nav.Count / 2)] }); stuck_or_idle_bots = $stuckBots.Count; broken_bots = $broken.Count; off_mesh_samples = $offMesh; accuracy = $acc; difficulty = $diff; nopath_max = $noPathMax; frame_ms = $frameMs; loaded_mb = $loadedMb; hard_team = $r.hardTeam
        hard_bot_kills = $(if ($null -ne $r.hardTeam) { @($kl | Where-Object { $_.k -gt 0 -and $_.v -gt 0 -and $_.k -ne $_.v -and $_.kt -eq $r.hardTeam }).Count })
        easy_bot_kills = $(if ($null -ne $r.hardTeam) { @($kl | Where-Object { $_.k -gt 0 -and $_.v -gt 0 -and $_.k -ne $_.v -and $_.kt -ne $r.hardTeam -and $_.kt -ge 0 }).Count }); suicides = $suicides; team_kills = $teamKills; clean_exit = $clean })
    Res "$tag.spawned" $(if ($players.Count -eq $want -and $teamOk) { "PASS" } elseif ($players.Count) { "FAIL" } else { "UNKNOWN" }) ("distinct spawned players {0} of {1} (local + {2} friendly + {3} enemy); team split correct {4}" -f $players.Count, $want, $r.f, $r.e, $teamOk) "Gameplay"
    # a single enemy bot can roam a large map for 60 s without meeting the scripted (wall-walking) player: no contact is not a
    # combat defect there (8c2b6e3 0v1: the bot roamed toward a goal 433 m away, never saw the player) -> INFO; >= 2 enemies must fight
    if ($r.e -ge 1) { Res "$tag.combat" $(if ($kills -gt 0) { "PASS" } elseif ($r.e -lt 2) { "INFO" } else { "FAIL" }) ("{0} kills in {1} s of play{2}" -f $kills, $Seconds, $(if ($kills -eq 0 -and $r.e -lt 2) { " (single enemy bot: contact not guaranteed in the window)" } else { "" })) "Gameplay" }
    $navSt = if (-not $nav.Count) { "UNKNOWN" } elseif ($broken.Count) { "FAIL" } elseif ($stuckBots.Count) { "PARTIAL" } else { "PASS" }
    if ($r.weaker -and $navSt -eq "FAIL") { $navSt = "PARTIAL" }   # Debris: flight-only islands unreachable until the jet air layer (Gameplay, known)
    Res "$tag.navigation" $navSt ("{0} bots logged; median path {1} m; BROKEN (no displacement >= 20 s, no target, Roam / Attack / Retrieve goal): {4}; struggling (stuck >= 2 on > 25 % of samples) or idle (< 10 m): {2}; max no-path {3}; off-mesh samples {5}{6}" -f $nav.Count, $rows[-1].median_bot_path_m, $(if ($stuckBots.Count) { ($stuckBots | ForEach-Object { "p$($_.p) $($_.dist) m stuck $($_.stuckFrac)" }) -join ", " } else { "none" }), $noPathMax, $(if ($broken.Count) { ($broken | ForEach-Object { "p$($_.p) $($_.frozenS) s" }) -join ", " } else { "none" }), $offMesh, $(if ($r.weaker) { "; Debris expected weaker (flight-only islands)" } else { "" })) "Gameplay"
    if ($kills -gt 0) { Res "$tag.kill_feed" $(if ($feed -eq $kills) { "PASS" } else { "FAIL" }) ("kill-feed lines {0} vs kills {1}" -f $feed, $kills) "Frontend" }
    Res "$tag.frame_time" $(if ($frameMs -eq $null) { "UNKNOWN" } elseif ($frameMs -gt 16.7) { "PARTIAL" } else { "INFO" }) ("mean frame {0} ms with {1} players" -f $frameMs, $want) "Gameplay/Rendering"
    if ($kills -gt 0) { Res "$tag.suicides" $(if (($suicides + $teamKills) / [double]$kills -gt 0.25) { "PARTIAL" } else { "INFO" }) ("suicides / environment deaths {0}, team kills {1} of {2} kills (weapons: {3})" -f $suicides, $teamKills, $kills, ((@($kl | Group-Object w | Sort-Object Count -Descending | ForEach-Object { "$($_.Name) $($_.Count)" })) -join ", ")) "Gameplay" }
    Res "$tag.memory" "INFO" ("privateMB at match loaded: {0} with {1} players" -f $loadedMb, $want) "Gameplay/Rendering/Systems"
    Res "$tag.clean_exit" $(if ($clean) { "PASS" } else { "FAIL" }) ("shutdown complete {0}" -f $clean) "Integration"
}
$easy = @($rows | Where-Object { $_.run -like "*_d0" })[0]; $hard = @($rows | Where-Object { $_.run -like "*_d2" })[0]
if ($easy -and $hard -and $easy.accuracy -ne $null -and $hard.accuracy -ne $null) {
    # Mirror matches (Bot Settings has ONE difficulty for every bot): HARD bots shoot at HARD targets that strafe more (1.0 vs
    # 0.45), from farther (sight 70 vs 50 m), and fire more - accuracy is confounded and is NOT a pass / fail signal
    # (8c2b6e3: EASY 0.738 vs HARD 0.485, kills 8 / 8). Gameplay's criterion (HARD out-kills EASY) needs MIXED teams.
    Res "difficulty.mirror_comparison" "INFO" ("mirror matches EASY vs HARD: accuracy {0} vs {1}; kills {2} vs {3}; confounded by the targets' own difficulty - a mixed-team test is needed for a verdict" -f $easy.accuracy, $hard.accuracy, $easy.kills, $hard.kills) "Gameplay" }
$mix = @($rows | Where-Object { $_.run -like "*_mix*" -and $null -ne $_.hard_bot_kills })
if ($mix.Count -eq 2) {
    $hk = ($mix | Measure-Object hard_bot_kills -Sum).Sum; $ek = ($mix | Measure-Object easy_bot_kills -Sum).Sum
    Res "difficulty.hard_outkills_easy" $(if ($hk + $ek -lt 6) { "UNKNOWN" } elseif ($hk -gt $ek) { "PASS" } else { "FAIL" }) ("mixed teams, both sides swapped: bot-on-bot kills by HARD {0} vs EASY {1} (A: {2}/{3}, B: {4}/{5}; the scripted player's kills / deaths excluded)" -f $hk, $ek, $mix[0].hard_bot_kills, $mix[0].easy_bot_kills, $mix[1].hard_bot_kills, $mix[1].easy_bot_kills) "Gameplay"
} elseif (-not $H.Contains("WFC_LOBBY_OPTIONS")) { Res "difficulty.hard_outkills_easy" "SKIP" "build has no WFC_LOBBY_OPTIONS (Frontend 2cf1ab1) for the per-faction difficulty test" "Experimental" }
Res "known_partial" "INFO" "not flagged (Gameplay, known PARTIAL): bots never hold vehicle form in combat, jets stay robots, bot abilities unused" "Gameplay"
Write-WfcCsv $rows (Join-Path $OutDir "bots.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("run", "spawned", "teams", "kills", "kill_feed", "bots_logged", "median_bot_path_m", "broken_bots", "stuck_or_idle_bots", "nopath_max", "off_mesh_samples", "accuracy", "suicides", "team_kills", "frame_ms", "loaded_mb", "clean_exit") (Join-Path $OutDir "BOTS.md") "Bot matrix" @("build: ``$sha`` ($Config); difficulty $Difficulty; $Seconds s per run; frontend-launched private matches with Bot Settings in the run's profile.")
"BOT MATRIX: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
