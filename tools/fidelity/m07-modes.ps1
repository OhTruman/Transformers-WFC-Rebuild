# MILESTONE 07 GAME-MODE MATRIX: every mode in the recovered data, judged by ITS rules (no TDM expectations elsewhere).
#
#   .\tools\fidelity\m07-modes.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Config Debug|Release] [-Maps MP_IAC_Streets,MP_IAC_Seed] [-ReportOnly]
#
# Per mode and map, a lockstep direct-boot local match (WFC_MAP + WFC_GAMEMODE + WFC_MATCH, scripted opponent kills
# WFC_LIFECYCLE=3, forward walk):
#   setup        MATCH init mode / rules / teams / time limit / score limit (DM: free-for-all, every other mode: 2 teams)
#   actors       mapstate "mode-dependent actors (V visible)": objective modes show their objective actors, TDM / DM none
#   spawn        start class: DM TnFreeForAllPlayerStart, team modes TnTeamPlayerStart
#   timer        MATCH timer counts down one second per second
#   score        TDM: team score per kill; DM: personal score per kill; objective modes: kills must NOT be the scoring rule
#                (score comes from objectives - Gameplay's WFC_MODEPLAYTEST plays them and is parsed per mode)
#   death / respawn, end condition (score limit reachable by kills only in TDM / DM; objective ends UNKNOWN unless
#                MODEPLAYTEST reaches them)
# Per mode, the frontend path (Debug exe): mode chosen in the party lobby, private match on Streets, Choose Character,
#   play, end (TDM / DM) or quit (objective modes), results screen, lobby return, a SECOND match in the same process.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [ValidateSet("Release", "Debug")][string]$Config = "Debug",
      [string[]]$Maps = @("MP_IAC_Streets", "MP_IAC_Seed"), [switch]$ReportOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Maps = @($Maps | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$relExe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; if (-not (Test-Path $relExe)) { $relExe = $exe }
$H = Get-ExeHooks $exe; $X = Get-M07Expectations
$res = New-WfcResults
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "m07mode.$id" $status $null $note $owner }
$quitBox = [bool](Get-ChildItem (Join-Path $Root "src") -Recurse -Include *.cpp, *.h -ErrorAction SilentlyContinue | Select-String -Pattern "TnQuitMessageBox" -SimpleMatch -List | Select-Object -First 1)
$modes = @($X.modes | ForEach-Object { $_.tag })
$rows = New-Object System.Collections.Generic.List[object]

# ---------- Gameplay's scripted objective play (once): MODEPLAY PASS / FAIL lines grouped by mode
$mp = @{}
if ($H.Contains("WFC_MODEPLAYTEST")) { $d = Join-Path $OutDir "modeplaytest"; New-Item -ItemType Directory -Force $d | Out-Null
    if (-not $ReportOnly -and (Wait-WfcGpu)) { $null = Invoke-WfcExe $relExe $d @{ WFC_MODEPLAYTEST = "1" } "run.log" 900 }
    $cur = ""; if (Test-Path (Join-Path $d "wfc.log")) { foreach ($ln in [IO.File]::ReadLines((Join-Path $d "wfc.log"))) { if ($ln -match 'MODEPLAY (PASS|FAIL) (.*)') { $t = $Matches[2]; foreach ($m in $modes) { if ($t -match "\b$m\b|\($m\)") { $cur = $m } }; if (-not $mp.ContainsKey($cur)) { $mp[$cur] = @() }; $mp[$cur] += "$($Matches[1]) $t" } } } }

