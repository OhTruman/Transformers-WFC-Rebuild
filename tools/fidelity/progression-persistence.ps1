# PROGRESSION PERSISTENCE (tier TARGETED; milestone 09 "fresh account -> match -> XP -> restart -> reload, no lost or
# duplicated rewards"). Two processes in ONE working directory (the profile wfc_profile.ini lives in the cwd):
#   run A  fresh profile -> private TDM (kills: the scripted lifecycle opponent until bots land; -Bots N uses Bot Settings)
#          -> match end -> results -> lobby -> quit
#   run B  restart -> the profile reloads -> the same match again -> quit
# From the flow traces (progression.xp per award with its transaction id, progression.match end with lastMatchXp, levelUp,
# challenge) and the saved [Progression] section after each run:
#   awarded       run A awards XP at all (else UNKNOWN: no kills / no award feed in this build)
#   no_duplicates a transaction id is awarded once per run
#   match_sum     the sum of a match's awards == its lastMatchXp
#   saved_A       the profile after A holds XP<class> == run A's running total
#   reload_B      the profile is NOT changed by B's boot (no loss / reset on load): B's first award total = saved A + award
#   saved_B       the profile after B = saved A + run B's awards (per class), challenges / tiers never decrease
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
                WFC_SMOKE_FRAMES = "100000000"; WFC_LOGEVERY = "0"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.2" }
        if ($Bots -gt 0) { $e.WFC_BOTS = "$Bots" } else { $e.WFC_LIFECYCLE = "$Goal" }
        if ($H.Contains("WFC_CHARSELECT")) { $e.WFC_CHARSELECT = "1" }
        $null = Invoke-WfcExe $exe $work $e "run_$tag.log" 900
        if (Test-Path (Join-Path $work "wfc.log")) { Copy-Item (Join-Path $work "wfc.log") (Join-Path $OutDir "wfc_$tag.log") -Force }
        if (Test-Path $ini) { Copy-Item $ini (Join-Path $OutDir "profile_after_$tag.ini") -Force }
    }
    return (Test-Path $flow)
}
if (-not $ReportOnly) { if (Test-Path $ini) { Remove-Item $ini -Force }; Remove-Item (Join-Path $OutDir "flow_*.jsonl"), (Join-Path $OutDir "profile_after_*.ini") -ErrorAction SilentlyContinue }   # FRESH profile
$okA = RunOnce "A"; $profA = ReadProg (Join-Path $OutDir "profile_after_A.ini")
$okB = $okA -and (RunOnce "B"); $profB = ReadProg (Join-Path $OutDir "profile_after_B.ini")

function Awards([string]$tag) { $F = Read-FlowLog (Join-Path $OutDir "flow_$tag.jsonl"); return @(Flow-Ev $F "progression.xp") }
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
            Res "no_duplicates_$tag" $(if ($dup.Count) { "FAIL" } else { "PASS" }) ("run {0}: transaction ids awarded more than once: {1}" -f $tag, $(if ($dup.Count) { $dup -join "," } else { "none" })) "Frontend"
            if ($me.Count) { $lm = [long]$me[-1].lastMatchXp; $sum = ($aw | ForEach-Object { [long]$_.xp } | Measure-Object -Sum).Sum
                Res "match_sum_$tag" $(if ($sum -eq $lm) { "PASS" } else { "FAIL" }) ("run {0}: sum of awards {1} vs lastMatchXp {2} (level {3}, saved {4})" -f $tag, $sum, $lm, $me[-1].level, $me[-1].saved) "Frontend" }
            else { Res "match_sum_$tag" "UNKNOWN" "run ${tag}: no progression.match end event (match did not end?)" "Frontend/Gameplay" }
        }
        # saved after A == run A's running totals per class (the last award's 'total' per specialty)
        $lastTotA = @{}; foreach ($a in (Awards "A")) { if ($a.specialty -and "$($a.total)" -ne "-") { $lastTotA["$($a.specialty)"] = [long]$a.total } }
        $badA = @($lastTotA.Keys | Where-Object { [long]$profA["Xp$_"] -ne $lastTotA[$_] })
        Res "saved_A" $(if (-not $lastTotA.Count) { "UNKNOWN" } elseif ($badA.Count) { "FAIL" } else { "PASS" }) ("profile after A: {0}; award totals {1}" -f ((@($profA.Keys | Where-Object { $_ -like "Xp*" } | Sort-Object | ForEach-Object { "$_=$($profA[$_])" })) -join " "), ((@($lastTotA.Keys | ForEach-Object { "$_=$($lastTotA[$_])" })) -join " ")) "Frontend"
        if ($okB) {
            # reload: B's first award total per class must be saved A + that award (nothing lost or reset at load)
            $awB = @(Awards "B"); $firstB = @{}; foreach ($a in $awB) { if ($a.specialty -and -not $firstB.ContainsKey("$($a.specialty)") -and "$($a.total)" -ne "-") { $firstB["$($a.specialty)"] = $a } }
            $badR = @($firstB.Keys | Where-Object { [long]$firstB[$_].total -ne ([long]$profA["Xp$_"] + [long]$firstB[$_].xp) })
            Res "reload_B" $(if (-not $firstB.Count) { "UNKNOWN" } elseif ($badR.Count) { "FAIL" } else { "PASS" }) ("run B first award per class: {0} (saved A + award expected)" -f ((@($firstB.Keys | ForEach-Object { "$_ total $($firstB[$_].total) = A $($profA["Xp$_"]) + $($firstB[$_].xp)" })) -join "; ")) "Frontend"
            $sumB = @{}; foreach ($a in $awB) { if ($a.specialty) { $sumB["$($a.specialty)"] = [long]$sumB["$($a.specialty)"] + [long]$a.xp } }
            $classes = @(@($profA.Keys) + @($profB.Keys) | Where-Object { $_ -like "Xp*" } | ForEach-Object { $_.Substring(2) } | Select-Object -Unique)
            $badB = @($classes | Where-Object { [long]$profB["Xp$_"] -ne ([long]$profA["Xp$_"] + [long]$sumB[$_]) })
            $tierDrop = @($profA.Keys | Where-Object { $_ -like "Tier.*" -and [int]$profB[$_] -lt [int]$profA[$_] })
            Res "saved_B" $(if ($badB.Count -or $tierDrop.Count) { "FAIL" } else { "PASS" }) ("profile after B per class (A + run B awards): {0}; challenge tiers that decreased: {1}" -f ((@($classes | ForEach-Object { "$_ $($profB["Xp$_"]) = $($profA["Xp$_"]) + $([long]$sumB[$_])" })) -join "; "), $(if ($tierDrop.Count) { $tierDrop -join "," } else { "none" })) "Frontend"
        }
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"PROGRESSION ($sha): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
