# BOLT COLLISION CHECK (tier TARGETED; Rendering's particle-collision fix d06eec2, map_fx_runtime.json regenerated).
# Rendering's verification run, reproduced: frontend-launched TDM on Streets with 5 + 5 bots, WFC_LOCKSTEP, autofire with
# a slow turn, the player's own view (at spawn it faces a wall a few metres away), screenshot 12 s into play. The weapon is
# whatever character selection gives the player; it was the Scatter Blaster. Before the fix the bolts pass through the
# wall; after it they end on it and read brighter.
# Runs every -Roots tree (e.g. a pre-d06eec2 build as "before" and the current one as "after") and writes a side-by-side.
# A human judges the images (HUMAN): the script records the weapon and ammo, not the visual verdict.
#
#   .\tools\fidelity\bolt-check.ps1 -Roots work\ab\<before>,work\ab\<after> -OutDir <dir> [-Seconds 12]
param([Parameter(Mandatory)][string[]]$Roots, [Parameter(Mandatory)][string]$OutDir, [int]$Seconds = 12, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Roots = @($Roots | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { (Resolve-Path $_.Trim()).Path })
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "bolts.$id" $status $null $note $owner }
$shots = @()
foreach ($root in $Roots) {
    $exe = Join-Path $root "build-release\bin\wfc_rebuild.exe"
    $sha = if (Test-Path (Join-Path $root "M05_TARGET.txt")) { (((Get-Content (Join-Path $root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '').Substring(0, 7) } else { Split-Path $root -Leaf }
    $d = Join-Path $OutDir $sha; New-Item -ItemType Directory -Force $d | Out-Null
    $shot = Join-Path $d "after.bmp"
    if (-not $ReportOnly -and -not (Test-Path $shot)) {
        if (-not (Wait-WfcGpu)) { Res "$sha.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        (Get-BotProfile 5 5 1) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $s = (@((Get-MousePark $root), "wait:frontend", "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
                "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,508", "wait:t=1",
                "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "wait:movie=CustomTransformers", "wait:t=1.5", "ui:Accept", "wait:ui=InGame", "wait:t=$Seconds", "shot:$shot", "wait:t=0.5", "quit")) -join ";"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_FLOW_TIMEOUT = "400"
                WFC_LOCKSTEP = "1"; WFC_AUTOFIRE = "1"; WFC_AUTOTURN = "0.4"; WFC_CHARSELECT = "1"; WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_PERFLOG = "60" }
        $null = Invoke-WfcExe $exe $d $e "run.log" 600
    }
    $lg = Join-Path $d "wfc.log"
    $wpn = if (Test-Path $lg) { @(Select-String $lg -Pattern 'PERF f\d+ .* ammo=(\d+)' | Select-Object -Last 1 | ForEach-Object { $_.Matches[0].Groups[1].Value })[0] } else { $null }
    $have = Test-Path $shot
    Res "$sha.captured" $(if ($have) { "HUMAN" } else { "FAIL" }) ("{0}: screenshot {1}; last ammo {2}; judge: bolts end at the wall (after the fix) vs pass through (before)" -f $sha, $(if ($have) { "taken" } else { "MISSING" }), $wpn) "Rendering"
    if ($have) { $shots += @{ png = $shot; label = $sha } }
}
if ($shots.Count) { New-WfcSheet $shots (Join-Path $OutDir "bolts_side_by_side.png") $shots.Count 640 360 }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"BOLT CHECK: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
