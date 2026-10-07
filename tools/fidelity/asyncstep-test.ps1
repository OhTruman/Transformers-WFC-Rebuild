# ASYNC STEP SELF-TEST (tier TARGETED; Gameplay's WFC_ASYNCSTEP sim thread). Runs Gameplay's in-process test hook per mode:
# the seeded 32 v 32 played twice (A: whole synchronous steps; B: the live async loop shape with 1-4 frames per step), with
# event logs and final pawn states compared. Pass line: "ASYNCSTEP SUMMARY: 2/2 checks passed". A mismatch logs the first
# state difference at step <s>. Keep -Seconds < 900 (the test match's TimeLimit). ~5 min per mode at 300 s in Release.
#
#   .\tools\fidelity\asyncstep-test.ps1 -Root work\ab\<target> -OutDir <dir> [-Modes TDM,CTF,DOM] [-Seconds 300] [-Seed 7] [-Config Release|Debug]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Modes = @("TDM", "CTF", "DOM"), [int]$Seconds = 300,
      [int]$Seed = 7, [ValidateSet("Release", "Debug")][string]$Config = "Release", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root $(if ($Config -eq "Debug") { "build\bin\wfc_rebuild.exe" } else { "build-release\bin\wfc_rebuild.exe" })
$H = Get-ExeHooks $exe
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "asyncstep.$id" $status $null $note $owner }
if (-not $H.Contains("WFC_ASYNCSTEPTEST")) { Res "hook" "UNKNOWN" "build has no WFC_ASYNCSTEPTEST" "Experimental"; Write-WfcReport $res (Join-Path $OutDir "report.json") | Out-Null; return }
$Modes = @($Modes | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
foreach ($m in $Modes) {
    $d = Join-Path $OutDir $m; New-Item -ItemType Directory -Force $d | Out-Null; $lg = Join-Path $d "wfc.log"
    if (-not $ReportOnly -and -not (Test-Path $lg)) {
        if (-not (Wait-WfcGpu)) { Res "$m.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
        $e = @{ WFC_ASYNCSTEP = "1"; WFC_ASYNCSTEPTEST = "1"; WFC_ASYNCSTEP_SECS = "$Seconds"; WFC_ASYNCSTEP_MODE = $m; WFC_SEED = "$Seed"; WFC_LOGEVERY = "0" }
        $null = Invoke-WfcExe $exe $d $e "run.log" ([Math]::Max(1800, $Seconds * 12))
    }
    if (-not (Test-Path $lg)) { continue }
    $lines = @(Select-String $lg -Pattern '\] ASYNCSTEP ' | ForEach-Object { $_.Line -replace '^\[[^\]]*\]\s*', '' })
    $summary = @($lines | Where-Object { $_ -match '^ASYNCSTEP SUMMARY' })[-1]
    $ok = $summary -match 'SUMMARY: (\d+)/(\d+) checks passed' -and $Matches[1] -eq $Matches[2]
    $detail = (@($lines | Where-Object { $_ -match 'first (state )?difference|lines \(A sync\)|main-thread cost|FAIL' } | Select-Object -First 4) -join " | ")
    Res "$m" $(if (-not $summary) { "UNKNOWN" } elseif ($ok) { "PASS" } else { "FAIL" }) ("{0} {1} s seed {2}: {3}; {4}" -f $m, $Seconds, $Seed, $(if ($summary) { $summary } else { "no SUMMARY line (crash / timeout?)" }), $detail) "Gameplay"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"ASYNCSTEP: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
