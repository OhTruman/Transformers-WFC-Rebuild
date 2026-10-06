# PROGRESSION PERSISTENCE (tier TARGETED; milestone 09 "fresh account -> match -> XP -> restart -> reload, no lost or
# duplicated rewards"). Two processes in ONE working directory (the profile wfc_profile.ini lives in the cwd):
#   run A  fresh profile -> private TDM (kills: the scripted lifecycle opponent until bots land; -Bots N uses Bot Settings)
#          -> match end -> results -> lobby -> quit
#   run B  restart -> the profile reloads -> the same match again -> quit
# From the flow traces (progression.xp per award with its transaction id, progression.match end with lastMatchXp, levelUp,
# challenge) and the saved [Progression] section after each run:
#   awarded       run A awards XP at all (else UNKNOWN: no kills / no award feed in this build)
#   no_duplicates a transaction id is awarded once per run
#   match_sum     per class: progression.xp awards + progression.challenge tier XP (Prime -> all four classes) == the saved
#                 LastMatch<class> (Frontend semantics 2026-10-06; xp=0 awards are legitimate when CanGainXp is false)
#   saved_A       the profile after A holds XP<class> == run A's running total
#   reload_B      the profile is NOT changed by B's boot (no loss / reset on load): B's first award total = saved A + award
#   handoff       Gameplay's produced awards (WFC_XPLOG "XP p<player> txn <id> ...") for the local player == the transactions
#                 Frontend consumed (progression.xp): none lost, none invented, none doubled between the lanes
#   saved_B       the profile after B = min(saved A + run B's XP incl. challenge XP, 355000) per class; tiers never decrease
#
#   .\tools\fidelity\progression-persistence.ps1 -Root work\ab\<target> -OutDir <dir> [-Bots 0] [-Goal 3] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Bots = 0, [int]$Goal = 3,
      [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "progression.$id" $status $null $note $owner }
$work = Join-Path $OutDir "profile_dir"; New-Item -ItemType Directory -Force $work | Out-Null   # both runs share this cwd
$ini = Join-Path $work "wfc_profile.ini"

function ReadProg([string]$path) {   # [Progression] section -> hashtable
    $h = @{}; if (-not (Test-Path $path)) { return $h }; $in = $false
    foreach ($l in Get-Content $path) { if ($l -match '^\[(.+)\]') { $in = $Matches[1] -eq "Progression"; continue }; if ($in -and $l -match '^([^=]+)=(.*)$') { $h[$Matches[1]] = $Matches[2] } }
    return $h
}
$cs = if ($H.Contains("WFC_CHARSELECT")) { "wait:movie=CustomTransformers;wait:t=1.5;ui:Accept;" } else { "" }
$script = (@("wait:frontend", (Get-MousePark $Root), "wait:ui=FrontEnd", "wait:t=2", "call:Online.OpenPartyLobby,GTS_TeamGame", "wait:level=PartyLobby", "wait:ui=InLobby", "wait:t=1",
            "call:Online.EditGameMode,TDM", "call:Online.PlayPrivateGame,TDM", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=1.5", "call:Online.SetSelectedMapID,508", "wait:t=1",
            "call:Online.BeginLobbyExitCountdown", "wait:level=Match", "${cs}wait:ui=InGame", "wait:ui=GameEnded", "wait:t=3", "wait:level=GameLobby", "wait:ui=InLobby", "wait:t=2", "quit") -join ";")
function RunOnce([string]$tag) {
    $flow = Join-Path $OutDir "flow_$tag.jsonl"
    if (-not $ReportOnly -and -not (Test-Path $flow)) {
        if (-not (Wait-WfcGpu)) { return $false }
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_NOMOUSE = "1"; WFC_FRONTEND_SCRIPT = $script; WFC_FLOWLOG = $flow; WFC_FLOW_TIMEOUT = "600";
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2"; WFC_XPLOG = "1" }   # Gameplay's producer log (7ed5faf+)
        $e.WFC_LIFECYCLE = "$Goal"   # match end at the goal; bots (if any) come from the profile Bot Settings, there is no WFC_BOTS hook
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        $null = Invoke-WfcExe $exe $work $e "run_$tag.log" 900
        if (Test-Path (Join-Path $work "wfc.log")) { Copy-Item (Join-Path $work "wfc.log") (Join-Path $OutDir "wfc_$tag.log") -Force }
        if (Test-Path $ini) { Copy-Item $ini (Join-Path $OutDir "profile_after_$tag.ini") -Force }
    }
    return (Test-Path $flow)
}
if (-not $ReportOnly) { if (Test-Path $ini) { Remove-Item $ini -Force }; Remove-Item (Join-Path $OutDir "flow_*.jsonl"), (Join-Path $OutDir "profile_after_*.ini") -ErrorAction SilentlyContinue }   # FRESH profile
# -Bots N: Private Match Bot Settings as a player sets them ([PCSettings], N split friendly / enemy); progression stays fresh
if (-not $ReportOnly -and $Bots -gt 0) { $bf = [int][Math]::Floor(($Bots - 1) / 2); "[PCSettings]`nWidth=1280`nHeight=720`nFullscreen=0`nBotsFriendly=$bf`nBotsEnemy=$($Bots - $bf)`nBotDifficulty=1`n" | Set-Content -Encoding ASCII $ini }
$okA = RunOnce "A"; $profA = ReadProg (Join-Path $OutDir "profile_after_A.ini")
$okB = $okA -and (RunOnce "B"); $profB = ReadProg (Join-Path $OutDir "profile_after_B.ini")

