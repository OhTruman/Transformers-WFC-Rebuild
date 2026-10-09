# FILELOG PASS (tier TARGETED; Integration 2026-10-09: verify the slim package manifest against what the game really opens).
# One full flow per map with Systems' WFC_FILELOG=<file> (every file the game's code opens, deduplicated): frontend -> party lobby
# -> Create-a-Character (in and out) -> game lobby -> TDM match on the map (10 v 10, short) -> results screen -> back to the lobby.
# Untimed; GPU-gated (Wait-WfcGpu honours PERF_HOLD). Output: <OutDir>\filelog_<map>.txt + the run dir per map.
#
#   .\tools\fidelity\filelog-pass.ps1 -Root work\ab\<target> -OutDir <dir> [-Maps 501,502,...] [-TimeLimit 45]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir,
      [int[]]$Maps = @(501, 502, 503, 504, 507, 508, 509, 510), [int]$TimeLimit = 45, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; $H = Get-ExeHooks $exe
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "filelog.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_FILELOG")) { Res "hook" "SKIP" "build has no WFC_FILELOG (Systems 37bafe7, 09c-next)" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
foreach ($map in $Maps) {
    $d = Join-Path $OutDir "run_$map"; New-Item -ItemType Directory -Force $d | Out-Null
    $fl = Join-Path $OutDir "filelog_$map.txt"; $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $fl)) {
        if (-not (Wait-WfcGpu)) { Res "$map.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        (Get-BotProfile 0 0 1) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        # CaC: the leak sweep's verified frontend cycle (customCharacters clip, a body pick, back out to the party lobby)
        $s = (@((Get-MousePark $Root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:t=3",
                "clickclip:lobby_mc.menuAnchor_mc.menu_mc.customCharacters_mc", "wait:t=3", "ui:Accept", "wait:t=5", "navcheck:cac", "ui:Down", "wait:t=0.5", "ui:Down", "wait:t=0.5", "ui:Accept", "wait:t=3",
                "ui:Down", "wait:t=1.5", "ui:Back", "wait:t=2", "ui:Back", "wait:t=2", "ui:Back", "wait:t=2", "ui:Back", "wait:t=5",
                "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5",
                "call:Online.SetSelectedMapID,$map", "wait:t=1", "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame",
                "wait:ui=GameEnded", "wait:t=10", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=3", "quit") -join ";") -replace ';;', ';'
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "$(2 * $TimeLimit + 600)"
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_FILELOG = $fl; WFC_LOBBY_OPTIONS = "PointsToWin=9999;TimeLimit=$TimeLimit" }
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        if ($H.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "1" }
        $null = Invoke-WfcExe $exe $d $e "run.log" (2 * $TimeLimit + 900)
    }
    if (-not (Test-Path $lg)) { Res "$map" "UNKNOWN" "not run" "Experimental"; continue }
    $t = Get-Content -Raw $lg
    $lvl = [regex]::Match($t, 'FLOW gamelobby\.startLevel url=(\S+)'); # CaC opens INSIDE the party-lobby movie (openMovie stays PartyLobby): judge by the CaC preview (nav.check preview=a/b/c/meshes/bodies)
    $cm = [regex]::Match($t, 'nav\.check label=cac .*?preview=(\d+)/(\d+)/(\d+)/(\d+)/(\d+)'); $cac = $cm.Success -and [int]$cm.Groups[5].Value -gt 0
    $cacNote = if ($cm.Success) { "preview bodies $($cm.Groups[5].Value)" } else { "no nav.check" }
    $ended = $t -match 'to=GameEnded'; $back = [regex]::Matches($t, 'level=GameLobby|to=GameLobby').Count -ge 2
    $n = if (Test-Path $fl) { @(Get-Content $fl | Where-Object { $_.Trim() }).Count } else { 0 }
    $ok = $n -gt 0 -and $lvl.Success -and $ended -and $back -and $cac
    Res "$map" $(if ($ok) { "PASS" } elseif ($n -gt 0) { "PARTIAL" } else { "FAIL" }) ("{0} paths in {1}; startLevel {2}; CaC {3} ({6}); results screen {4}; back to lobby {5}" -f $n, $fl, $(if ($lvl.Success) { $lvl.Groups[1].Value.Split('?')[0] } else { "-" }), $cac, $ended, $back, $cacNote) "Experimental"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"FILELOG PASS: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
