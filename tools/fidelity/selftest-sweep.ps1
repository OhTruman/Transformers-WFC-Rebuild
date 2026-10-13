# SELF-TEST SWEEP (tier TARGETED, untimed; regression sweep 2026-10-13). Runs every in-product WFC_*TEST switch the build reads,
# one process each (env = 1, harness pins, windowed), and turns its log into a result: FAIL if any "<TAG> FAIL" / "test FAIL" /
# "FAIL:" line appears, PASS if PASS / SUMMARY lines appear and none fail, UNKNOWN on no verdict line (timeout / crash / the test
# needs a scenario this generic launch does not give it). Switches found by scanning the exe for "WFC_...TEST" strings, so a
# build without a test reports nothing for it. WFC_CRASHTEST (deliberate crash) and the parameter / mesh switches are skipped.
#
#   .\tools\fidelity\selftest-sweep.ps1 -Root work\ab\<target> -OutDir <dir> [-Only WFC_SETTINGSTEST,...] [-TimeoutSec 300] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Only = @(), [int]$TimeoutSec = 300, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"
$skip = @("WFC_CRASHTEST", "WFC_TESTDUMMY", "WFC_TESTMESH", "WFC_NOSPRITESTREAM")
$bytes = [IO.File]::ReadAllBytes($exe); $txt = [Text.Encoding]::ASCII.GetString($bytes)
$tests = @([regex]::Matches($txt, 'WFC_[A-Z0-9_]*TEST[A-Z0-9_]*') | ForEach-Object { $_.Value } | Select-Object -Unique |
           Where-Object { $_ -match 'TEST$' -and $skip -notcontains $_ })
$Only = @($Only | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
if ($Only.Count) { $tests = @($tests | Where-Object { $Only -contains $_ }) }
$res = New-WfcResults; $rows = New-Object System.Collections.Generic.List[object]
$failRe = '(?<![A-Za-z])(FAIL|FAILED)(?![A-Za-z])'; $passRe = '(?<![A-Za-z])(PASS|PASSED|SUMMARY)(?![A-Za-z])'
foreach ($t in ($tests | Sort-Object)) {
    $d = Join-Path $OutDir ($t.ToLower()); $lg = Join-Path $d "wfc.log"; $code = $null; $t0 = Get-Date
    if (-not $ReportOnly -and -not (Test-Path $lg)) { $code = Invoke-WfcExe $exe $d @{ $t = "1"; WFC_NO_WATCHDOG = "1" } "run.log" $TimeoutSec }
    $secs = [int]((Get-Date) - $t0).TotalSeconds
    $lines = @(if (Test-Path $lg) { [IO.File]::ReadLines($lg) })
    $tag = ($t -replace '^WFC_', '' -replace 'TEST$', '')
    $rel = @($lines | Where-Object { $_ -match "(?i)$tag" -or $_ -match '(?i)test' })
    $fails = @($rel | Where-Object { $_ -cmatch $failRe -and $_ -notmatch '(?i)\b0 fail' })
    $pass = @($rel | Where-Object { $_ -cmatch $passRe })
    $crash = @($lines | Where-Object { $_ -match '(?i)unhandled exception|access violation|crash dump|fatal' })
    $st = if ($fails.Count) { "FAIL" } elseif ($crash.Count) { "FAIL" } elseif ($pass.Count) { "PASS" } else { "UNKNOWN" }
    $note = "{0}: {1} pass / summary lines, {2} fail lines{3}{4}; exit {5}, {6} s{7}" -f $t, $pass.Count, $fails.Count,
        $(if ($fails.Count) { " - " + (($fails | Select-Object -First 3 | ForEach-Object { ($_ -replace '^\S+\s+\S+\s+', '').Substring(0, [Math]::Min(160, ($_ -replace '^\S+\s+\S+\s+', '').Length)) }) -join " | ") } else { "" }),
        $(if ($crash.Count) { "; CRASH: " + ($crash | Select-Object -First 1) } else { "" }), $code, $secs,
        $(if (-not $fails.Count -and $pass.Count) { "; last: " + (($pass | Select-Object -Last 1) -replace '^\S+\s+\S+\s+', '') } else { "" })
    Add-WfcResult $res "selftest.$($t.ToLower())" $st $null $note "Experimental"
    $rows.Add([pscustomobject][ordered]@{ test = $t; status = $st; pass_lines = $pass.Count; fail_lines = $fails.Count; exit = $code; secs = $secs })
}
Write-WfcCsv $rows (Join-Path $OutDir "selftests.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"SELF-TEST SWEEP: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$rows | Format-Table -AutoSize | Out-String -Width 200