function Awards([string]$tag) { $F = Read-FlowLog (Join-Path $OutDir "flow_$tag.jsonl"); return @(Flow-Ev $F "progression.xp") }
function Challenges([string]$tag) { $F = Read-FlowLog (Join-Path $OutDir "flow_$tag.jsonl"); return @(Flow-Ev $F "progression.challenge") }
$kCap = 355000
# per-class XP of one run: awards by specialty + challenge XP (to the played class, or all four when Prime)
function RunXp([string]$tag) {
    $h = @{}; $aw = @(Awards $tag); $played = @($aw | ForEach-Object { "$($_.specialty)" } | Where-Object { $_ } | Select-Object -Unique)
    foreach ($a in $aw) { if ($a.specialty) { $h["$($a.specialty)"] = [long]$h["$($a.specialty)"] + [long]$a.xp } }
    foreach ($c in (Challenges $tag)) { $targets = if ("$($c.prime)" -match '^(1|true)$') { @("Scout", "Scientist", "Leader", "Soldier") } else { $played }
        foreach ($t in $targets) { $h[$t] = [long]$h[$t] + [long]$c.xp } }
    return $h
}
function MatchEnd([string]$tag) { $F = Read-FlowLog (Join-Path $OutDir "flow_$tag.jsonl"); return @(Flow-Ev $F "progression.match" | Where-Object { $_.end }) }
if (-not $okA) { Res "run" "UNKNOWN" "run A produced no flow log (GPU busy / crash)" "Experimental" }
else {
    $hasProg = $H.Contains("WFC_ORIGINAL_XP_RULE") -or (Select-String -Path (Join-Path $OutDir "flow_A.jsonl") -Pattern '"progression\.' -Quiet)
    if (-not $hasProg) { Res "build" "SKIP" "this build emits no progression.* traces (Frontend 9e4e2aa+ / Gameplay award feed not merged)" "Experimental" }
    else {
        foreach ($tag in "A", "B") { if ($tag -eq "B" -and -not $okB) { continue }
            $aw = @(Awards $tag); $me = @(MatchEnd $tag)
            $awarded = @($aw | Where-Object { [long]$_.xp -gt 0 })
            if ($tag -eq "A") { Res "awarded" $(if ($awarded.Count) { "PASS" } else { "UNKNOWN" }) ("run A: {0} XP awards totalling {1} ({2})" -f $awarded.Count, (($awarded | ForEach-Object { [long]$_.xp } | Measure-Object -Sum).Sum), ((@($aw | ForEach-Object { "$($_.transaction):$($_.xp)" }) | Select-Object -First 8) -join " ")) "Frontend/Gameplay" }
            $dup = @($aw | Group-Object transaction | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
            # producer -> consumer hand-off by transaction id (the local player = the player whose ids the consumer saw)
            $wl = Join-Path $OutDir "wfc_$tag.log"
            $prod = if (Test-Path $wl) { @(Select-String $wl -Pattern '\] XP p(\d+) txn (\d+) (\S+) \+(-?\d+)' | ForEach-Object { [pscustomobject]@{ p = $_.Matches[0].Groups[1].Value; txn = $_.Matches[0].Groups[2].Value; ev = $_.Matches[0].Groups[3].Value; xp = [long]$_.Matches[0].Groups[4].Value } }) } else { @() }
            if (-not $prod.Count) { Res "handoff_$tag" "SKIP" "run ${tag}: no WFC_XPLOG producer lines (Gameplay 7ed5faf+ not in this build)" "Experimental" }
            else {
                $cons = @($aw | ForEach-Object { "$($_.transaction)" } | Select-Object -Unique)
                $local = @($prod | Where-Object { $cons -contains $_.txn } | ForEach-Object { $_.p } | Select-Object -First 1)[0]
                $mine = @($prod | Where-Object { $_.p -eq $local } | ForEach-Object { $_.txn } | Select-Object -Unique)
                $lost = @($mine | Where-Object { $cons -notcontains $_ }); $extra = @($cons | Where-Object { $mine -notcontains $_ })
                Res "handoff_$tag" $(if ($null -eq $local) { "FAIL" } elseif ($lost.Count -or $extra.Count) { "FAIL" } else { "PASS" }) ("run {0}: local player p{1}; produced {2} / consumed {3}; produced but not consumed {4}; consumed but not produced {5}" -f $tag, $local, $mine.Count, $cons.Count, $(if ($lost.Count) { $lost -join "," } else { "none" }), $(if ($extra.Count) { $extra -join "," } else { "none" })) "Gameplay/Frontend"
            }
            Res "no_duplicates_$tag" $(if ($dup.Count) { "FAIL" } else { "PASS" }) ("run {0}: transaction ids awarded more than once: {1}" -f $tag, $(if ($dup.Count) { $dup -join "," } else { "none" })) "Frontend"
            if ($me.Count) { $prof = if ($tag -eq "A") { $profA } else { $profB }; $rx = RunXp $tag
                $badM = @($rx.Keys | Where-Object { [long]$prof["LastMatch$_"] -ne $rx[$_] })
                Res "match_sum_$tag" $(if (-not $rx.Count) { "UNKNOWN" } elseif ($badM.Count) { "FAIL" } else { "PASS" }) ("run {0} per class (awards + challenge XP vs saved LastMatch): {1}; match end level {2}, saved {3}" -f $tag, ((@($rx.Keys | ForEach-Object { "$_ $($rx[$_]) vs $($prof["LastMatch$_"])" })) -join "; "), $me[-1].level, $me[-1].saved) "Frontend" }
            else { Res "match_sum_$tag" "UNKNOWN" "run ${tag}: no progression.match end event (match did not end?)" "Frontend/Gameplay" }
        }
        # saved after A == run A's running totals per class (the last award's 'total' per specialty)
        $lastTotA = @{}; foreach ($a in (Awards "A")) { if ($a.specialty -and "$($a.total)" -ne "-") { $lastTotA["$($a.specialty)"] = [long]$a.total } }
        $badA = @($lastTotA.Keys | Where-Object { [long]$profA["Xp$_"] -ne $lastTotA[$_] })
        Res "saved_A" $(if (-not $lastTotA.Count) { "UNKNOWN" } elseif ($badA.Count) { "FAIL" } else { "PASS" }) ("profile after A: {0}; award totals {1}" -f ((@($profA.Keys | Where-Object { $_ -like "Xp*" } | Sort-Object | ForEach-Object { "$_=$($profA[$_])" })) -join " "), ((@($lastTotA.Keys | ForEach-Object { "$_=$($lastTotA[$_])" })) -join " ")) "Frontend"
        if ($okB) {
            # reload: B's first award total per class must be saved A + that award (nothing lost or reset at load)
            $awB = @(Awards "B"); $firstB = @{}; foreach ($a in $awB) { if ($a.specialty -and -not $firstB.ContainsKey("$($a.specialty)") -and "$($a.total)" -ne "-") { $firstB["$($a.specialty)"] = $a } }
            $badR = @($firstB.Keys | Where-Object { [long]$firstB[$_].total -ne [Math]::Min([long]$profA["Xp$_"] + [long]$firstB[$_].xp, $kCap) })
            Res "reload_B" $(if (-not $firstB.Count) { "UNKNOWN" } elseif ($badR.Count) { "FAIL" } else { "PASS" }) ("run B first award per class: {0} (saved A + award expected)" -f ((@($firstB.Keys | ForEach-Object { "$_ total $($firstB[$_].total) = min(A $($profA["Xp$_"]) + $($firstB[$_].xp), 355000)" })) -join "; ")) "Frontend"
            $sumB = RunXp "B"
            $classes = @(@($profA.Keys) + @($profB.Keys) | Where-Object { $_ -like "Xp*" } | ForEach-Object { $_.Substring(2) } | Select-Object -Unique)
            $badB = @($classes | Where-Object { [long]$profB["Xp$_"] -ne [Math]::Min([long]$profA["Xp$_"] + [long]$sumB[$_], $kCap) })
            $tierDrop = @($profA.Keys | Where-Object { $_ -like "Tier.*" -and [int]$profB[$_] -lt [int]$profA[$_] })
            Res "saved_B" $(if ($badB.Count -or $tierDrop.Count) { "FAIL" } else { "PASS" }) ("profile after B per class (A + run B awards): {0}; challenge tiers that decreased: {1}" -f ((@($classes | ForEach-Object { "$_ $($profB["Xp$_"]) = $($profA["Xp$_"]) + $([long]$sumB[$_])" })) -join "; "), $(if ($tierDrop.Count) { $tierDrop -join "," } else { "none" })) "Frontend"
        }
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"PROGRESSION ($sha): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
