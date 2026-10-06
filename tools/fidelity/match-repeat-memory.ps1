# REPEATED-MATCH MEMORY (tier TARGETED; M09 "process memory rises match after match with bots"). ONE process, the SAME map
# and mode -Matches times: private match -> play to the WFC_LIFECYCLE goal (match end) -> back to the game lobby -> again.
# Bot Settings come from the run's own profile ([PCSettings] BotsFriendly / BotsEnemy), as a player sets them; -Control
# runs the same flow with no bots. Per match, from the flow trace and the log:
#   loaded_mb     match.loaded privateMB (map + bots loaded)
#   unloaded_mb   match.unloaded privateMB (back in the lobby, map released)
#   pcm_loaded / pcm_unloaded   Systems audio state (audio.loaded / audio.unloaded pcmMB)
#   textures_released           Rendering "released map render data (... N textures)"
# Classification: matches 2..N slope of unloaded_mb (MB per match). The first match pays one-time caches, so it is
# excluded. GROWTH if the slope is > -GrowthMb per match AND the last match is above the second by more than
# 2 x GrowthMb; else PLATEAU. Same test for pcm_unloaded (Systems) and texture count drift (Rendering).
#
#   .\tools\fidelity\match-repeat-memory.ps1 -Root work\ab\<target> -OutDir <dir> [-Matches 6] [-Friendly 3] [-Enemy 4] [-Goal 10] [-Control] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Matches = 6, [int]$Friendly = 3, [int]$Enemy = 4,
      [int]$Goal = 10, [int]$MapId = 508, [double]$GrowthMb = 15, [switch]$Control, [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; $tagRun = if ($Control) { "nobots" } else { "bots_${Friendly}v$Enemy" }
function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "memrepeat.$tagRun.$id" $status $null $note $owner }
$f = if ($Control) { 0 } else { $Friendly }; $e = if ($Control) { 0 } else { $Enemy }
$lg = Join-Path $OutDir "wfc.log"; $fl = Join-Path $OutDir "flow.jsonl"
if (-not $ReportOnly -and -not (Test-Path $lg)) {
    if (-not (Wait-WfcGpu)) { Res "gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
    "[PCSettings]`nWidth=1280`nHeight=720`nFullscreen=0`nBotsFriendly=$f`nBotsEnemy=$e`nBotDifficulty=1`n" | Set-Content -Encoding ASCII (Join-Path $OutDir "wfc_profile.ini")
    $cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
    $one = "call:Online.BeginLobbyExitCountdown;wait:level=Match;${cs}wait:ui=InGame;wait:ui=GameEnded;wait:t=3;wait:level=GameLobby;wait:ui=InLobby;wait:t=3"
    $s = (@("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
            "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,$MapId", "wait:t=1") +
          @(1..$Matches | ForEach-Object { $one }) + @("quit")) -join ";"
    $env2 = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = $fl; WFC_FLOW_TIMEOUT = "3000";
               WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_LIFECYCLE = "$Goal"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
    if ($H.Contains("WFC_CHARSELECT")) { $env2.WFC_CHARSELECT = "1" }
    $null = Invoke-WfcExe $exe $OutDir $env2 "run.log" (300 + 240 * $Matches)
}
if (-not (Test-Path $fl)) { Res "ran" "UNKNOWN" "no flow trace" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$F = Read-FlowLog $fl
$ld = @(Flow-Ev $F "match.loaded"); $ul = @(Flow-Ev $F "match.unloaded"); $al = @(Flow-Ev $F "audio.loaded"); $au = @(Flow-Ev $F "audio.unloaded")
$tx = if (Test-Path $lg) { @(Select-String $lg -Pattern 'released map render data \(.*, (\d+) textures\)' | ForEach-Object { [int]$_.Matches[0].Groups[1].Value }) } else { @() }
$n = $ul.Count
$rows = New-Object System.Collections.Generic.List[object]
for ($i = 0; $i -lt [Math]::Max($ld.Count, $n); $i++) {
    $rows.Add([pscustomobject][ordered]@{ match = $i + 1; loaded_mb = $(if ($i -lt $ld.Count) { [double]$ld[$i].privateMB }); unloaded_mb = $(if ($i -lt $n) { [double]$ul[$i].privateMB })
        pcm_loaded = $(if ($i -lt $al.Count) { [double]$al[$i].pcmMB }); pcm_unloaded = $(if ($i -lt $au.Count) { [double]$au[$i].pcmMB }); textures_released = $(if ($i -lt $tx.Count) { $tx[$i] }) })
}
function Slope([double[]]$y) { if ($y.Count -lt 2) { return $null }; $mx = ($y.Count - 1) / 2.0; $my = ($y | Measure-Object -Average).Average; $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt $y.Count; $i++) { $num += ($i - $mx) * ($y[$i] - $my); $den += ($i - $mx) * ($i - $mx) }; return [Math]::Round($num / $den, 2) }
Res "matches" $(if ($n -ge $Matches) { "PASS" } elseif ($n -ge 3) { "PARTIAL" } else { "FAIL" }) ("{0} of {1} matches completed and unloaded in one process" -f $n, $Matches) "Frontend"
foreach ($k in @(@{ c = "unloaded_mb"; o = "Gameplay/Rendering/Systems"; t = $GrowthMb }, @{ c = "loaded_mb"; o = "Gameplay/Rendering/Systems"; t = $GrowthMb }, @{ c = "pcm_unloaded"; o = "Systems"; t = 2 }, @{ c = "pcm_loaded"; o = "Systems"; t = 2 })) {
    $y = @($rows | Select-Object -Skip 1 | ForEach-Object { $_.($k.c) } | Where-Object { $_ -ne $null } | ForEach-Object { [double]$_ })
    if ($y.Count -lt 3) { Res $k.c "UNKNOWN" "fewer than 3 post-first-match samples" $k.o; continue }
    $sl = Slope $y; $rise = [Math]::Round($y[-1] - $y[0], 1)
    Res $k.c $(if ($sl -gt $k.t / 3 -and $rise -gt 2 * $k.t / 3) { "FAIL" } else { "PASS" }) ("matches 2..{0}: {1}; slope {2} MB/match, rise {3} MB (GROWTH if slope > {4} and rise > {5})" -f ($y.Count + 1), (($y | ForEach-Object { [Math]::Round($_, 1) }) -join " -> "), $sl, $rise, [Math]::Round($k.t / 3, 1), [Math]::Round(2 * $k.t / 3, 1)) $k.o
}
$tu = @($tx | Select-Object -Unique)
Res "textures_released" $(if (-not $tx.Count) { "UNKNOWN" } elseif ($tu.Count -eq 1) { "PASS" } else { "FAIL" }) ("textures released per return: {0}" -f ($tx -join ", ")) "Rendering"
Write-WfcCsv $rows (Join-Path $OutDir "matches.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
Write-M07Matrix $rows @("match", "loaded_mb", "unloaded_mb", "pcm_loaded", "pcm_unloaded", "textures_released") (Join-Path $OutDir "MEMORY.md") "Repeated-match memory ($tagRun)" @("build: ``$sha`` ($Config); Streets TDM (map id $MapId), goal $Goal, $Matches matches in one process; bots $f friendly / $e enemy.")
"MATCH-REPEAT MEMORY ($tagRun): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$rows | Format-Table -AutoSize | Out-String -Width 200