foreach ($mode in $X.modes) {
    foreach ($map in $Maps) {
        $mapX = @($X.maps | Where-Object { $_.runtime -eq $map })[0]
        if (-not $mapX -or $mapX.modes -notcontains $mode.tag) { Res "$($mode.tag).$map" "SKIP" "$map does not offer $($mode.tag)" ""; continue }
        $d = Join-Path $OutDir "direct_$($mode.tag)_$map"; New-Item -ItemType Directory -Force $d | Out-Null
        if (-not $ReportOnly -and (Wait-WfcGpu)) { $null = Invoke-WfcExe $relExe $d @{ WFC_BOOT = "match"; WFC_MAP = $map; WFC_GAMEMODE = $mode.tag; WFC_MATCH = $mode.tag; WFC_LIFECYCLE = "3"; WFC_LOCKSTEP = "1"; WFC_SMOKE_FRAMES = "3600"; WFC_LOGEVERY = "60"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_NOMOUSE = "1" } "run.log" 600 }
        $log = Join-Path $d "wfc.log"; if (-not (Test-Path $log)) { Res "$($mode.tag).$map.direct" "SKIP" "no run" "Experimental"; continue }
        $init = @(Grep-Log $log '\] MATCH init ')[0]; $ms = @(Grep-Log $log 'mapstate: mode \S+, .*mode-dependent actors \((\d+) visible\)')[0]
        $spawns = @(Grep-Log $log '\] MATCH spawn ' | ForEach-Object { [regex]::Match($_.text, 'start=(\S+?)_\d+').Groups[1].Value })
        $timer = @(Grep-Log $log '\] MATCH timer remaining_s=(\d+)' | ForEach-Object { [int][regex]::Match($_.text, 'remaining_s=(\d+)').Groups[1].Value })
        $kills = @(Grep-Log $log '\] MATCH kill '); $scores = @(Grep-Log $log '\] MATCH score '); $deaths = @(Grep-Log $log '\] MATCH death ')
        $resp = @(Grep-Log $log '\] MATCH respawn ' | ForEach-Object { [double][regex]::Match($_.text, 'delay_s=([\d.]+)').Groups[1].Value }); $end = @(Grep-Log $log '\] MATCH end ')[0]
        $teams = if ($init) { [int][regex]::Match($init.text, 'teams=(\d+)').Groups[1].Value } else { -1 }
        $visible = if ($ms) { [int][regex]::Match($ms.text, '\((\d+) visible\)').Groups[1].Value } else { -1 }
        $objMode = $mode.objective_actors.Count -gt 0
        $r = [ordered]@{ mode = $mode.tag; map = $map
            setup = $(if (-not $init) { "FAIL" } elseif ($init.text -notmatch "mode=$($mode.tag)\b") { "FAIL" } elseif ((-not $mode.team -and $teams -gt 1) -or ($mode.team -and $teams -ne 2)) { "FAIL" } else { "PASS" })
            actors = $(if ($visible -lt 0) { "UNKNOWN" } elseif ($objMode -and $visible -eq 0) { "FAIL" } elseif (-not $objMode -and $visible -gt 0) { "FAIL" } else { "PASS" })
            spawn = $(if (-not $spawns.Count) { "FAIL" } elseif (@($spawns | Where-Object { $_ -ne $mode.start_class }).Count) { "FAIL" } else { "PASS" })
            timer = $(if ($timer.Count -ge 3 -and $timer[0] -gt $timer[-1]) { "PASS" } elseif ($timer.Count) { "PARTIAL" } else { "FAIL" })
            score = ""; death_respawn = $(if ($deaths.Count -and $resp.Count) { "PASS" } elseif ($kills.Count) { "FAIL" } else { "UNKNOWN" })
            end = ""; note = "" }
        if ($mode.tag -eq "TDM") { $r.score = if (@($scores | Where-Object { $_.text -match 'team=[01] score=[1-9]' }).Count) { "PASS" } else { "FAIL" }; $r.end = if ($end -and $end.text -match 'score_limit') { "PASS" } else { "FAIL" } }
        elseif ($mode.tag -eq "DM") { $r.score = if (@($scores | Where-Object { $_.text -match 'team=-1 player=\d+ score=[1-9]' }).Count) { "PASS" } else { "FAIL" }; $r.end = if ($end -and $end.text -match 'score_limit') { "PASS" } else { "FAIL" } }
        else { $kTeamScore = @($scores | Where-Object { $_.text -match 'team=[01] score=[1-9]' }).Count
            $mpl = @($mp[$mode.tag]); $r.score = if ($mpl.Count -and -not @($mpl | Where-Object { $_ -like "FAIL*" }).Count) { "PASS" } elseif ($mpl.Count) { "FAIL" } elseif ($kTeamScore -gt 0 -and $kills.Count -and $kTeamScore -ge $kills.Count) { "FAIL" } else { "UNKNOWN" }
            $r.end = if ($end) { "INFO" } else { "UNKNOWN" } }
        $r.note = "init: {0}; mode actors visible {1}; spawn classes {2}; timer {3} -> {4}; kills {5}, deaths {6}, respawn {7}; end {8}; MODEPLAY {9}" -f $(if ($init) { $init.text -replace '^.*MATCH init ', '' } else { "none" }), $visible, (($spawns | Select-Object -Unique) -join ","), $(if ($timer.Count) { $timer[0] }), $(if ($timer.Count) { $timer[-1] }), $kills.Count, $deaths.Count, (($resp | Select-Object -First 3 | ForEach-Object { "{0:N2}" -f $_ }) -join ","), $(if ($end) { $end.text -replace '^.*MATCH end ', '' } else { "none" }), (@($mp[$mode.tag]) -join " | ")
        $rows.Add([pscustomobject]$r)
        foreach ($c in "setup", "actors", "spawn", "timer", "score", "death_respawn", "end") { Res "$($mode.tag).$map.$c" $r[$c] $r.note $(if ($c -in "actors") { "Gameplay/Rendering" } else { "Gameplay" }) }
    }
    # ---------- frontend: mode chosen in the menus, results, lobby return, second match
    $d = Join-Path $OutDir "frontend_$($mode.tag)"; New-Item -ItemType Directory -Force $d | Out-Null
    $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
    $versus = $mode.tag -in "TDM", "DM"
    $one = { param($n) $endPart = if ($versus) { "wait:ui=GameEnded;wait:t=2;shot:$d\${n}3_results.bmp;snapshot:${n}results;wait:level=GameLobby;wait:ui=InLobby;wait:t=2;shot:$d\${n}4_lobby.bmp;snapshot:${n}lobby" } else { "wait:t=12;shot:$d\${n}2b_play.bmp;showmenu;wait:ui=Paused;wait:t=0.5;call:Game.QuitToMainMenu;wait:t=1.5;" + $(if ($quitBox) { "ui:Accept;wait:level=PartyLobby;wait:ui=InLobby;wait:t=2;shot:$d\${n}4_party.bmp;snapshot:${n}lobby" } else { "wait:level=FrontEnd;wait:t=2;shot:$d\${n}4_frontend.bmp;snapshot:${n}lobby" }) }
        "call:Online.SetSelectedMapID,508;wait:t=1.5;shot:$d\${n}0_lobby.bmp;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:t=2;shot:$d\${n}1_spawn.bmp;wait:t=3;shot:$d\${n}2_play.bmp;$endPart" }
    $toLobby = "call:Online.OpenPartyLobby,$(if ($mode.team) { 'GTS_TeamGame' } else { 'GTS_FreeForAllGame' });wait:level=PartyLobby;wait:ui=InLobby;wait:t=1.5;call:Online.EditGameMode,$($mode.tag);call:Online.PlayPrivateGame,$($mode.tag);wait:level=GameLobby;wait:ui=InLobby;wait:t=2"
    $second = if ($versus) { & $one "b" } else { "$toLobby;" + (& $one "b") }
    $s = @("wait:frontend", "wait:ui=FrontEnd", "wait:t=2", $toLobby, (& $one "a"), $second, "quit") -join ";"
    $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "900"; WFC_LIFECYCLE = "2"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "60" }
    if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
    if (-not $ReportOnly -and (Wait-WfcGpu)) { $null = Invoke-WfcSampled $exe $d $e 1200 1.0 }
    $F = Read-FlowLog (Join-Path $d "flow.jsonl"); $snaps = @(Flow-Ev $F "snapshot" | ForEach-Object { $_.why })
    $gm = @(Flow-Ev $F "match.gameplay" | ForEach-Object { $_.mode })
    $ws = @("a1_spawn", "a2_play", "b1_spawn", "b2_play" | ForEach-Object { Present-World "$d\$_.bmp" } | Where-Object { $_ })
    $resultsOk = $(if ($versus) { $snaps -contains "aresults" -and @(Flow-Ev $F "gfx.movie" | Where-Object { $_.movie -like "*EndGameStats*" }).Count -gt 0 } else { $null })
    $fr = [ordered]@{ mode = $mode.tag; frontend_setup = $(if ($gm.Count -and @($gm | Where-Object { $_ -ne $mode.tag }).Count -eq 0) { "PASS" } elseif ($gm.Count) { "FAIL" } else { "FAIL" })
        results = $(if ($resultsOk -eq $null) { "UNKNOWN" } elseif ($resultsOk) { "PASS" } else { "FAIL" }); lobby_return = $(if ($snaps -contains "alobby") { "PASS" } else { "FAIL" })
        second_match = $(if ($snaps -contains "blobby" -or (Test-Path "$d\b2_play.bmp")) { "PASS" } else { "FAIL" }); world = (Present-WorldSetVerdict $ws) }
    foreach ($c in "frontend_setup", "results", "lobby_return", "second_match", "world") { Res "$($mode.tag).frontend.$c" $fr[$c] ("Gameplay modes launched {0}; snapshots {1}; world detail {2}" -f ($gm -join ","), ($snaps -join ","), (($ws | ForEach-Object { $_.detail }) -join "/")) $(if ($c -eq "world") { "Rendering/Frontend" } else { "Frontend/Gameplay" }) }
    foreach ($row in @($rows | Where-Object { $_.mode -eq $mode.tag })) { foreach ($k in $fr.Keys) { if ($k -ne "mode") { $row | Add-Member -Force -NotePropertyName $k -NotePropertyValue $fr[$k] } } }
    $tiles = @(Get-ChildItem $d -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = "$($mode.tag) $($_.BaseName)" } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_$($mode.tag).png") 4 400 225 }
}
Res "SV.blocked" "INFO" "Escalation (SV) needs 2 players (authored autostart) and Survival is not implemented: mode blocked, maps BrokenHope / Remnant runtime-capable (see m07-maps)" "Gameplay"
Write-WfcCsv $rows (Join-Path $OutDir "modes.csv")
Write-M07Matrix $rows @("mode", "map", "setup", "actors", "spawn", "timer", "score", "death_respawn", "end", "frontend_setup", "results", "lobby_return", "second_match", "world") (Join-Path $OutDir "MODES.md") "M07 game-mode matrix" @("exe: ``$exe`` (frontend) / ``$relExe`` (direct)", "", "Each mode is judged by its own rules; objective-mode ends that the test cannot drive are UNKNOWN, not PASS. Notes: modes.csv; sheets: sheet_<mode>.png (mode presentation = human check).")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M07 MODES: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
