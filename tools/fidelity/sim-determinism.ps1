# SIM DETERMINISM (tier TARGETED; Gameplay's threaded simulation, 09c cda46cc+). Identical seeded matches:
# direct boot (no frontend wall-clock timing), WFC_LOCKSTEP (exactly one 60 Hz step per frame), WFC_SEED fixed, the same bots
# and the same scripted input, for -Frames frames. Per seed: one SERIAL run (WFC_SIMTHREADS=0) and -Repeats threaded runs
# (worker threads as shipped, plus -ExtraEnv, e.g. "WFC_ASYNCSTEP=1" for the async sim thread). The signature is every bot's
# once-a-second state (WFC_BOTLOG=all: position, cell, form, goal, target, stuck, hp, ammo, shots, hits, ...) plus the XP /
# award event stream (WFC_XPLOG), in log order. Every threaded run must equal its seed's serial run; a difference reports
# the seed, run and first divergent line (for Gameplay to reproduce). Rare races need many runs: use -Repeats 5+ and
# -Config Debug as well (Gameplay saw 1 failure in 16 Debug runs on 0d308cd).
#
#   .\tools\fidelity\sim-determinism.ps1 -Root work\ab\<target> -OutDir <dir> [-Seeds 123,124] [-Repeats 2] [-Frames 3600] [-Bots 8] [-ExtraEnv "WFC_ASYNCSTEP=1"] [-Config Debug] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Frames = 3600, [string[]]$Seeds = @("123"), [int]$Repeats = 2,
      [int]$Bots = 8, [string]$Map = "MP_IAC_Streets", [string]$ExtraEnv = "", [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$sha = if (Test-Path (Join-Path $Root "M05_TARGET.txt")) { ((Get-Content (Join-Path $Root "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "?" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "determinism.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_SIMTHREADS")) { Res "hook" "UNKNOWN" "build has no WFC_SIMTHREADS (threaded sim not in this build)" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$Seeds = @($Seeds | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
$extra = @{}; foreach ($kv in @($ExtraEnv -split ';' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $extra[$kv.Substring(0, $i).Trim()] = $kv.Substring($i + 1) }
$url = "{0}?GameModeTag=TDM?BotsAutobot={1}?BotsDecepticon={1}?BotDifficulty=1?ExtendedPlayers=1" -f $Map, $Bots
function RunOne([string]$tag, [string]$seed, [bool]$serial) {
    $d = Join-Path $OutDir $tag; New-Item -ItemType Directory -Force $d | Out-Null
    if ($ReportOnly -or (Test-Path (Join-Path $d "wfc.log"))) { return }
    if (-not (Wait-WfcGpu)) { Res "$tag.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; return }
    $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_LOCKSTEP = "1"; WFC_SEED = "$seed"; WFC_SMOKE_FRAMES = "$Frames"; WFC_LOGEVERY = "0"
            WFC_BOTLOG = "all"; WFC_XPLOG = "1"; WFC_NOMOUSE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOSTRAFE = "1"; WFC_AUTOJUMP_EVERY = "150" }
    if ($serial) { $e.WFC_SIMTHREADS = "0" } else { foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] } }
    $null = Invoke-WfcExe $exe $d $e "run.log" 1800
}
function Sig([string]$tag) {
    $lg = Join-Path $OutDir "$tag\wfc.log"; if (-not (Test-Path $lg)) { return $null }
    # leading comma: an EMPTY signature must stay an empty array (a bare @() return becomes $null = "missing")
    return ,@([IO.File]::ReadLines($lg) | Where-Object { $_ -match '\] (BOTLOG |XP p\d+ txn )' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\s*', '' })
}
$fails = 0; $total = 0
foreach ($seed in $Seeds) {
    RunOne "s${seed}_serial" $seed $true
    for ($r = 1; $r -le $Repeats; $r++) { RunOne "s${seed}_thr$r" $seed $false }
    $ref = Sig "s${seed}_serial"
    for ($r = 1; $r -le $Repeats; $r++) {
        $b = Sig "s${seed}_thr$r"; $name = "seed$seed.run$r"
        if ($null -eq $ref -or $null -eq $b) { Res $name "UNKNOWN" "a run is missing" "Experimental"; continue }
        if (-not $ref.Count -and -not $b.Count) { Res $name "UNKNOWN" "no bot activity logged in either run (bots never became active within the frame budget)" "Gameplay"; continue }
        if (-not $ref.Count -or -not $b.Count) { Res $name "FAIL" ("bot activity differs between identical runs: serial {0} BOTLOG / XP lines vs threaded {1} - the bots did not run (or started at a different time) in one of them; a lockstep seeded run must log the same bot states" -f $ref.Count, $b.Count) "Gameplay"; $total++; $fails++; continue }
        $total++
        $n = [Math]::Min($ref.Count, $b.Count); $first = -1
        for ($i = 0; $i -lt $n; $i++) { if ($ref[$i] -ne $b[$i]) { $first = $i; break } }
        if ($first -lt 0 -and $ref.Count -eq $b.Count) { Res $name "PASS" ("threaded run {0} == serial: {1} state / event lines" -f $r, $ref.Count) "Gameplay"; continue }
        if ($first -lt 0) { $first = $n }
        $fails++
        Res $name "FAIL" ("seed {0} run {1} ({2}) diverges from serial at line {3} of {4} / {5}:`n  serial:   {6}`n  threaded: {7}`n  log: {8}" -f $seed, $r, $(if ($ExtraEnv) { $ExtraEnv } else { "threads" }), $first, $ref.Count, $b.Count,
            $(if ($first -lt $ref.Count) { $ref[$first] } else { "(end)" }), $(if ($first -lt $b.Count) { $b[$first] } else { "(end)" }), (Join-Path $OutDir "s${seed}_thr$r\wfc.log")) "Gameplay"
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"DETERMINISM ($sha, $Config, $Frames lockstep frames, seeds $($Seeds -join ','), $Repeats threaded runs each$(if ($ExtraEnv) { ", $ExtraEnv" }), $Bots v $Bots): $fails of $total threaded runs diverged; " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | Where-Object { $_.status -ne "PASS" } | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
