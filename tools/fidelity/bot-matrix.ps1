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
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("default", "quick")][string]$Matrix = "default",
      [int]$Seconds = 60, [int]$Difficulty = 1, [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "bots.$id" $status $null $note $owner }
$mapId = @{ MP_IAC_Streets = 508; MP_IAC_Berth = 502; MP_UND_Gorge = 510; MP_IAC_Seed = 501 }
$runs = @(@{ mode = "TDM"; map = "MP_IAC_Streets"; f = 0; e = 1 }, @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 2; e = 3 }, @{ mode = "TDM"; map = "MP_IAC_Streets"; f = 7; e = 8 })
if ($Matrix -eq "default") {
    $runs += @("DOM", "KOTH", "CTF", "EXT" | ForEach-Object { @{ mode = $_; map = "MP_IAC_Streets"; f = 3; e = 4 } })
    $runs += @("MP_IAC_Berth", "MP_UND_Gorge" | ForEach-Object { @{ mode = "TDM"; map = $_; f = 3; e = 4 } })
}
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
$rows = New-Object System.Collections.Generic.List[object]
foreach ($r in $runs) {
    $tag = "{0}_{1}_{2}v{3}" -f $r.mode, ($r.map -replace '^MP_', ''), $r.f, $r.e; $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$tag.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        "[PCSettings]`nWidth=1280`nHeight=720`nFullscreen=0`nBotsFriendly=$($r.f)`nBotsEnemy=$($r.e)`nBotDifficulty=$Difficulty`n" | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $party = if ($r.mode -eq "DM") { "GTS_FreeForAllGame" } else { "GTS_TeamGame" }
        $s = @("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,$party", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
               "call:Online.EditGameMode,$($r.mode)", "call:Online.PlayPrivateGame,$($r.mode)", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,$($mapId[$r.map])", "wait:t=1",
               "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:t=$Seconds", "quit") -join ";"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "400";
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_BOTLOG = "all"; WFC_PERFLOG = "60"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        $null = Invoke-WfcExe $exe $d $e "run.log" ($Seconds + 240)
    }
    if (-not (Test-Path $lg)) { continue }
    $lines = @([IO.File]::ReadLines($lg)); $clean = [bool]($lines | Where-Object { $_ -match 'Shutdown complete' } | Select-Object -First 1)
    $spawns = @($lines | Where-Object { $_ -match '\] MATCH spawn player=(\d+) team=(-?\d+)' } | ForEach-Object { $m = [regex]::Match($_, 'player=(\d+) team=(-?\d+)'); [pscustomobject]@{ p = [int]$m.Groups[1].Value; team = [int]$m.Groups[2].Value } })
    $players = @($spawns | Group-Object p | ForEach-Object { [pscustomobject]@{ p = [int]$_.Name; team = $_.Group[0].team } })
    $kills = @($lines | Where-Object { $_ -match '\] MATCH kill ' }).Count
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $feed = @(Flow-Ev $F "hud.killFeed").Count
    $bl = @($lines | Where-Object { $_ -match '\] BOTLOG ' } | ForEach-Object { $m = [regex]::Match($_, 'BOTLOG (\S+) p(\d+) \(([-\d.]+) ([-\d.]+) ([-\d.]+)\).* stuck (\d+) .* nopath (\d+)')
        if ($m.Success) { [pscustomobject]@{ p = [int]$m.Groups[2].Value; x = [double]$m.Groups[3].Value; y = [double]$m.Groups[4].Value; z = [double]$m.Groups[5].Value; stuck = [int]$m.Groups[6].Value; nopath = [int]$m.Groups[7].Value } } })
    $nav = @($bl | Group-Object p | ForEach-Object { $g = @($_.Group); $dist = 0.0; for ($i = 1; $i -lt $g.Count; $i++) { $dx = $g[$i].x - $g[$i - 1].x; $dz = $g[$i].z - $g[$i - 1].z; $step = [Math]::Sqrt($dx * $dx + $dz * $dz); if ($step -lt 30) { $dist += $step } }
        [pscustomobject]@{ p = [int]$_.Name; samples = $g.Count; dist = [Math]::Round($dist, 1); stuckFrac = [Math]::Round(@($g | Where-Object { $_.stuck -ge 2 }).Count / [Math]::Max(1, $g.Count), 2); nopath = ($g | Measure-Object nopath -Maximum).Maximum } })
    $perf = @($lines | Where-Object { $_ -match 'PERF f\d+ frame=([\d.]+)ms' } | ForEach-Object { [double]([regex]::Match($_, 'frame=([\d.]+)ms').Groups[1].Value) } | Select-Object -Skip 2)
    $frameMs = if ($perf.Count) { [Math]::Round(($perf | Measure-Object -Average).Average, 2) } else { $null }
    $want = $r.f + $r.e + 1
    $local = @($players | Sort-Object p | Select-Object -First 1)[0]
    $teamOk = if ($r.mode -eq "DM" -or -not $local) { $true } else { $fr = @($players | Where-Object { $_.p -ne $local.p -and $_.team -eq $local.team }).Count; $en = @($players | Where-Object { $_.team -ne $local.team }).Count; ($fr -eq $r.f -and $en -eq $r.e) }
    $stuckBots = @($nav | Where-Object { $_.stuckFrac -gt 0.25 -or $_.dist -lt 10 }); $noPathMax = ($nav | Measure-Object nopath -Maximum).Maximum
    $rows.Add([pscustomobject][ordered]@{ run = $tag; spawned = "$($players.Count)/$want"; teams = $teamOk; kills = $kills; kill_feed = $feed; bots_logged = $nav.Count
        median_bot_path_m = $(if ($nav.Count) { ($nav | ForEach-Object { $_.dist } | Sort-Object)[[int]($nav.Count / 2)] }); stuck_or_idle_bots = $stuckBots.Count; nopath_max = $noPathMax; frame_ms = $frameMs; clean_exit = $clean })
    Res "$tag.spawned" $(if ($players.Count -eq $want -and $teamOk) { "PASS" } elseif ($players.Count) { "FAIL" } else { "UNKNOWN" }) ("distinct spawned players {0} of {1} (local + {2} friendly + {3} enemy); team split correct {4}" -f $players.Count, $want, $r.f, $r.e, $teamOk) "Gameplay"
    if ($r.e -ge 1) { Res "$tag.combat" $(if ($kills -gt 0) { "PASS" } else { "FAIL" }) ("{0} kills in {1} s of play" -f $kills, $Seconds) "Gameplay" }
    Res "$tag.navigation" $(if (-not $nav.Count) { "UNKNOWN" } elseif ($stuckBots.Count -gt [Math]::Max(1, [int]($nav.Count * 0.2))) { "FAIL" } elseif ($stuckBots.Count) { "PARTIAL" } else { "PASS" }) ("{0} bots logged; median path {1} m; stuck (> 25 % of samples) or idle (< 10 m) bots: {2}; max no-path count {3}" -f $nav.Count, $rows[-1].median_bot_path_m, $(if ($stuckBots.Count) { ($stuckBots | ForEach-Object { "p$($_.p) $($_.dist) m stuck $($_.stuckFrac)" }) -join ", " } else { "none" }), $noPathMax) "Gameplay"
    if ($kills -gt 0) { Res "$tag.kill_feed" $(if ($feed -eq $kills) { "PASS" } else { "FAIL" }) ("kill-feed lines {0} vs kills {1}" -f $feed, $kills) "Frontend" }
    Res "$tag.frame_time" $(if ($frameMs -eq $null) { "UNKNOWN" } elseif ($frameMs -gt 16.7) { "PARTIAL" } else { "INFO" }) ("mean frame {0} ms with {1} players" -f $frameMs, $want) "Gameplay/Rendering"
    Res "$tag.clean_exit" $(if ($clean) { "PASS" } else { "FAIL" }) ("shutdown complete {0}" -f $clean) "Integration"
}
Write-WfcCsv $rows (Join-Path $OutDir "bots.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("run", "spawned", "teams", "kills", "kill_feed", "bots_logged", "median_bot_path_m", "stuck_or_idle_bots", "nopath_max", "frame_ms", "clean_exit") (Join-Path $OutDir "BOTS.md") "Bot matrix" @("build: ``$sha`` ($Config); difficulty $Difficulty; $Seconds s per run; frontend-launched private matches with Bot Settings in the run's profile.")
"BOT MATRIX: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
