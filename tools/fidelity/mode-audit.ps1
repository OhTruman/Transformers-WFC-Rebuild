# MODE AUDIT (tier TARGETED; Milestone E "complete offline multiplayer"). Per game mode, ONE process: private match with
# bots (Bot Settings in the run's profile) -> start -> scoring -> end -> results -> back to the game lobby -> a SECOND match
# of the same mode -> end -> lobby -> quit. The map is re-selected before every match (the authored GameLobby_GFX script
# resets the selection to the first compatible map whenever the lobby reopens).
# Per match, from the flow trace and the MATCH protocol lines:
#   loaded     match.loaded with the expected mode and map
#   in_game    the InGame UI state was reached and the local player spawned
#   scoring    MATCH score lines (team / player score rising); objective modes without objective score lines -> INFO
#   end        MATCH end reason (score_limit expected with the shortened goal; time_limit -> PARTIAL; none -> FAIL)
#   results    GameEnded UI + the EndGameStats movie opened
#   saved      progression.match end ... saved=1
#   lobby      match.unloaded, then back in the game lobby (InLobby)
# Run level: both matches completed, clean exit. Goal per mode via WFC_LIFECYCLE (TEST ONLY score-limit override).
#
#   .\tools\fidelity\mode-audit.ps1 -Root work\ab\<target> -OutDir <dir> [-Modes TDM,DM,DOM,KOTH,CTF,EXT] [-MapId 508] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Modes = @("TDM", "DM", "DOM", "KOTH", "CTF", "EXT"),
      [int]$MapId = 508, [hashtable]$Goals = @{ TDM = 10; DM = 10; DOM = 0; KOTH = 0; CTF = 0; EXT = 0 }, [int]$MatchTimeoutS = 420, [switch]$AuthoredGoals,
      # short goals through the lobby host options (Frontend WFC_LOBBY_OPTIONS, TEST ONLY; values from Gameplay 2026-10-06)
      [hashtable]$LobbyOptions = @{ TDM = "PointsToWin=10"; DM = "PointsToWin=10"; DOM = "PointsToWin=40"; KOTH = "PointsToWin=40"; CTF = "PointsToWin=2;TimeLimit=60"; EXT = "PointsToWin=1;TimeLimit=180" },
      [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "mode.$id" $status $null $note $owner }
