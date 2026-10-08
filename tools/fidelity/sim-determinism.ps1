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
# Frame budgets count LOADING frames too (one presented frame per load yield, varying with caches), so identical sims can
# start their match at different frames: runs are compared on their common PREFIX (the shorter log must equal the start of
# the longer), with -MinLines of overlap required; with Gameplay's WFC_MATCH_SECONDS every run ends a fixed match time
# after InProgress instead (2026-10-07: the "0 vs 124 lines" runs were prefix-identical - a harness artefact).
# NOTE: the reference run sets WFC_SIMTHREADS=0, which ALSO turns the async step off (Gameplay): "serial" = sync + serial.
# -ExtraEnv "WFC_ASYNCSTEP=0" makes the compared runs threads-on / async-off, isolating threading from the async step.
# WFC_SIMHASH (Gameplay; per-step hash of every pawn + bot steering state): when the build has it every run logs it, the hash
# streams are compared too (stronger than the once-a-second BOTLOG), and a failure names the FIRST DIFFERING STEP - rerun that
# seed with -HashDetail "<step-2>-<step>" (WFC_SIMHASH range) for the per-pawn / per-field dump Gameplay asks for.
# -SerialRepeats N adds N more SERIAL runs per seed compared with the first serial run: a serial-vs-serial difference means the
# nondeterminism is run-dependent (address-ordered containers, uninitialised data), not thread scheduling (Gameplay 2026-10-07).
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [int]$Frames = 7200, [int]$MatchSeconds = 50, [int]$MinLines = 40, [string[]]$Seeds = @("123"), [int]$Repeats = 2,
      [int]$Bots = 8, [string]$Map = "MP_IAC_Streets", [string]$ExtraEnv = "", [ValidateSet("Release", "Debug")][string]$Config = "Release", [string]$HashDetail = "", [int]$SerialRepeats = 0, [switch]$ReportOnly)
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
    if ($H.Contains("WFC_MATCH_SECONDS")) { $e.WFC_MATCH_SECONDS = "$MatchSeconds"; $e.WFC_SMOKE_FRAMES = "1000000" }
    if ($H.Contains("WFC_SIMHASH")) { $e.WFC_SIMHASH = $(if ($HashDetail) { $HashDetail } else { "0" }) }
    if ($serial) { $e.WFC_SIMTHREADS = "0" } else { foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] } }
    $null = Invoke-WfcExe $exe $d $e "run.log" 1800
}
function Sig([string]$tag) {
    $lg = Join-Path $OutDir "$tag\wfc.log"; if (-not (Test-Path $lg)) { return $null }
    # leading comma: an EMPTY signature must stay an empty array (a bare @() return becomes $null = "missing")
    return ,@([IO.File]::ReadLines($lg) | Where-Object { $_ -match '\] (BOTLOG |XP p\d+ txn )' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\s*', '' })
}
function Hashes([string]$tag) {
    $lg = Join-Path $OutDir "$tag\wfc.log"; if (-not (Test-Path $lg)) { return ,@() }
    return ,@([IO.File]::ReadLines($lg) | Where-Object { $_ -match 'SIMHASH' } | ForEach-Object { $_ -replace '^\[[^\]]*\]\s*', '' })
}
function HashDiff($a, $b) {   # first differing per-step hash line over the common prefix, or $null
    $n = [Math]::Min($a.Count, $b.Count)
    for ($i = 0; $i -lt $n; $i++) { if ($a[$i] -ne $b[$i]) { return "first differing SIMHASH line $i of $($a.Count) / $($b.Count):`n  serial:   $($a[$i])`n  threaded: $($b[$i])" } }
    return $null
}
$fails = 0; $total = 0
function CompareSerial($seed) {   # serial vs serial: no threads involved, so any difference is run-dependent state
    $ref = Sig "s${seed}_serial"
    for ($r = 1; $r -le $SerialRepeats; $r++) {
        $b = Sig "s${seed}_ser$r"; $name = "seed$seed.serial$r"
        if ($null -eq $ref -or $null -eq $b -or -not $ref.Count -or -not $b.Count) { Res $name "UNKNOWN" "a serial run is missing or has no bot activity" "Experimental"; continue }
        $script:total++; $n = [Math]::Min($ref.Count, $b.Count); $first = -1
        for ($i = 0; $i -lt $n; $i++) { if ($ref[$i] -ne $b[$i]) { $first = $i; break } }
        $hd = $null; $ha = Hashes "s${seed}_serial"; $hb = Hashes "s${seed}_ser$r"; if ($ha.Count -and $hb.Count) { $hd = HashDiff $ha $hb }
        if ($first -lt 0 -and -not $hd) { Res $name "PASS" ("serial run {0} == serial over {1} lines" -f $r, $n) "Gameplay"; continue }
        $script:fails++
        Res $name "FAIL" ("SERIAL vs SERIAL differ (run-dependent, not threading): {0}{1}" -f $(if ($first -ge 0) { "BOTLOG line $first`n  a: $($ref[$first])`n  b: $($b[$first])" } else { "BOTLOG equal" }), $(if ($hd) { "; $hd" } else { "" })) "Gameplay"
    }
}
foreach ($seed in $Seeds) {
    RunOne "s${seed}_serial" $seed $true
    for ($r = 1; $r -le $Repeats; $r++) { RunOne "s${seed}_thr$r" $seed $false }
    for ($r = 1; $r -le $SerialRepeats; $r++) { RunOne "s${seed}_ser$r" $seed $true }
    CompareSerial $seed
    $ref = Sig "s${seed}_serial"
    for ($r = 1; $r -le $Repeats; $r++) {
        $b = Sig "s${seed}_thr$r"; $name = "seed$seed.run$r"
        if ($null -eq $ref -or $null -eq $b) { Res $name "UNKNOWN" "a run is missing" "Experimental"; continue }
        if (-not $ref.Count -and -not $b.Count) { Res $name "UNKNOWN" "no bot activity logged in either run (bots never became active within the frame budget)" "Gameplay"; continue }
        if (-not $ref.Count -or -not $b.Count) { Res $name "UNKNOWN" ("one run has no bot activity in its frame budget (serial {0} lines, threaded {1}): its match started too late (loading frames count) - not comparable; use WFC_MATCH_SECONDS or more -Frames" -f $ref.Count, $b.Count) "Experimental"; continue }
        $total++
        $n = [Math]::Min($ref.Count, $b.Count); $first = -1
        for ($i = 0; $i -lt $n; $i++) { if ($ref[$i] -ne $b[$i]) { $first = $i; break } }
        if ($first -lt 0) {   # identical over the common prefix: same sim, different match start frame / end
            if ($n -lt $MinLines) { $total--; Res $name "UNKNOWN" ("identical over the common prefix but only {0} lines overlap (serial {1}, threaded {2}): not enough match time in one run (load frames ate the frame budget)" -f $n, $ref.Count, $b.Count) "Experimental"; continue }
            $ha = Hashes "s${seed}_serial"; $hb = Hashes "s${seed}_thr$r"; $hd = if ($ha.Count -and $hb.Count) { HashDiff $ha $hb } else { $null }
            if ($hd) { $fails++; Res $name "FAIL" ("seed {0} run {1}: BOTLOG equal over {2} lines but the per-step sim hash differs - {3}" -f $seed, $r, $n, $hd) "Gameplay"; continue }
            Res $name "PASS" ("threaded run {0} == serial over {1} common state / event lines (serial {2}, threaded {3}){4}" -f $r, $n, $ref.Count, $b.Count, $(if ($ha.Count -and $hb.Count) { "; per-step sim hash equal over $([Math]::Min($ha.Count, $hb.Count)) steps" } else { "" })) "Gameplay"; continue }
        $fails++
        Res $name "FAIL" ("seed {0} run {1} ({2}) diverges from serial at line {3} of {4} / {5}:`n  serial:   {6}`n  threaded: {7}`n  log: {8}" -f $seed, $r, $(if ($ExtraEnv) { $ExtraEnv } else { "threads" }), $first, $ref.Count, $b.Count,
            $(if ($first -lt $ref.Count) { $ref[$first] } else { "(end)" }), $(if ($first -lt $b.Count) { $b[$first] } else { "(end)" }), (Join-Path $OutDir "s${seed}_thr$r\wfc.log")) "Gameplay"
        $ha = Hashes "s${seed}_serial"; $hb = Hashes "s${seed}_thr$r"
        if ($ha.Count -and $hb.Count) { $hd = HashDiff $ha $hb; Res "$name.simhash" "INFO" $(if ($hd) { "$hd - rerun with -Seeds $seed -HashDetail <that step> for the per-pawn dump" } else { "per-step sim hash equal over $([Math]::Min($ha.Count, $hb.Count)) steps although BOTLOG differs" }) "Gameplay" }
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"DETERMINISM ($sha, $Config, $Frames lockstep frames, seeds $($Seeds -join ','), $Repeats threaded runs each$(if ($ExtraEnv) { ", $ExtraEnv" }), $Bots v $Bots): $fails of $total threaded runs diverged; " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | Where-Object { $_.status -ne "PASS" } | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
