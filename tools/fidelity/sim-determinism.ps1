# SIM DETERMINISM (tier TARGETED; Gameplay's threaded simulation, 09c cda46cc+). Three identical runs of the same seeded match:
# direct boot (no frontend wall-clock timing), WFC_LOCKSTEP (exactly one 60 Hz step per frame), WFC_SEED fixed, the same bots
# and the same scripted input, for -Frames frames. Runs: thrA and thrB (worker threads as shipped) and serial
# (WFC_SIMTHREADS=0). The signature is every bot's once-a-second state (WFC_BOTLOG=all: position, cell, form, goal, target,
# stuck, hp, ammo, shots, hits, ...) plus the XP / award event stream (WFC_XPLOG), in log order.
#   repeatable      thrA == thrB  (threads do not introduce run-to-run nondeterminism)
#   matches_serial  thrA == serial (the deterministic merge reproduces the single-threaded result)
# A difference reports the first divergent line of each pair.
#
#   .\tools\fidelity\sim-determinism.ps1 -Root work\ab\<target> -OutDir <dir> [-Frames 3600] [-Seed 123] [-Bots 8] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Frames = 3600, [int]$Seed = 123, [int]$Bots = 8,
      [string]$Map = "MP_IAC_Streets", [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "determinism.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_SIMTHREADS")) { Res "hook" "UNKNOWN" "build has no WFC_SIMTHREADS (threaded sim not in this build)" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$url = "{0}?GameModeTag=TDM?BotsAutobot={1}?BotsDecepticon={1}?BotDifficulty=1?ExtendedPlayers=1" -f $Map, $Bots
$runs = @(@{ tag = "thrA"; serial = $false }, @{ tag = "thrB"; serial = $false }, @{ tag = "serial"; serial = $true })
foreach ($r in $runs) {
    $d = Join-Path $OutDir $r.tag; New-Item -ItemType Directory -Force $d | Out-Null
    if (-not $ReportOnly -and -not (Test-Path (Join-Path $d "wfc.log"))) {
        if (-not (Wait-WfcGpu)) { Res "$($r.tag).gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_LOCKSTEP = "1"; WFC_SEED = "$Seed"; WFC_SMOKE_FRAMES = "$Frames"; WFC_LOGEVERY = "0"
                WFC_BOTLOG = "all"; WFC_XPLOG = "1"; WFC_NOMOUSE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_AUTOJUMP_EVERY = "150" }
        if ($r.serial) { $e.WFC_SIMTHREADS = "0" }
        $null = Invoke-WfcExe $exe $d $e "run.log" 900
    }
}
function Sig([string]$tag) {
    $lg = Join-Path $OutDir "$tag\wfc.log"; if (-not (Test-Path $lg)) { return $null }
    return @([IO.File]::ReadLines($lg) | Where-Object { $_ -match '\] (BOTLOG |XP p\d+ txn )' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\s*', '' })
}
function Cmp($a, $b, [string]$name, [string]$owner) {
    if ($null -eq $a -or $null -eq $b) { Res $name "UNKNOWN" "a run is missing" "Experimental"; return }
    if (-not $a.Count) { Res $name "UNKNOWN" "no BOTLOG / XP lines (bots did not run?)" "Experimental"; return }
    $n = [Math]::Min($a.Count, $b.Count); $first = -1
    for ($i = 0; $i -lt $n; $i++) { if ($a[$i] -ne $b[$i]) { $first = $i; break } }
    if ($first -lt 0 -and $a.Count -eq $b.Count) { Res $name "PASS" ("identical: {0} state / event lines" -f $a.Count) $owner; return }
    if ($first -lt 0) { $first = $n }
    Res $name "FAIL" ("first divergence at line {0} of {1} / {2}:`n  A: {3}`n  B: {4}" -f $first, $a.Count, $b.Count, $(if ($first -lt $a.Count) { $a[$first] } else { "(end)" }), $(if ($first -lt $b.Count) { $b[$first] } else { "(end)" })) $owner
}
$sA = Sig "thrA"; $sB = Sig "thrB"; $sS = Sig "serial"
Cmp $sA $sB "repeatable_threaded" "Gameplay"
Cmp $sA $sS "threaded_matches_serial" "Gameplay"
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"DETERMINISM ($sha, $Frames lockstep frames, seed $Seed, $Bots v $Bots): " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