$objective = @("DOM", "KOTH", "CTF", "EXT")
$rows = New-Object System.Collections.Generic.List[object]
$Modes = @($Modes | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })   # -File passes "A,B" as one string
foreach ($mode in $Modes) {
    # Objective modes need short goals through the lobby host options (PointsToWin / TimeLimit; Gameplay: WFC_LIFECYCLE skews
    # objective scoring). Until a script hook exists they would run to the authored goal (DOM / KOTH 400): skip unless asked.
    $useLobbyOpts = $H.Contains("WFC_LOBBY_OPTIONS") -and -not $AuthoredGoals -and $LobbyOptions[$mode]
    if ($objective -contains $mode -and -not $AuthoredGoals -and -not $useLobbyOpts -and [int]$Goals[$mode] -le 0) {
        Res "$mode" "SKIP" "objective mode needs short lobby goals (PointsToWin / TimeLimit host options; asked Frontend for a script hook) - run with -AuthoredGoals to play the authored goal" "Experimental"; continue }
    $d = Join-Path $OutDir $mode; New-Item -ItemType Directory -Force $d | Out-Null
    $lg = Join-Path $d "wfc.log"; $fl = Join-Path $d "flow.jsonl"
    $team = $mode -ne "DM"; $f = if ($team) { 3 } else { 0 }; $e = if ($team) { 4 } else { 7 }
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$mode.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        "[PCSettings]`nWidth=1280`nHeight=720`nFullscreen=0`nBotsFriendly=$f`nBotsEnemy=$e`nBotDifficulty=1`n" | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $party = if ($team) { "GTS_TeamGame" } else { "GTS_FreeForAllGame" }
        $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
        $one = "call:Online.SetSelectedMapID,$MapId;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:ui=GameEnded;wait:t=4;wait:level=GameLobby;wait:ui=InLobby;wait:t=3"
        $s = (@("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,$party", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
                "call:Online.EditGameMode,$mode", "call:Online.PlayPrivateGame,$mode", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", $one, $one, "quit")) -join ";"
        $env2 = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = $fl; WFC_FLOW_TIMEOUT = "$(2 * $MatchTimeoutS + 200)";
                   WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
        if ($useLobbyOpts) { $env2.WFC_LOBBY_OPTIONS = $LobbyOptions[$mode] }          # real Score / Time end paths
        elseif ([int]$Goals[$mode] -gt 0) { $env2.WFC_LIFECYCLE = "$($Goals[$mode])" }  # fallback: builds without the lobby hook
        if ($H.Contains("WFC_CHARSELECT")) { $env2.WFC_CHARSELECT = "1" }
        $null = Invoke-WfcExe $exe $d $env2 "run.log" (2 * $MatchTimeoutS + 300)
    }
    if (-not (Test-Path $lg)) { continue }
    $lines = @([IO.File]::ReadLines($lg)); $clean = [bool]($lines | Where-Object { $_ -match 'Shutdown complete' } | Select-Object -First 1)
    # split the log into matches at each "MATCH init"
    $seg = @(); $cur = $null
    # (null checks, not truthiness: an empty List is falsy in PowerShell)
    foreach ($l in $lines) { if ($l.Contains('] MATCH init ')) { if ($null -ne $cur) { $seg += , $cur }; $cur = New-Object System.Collections.Generic.List[string] }; if ($null -ne $cur) { $cur.Add($l) } }
    if ($null -ne $cur) { $seg += , $cur }
    for ($k = 0; $k -lt 2; $k++) {
        $tag = "$mode.m$($k + 1)"
        if ($k -ge $seg.Count) { Res "$tag" "FAIL" ("match {0} of 2 never started (MATCH init missing){1}" -f ($k + 1), $(if ($k -eq 1) { " - the return to the lobby or the relaunch failed" } else { "" })) "Frontend/Gameplay"; continue }
        $L = $seg[$k]
        $init = @($L | Where-Object { $_ -match '\] MATCH init ' })[0]
        $scoreLimit = [regex]::Match($init, 'score_limit=(-?\d+)').Groups[1].Value; $timeLimit = [regex]::Match($init, 'time_limit_s=(-?\d+)').Groups[1].Value
        $initMode = [regex]::Match($init, 'mode=(\S+)').Groups[1].Value
        $spawned = [bool](@($L | Where-Object { $_ -match '\] MATCH spawn player=0 ' }).Count)
        $inGame = [bool](@($L | Where-Object { $_ -match 'FLOW ui.state from=\S+ to=InGame' }).Count)
        $scores = @($L | Where-Object { $_ -match '\] MATCH score ' })
        $endL = @($L | Where-Object { $_ -match '\] MATCH end ' })[0]
        $reason = if ($endL) { [regex]::Match($endL, 'reason=(\S+)').Groups[1].Value } else { "" }
        $winner = if ($endL) { [regex]::Match($endL, 'winner=(\S+)').Groups[1].Value } else { "" }
        $endT = if ($endL) { [regex]::Match($endL, ' t=(\d+)').Groups[1].Value } else { "" }
        $results = [bool](@($L | Where-Object { $_ -match 'to=GameEnded' }).Count) -and [bool](@($L | Where-Object { $_ -match 'EndGameStats_GFX_1 opened=true' }).Count)
        $saved = [bool](@($L | Where-Object { $_ -match 'progression.match end=1 .*saved=1' }).Count)
        $unl = [bool](@($L | Where-Object { $_ -match 'FLOW match.unloaded' }).Count)
        $lobby = [bool](@($L | Where-Object { $_ -match 'FLOW ui.state .*to=InLobby .*level=GameLobby' }).Count)
        $kills = @($L | Where-Object { $_ -match '\] MATCH kill ' }).Count
        $rows.Add([pscustomobject][ordered]@{ mode = $mode; match = $k + 1; init_mode = $initMode; score_limit = $scoreLimit; time_limit = $timeLimit; in_game = ($inGame -and $spawned); kills = $kills; score_lines = $scores.Count
            last_score = $(if ($scores.Count) { ($scores[-1] -replace '^.*MATCH score ', '') }); end_reason = $reason; winner = $winner; end_t = $endT; results = $results; saved = $saved; lobby = ($unl -and $lobby) })
        Res "$tag.start" $(if ($initMode -eq $mode -and $inGame -and $spawned) { "PASS" } else { "FAIL" }) ("MATCH init mode={0}; InGame {1}; local player spawned {2}" -f $initMode, $inGame, $spawned) "Frontend/Gameplay"
        $scSt = if ($scores.Count) { "PASS" } elseif ($objective -contains $mode) { "INFO" } else { "FAIL" }
        Res "$tag.scoring" $scSt ("{0} MATCH score lines (last: {1}); {2} kills{3}" -f $scores.Count, $rows[-1].last_score, $kills, $(if ($objective -contains $mode -and -not $scores.Count) { "; objective score changes are not logged by this build - judged by the end reason" } else { "" })) "Gameplay"
        # CTF / EXT may legitimately end on time (round / time cap); elsewhere the shortened score limit is the expected end
        $endOk = ($reason -eq "score_limit") -or (@("CTF", "EXT") -contains $mode -and $reason -eq "time_limit")
        Res "$tag.end" $(if ($endOk) { "PASS" } elseif ($reason) { "PARTIAL" } else { "FAIL" }) ("MATCH end reason={0} winner={1} t={2}s" -f $(if ($reason) { $reason } else { "(none)" }), $winner, $endT) "Gameplay"
        Res "$tag.results" $(if ($results) { "PASS" } else { "FAIL" }) ("GameEnded + EndGameStats movie opened: {0}" -f $results) "Frontend"
        Res "$tag.saved" $(if ($saved) { "PASS" } else { "FAIL" }) ("progression.match end saved=1: {0}" -f $saved) "Frontend"
        Res "$tag.lobby" $(if ($unl -and $lobby) { "PASS" } else { "FAIL" }) ("match.unloaded {0}; back in the game lobby {1}" -f $unl, $lobby) "Frontend"
    }
    Res "$mode.clean_exit" $(if ($clean) { "PASS" } else { "FAIL" }) ("shutdown complete {0}" -f $clean) "Integration"
}
Write-WfcCsv $rows (Join-Path $OutDir "modes.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("mode", "match", "init_mode", "score_limit", "time_limit", "in_game", "kills", "score_lines", "last_score", "end_reason", "winner", "end_t", "results", "saved", "lobby") (Join-Path $OutDir "MODES.md") "Mode audit" @("build: ``$sha`` ($Config); map id $MapId; bots 3v4 (DM 0v7); two matches per mode in one process; goals (WFC_LIFECYCLE, TEST ONLY): $(($Goals.Keys | ForEach-Object { "$_ $($Goals[$_])" }) -join ', ').")
"MODE AUDIT: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
