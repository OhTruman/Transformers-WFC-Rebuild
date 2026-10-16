# Voice-pool A/B (Systems, eye-pass item 1): base vs patched exe. (1) headless BOTTEST with WFC_SIMHASH (null render, seed 7):
# the per-step hashes must match; (2) per map a 70 s lockstep TDM 4 v 5 + playerbot, RT off, windowed, muted, NOINTERACT, with
# WFC_AMBLOG (voices / max / dropped / stolen / virtual every 0.5 s) + WFC_CUELOG; reported: peak real voices, final dropped /
# stolen counts, the local "no channel" warnings. One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([switch]$NoSimhash, [switch]$Realtime, [switch]$Paced, [Parameter(Mandatory)] [string]$BaseBin, [Parameter(Mandatory)] [string]$NewBin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\voice_ab',
      [string]$MapList = 'MP_IAC_Streets,MP_UND_Gorge', [string]$Bots = 'BotsFriendly=4?BotsEnemy=5', [int]$Seconds = 70)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
function Run([string]$bin, [string]$dir, [hashtable]$envs) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0' }
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    foreach ($k in $envs.Keys) { if ($envs[$k] -ne '') { Set-Item "env:$k" $envs[$k] } }
    $p = Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -RedirectStandardOutput "$dir\out.txt" -RedirectStandardError "$dir\err.txt" -NoNewWindow -PassThru
    $null = $p.Handle
    if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
}
$arms = [ordered]@{ base = $BaseBin; new = $NewBin }
if (-not $NoSimhash) { foreach ($arm in $arms.Keys) { Run $arms[$arm] (Join-Path $Out "simhash_$arm") @{ WFC_NULLRENDER = '1'; WFC_BOTTEST = '1'; WFC_SIMHASH = '1' } } }
$a = @(Select-String -LiteralPath (Join-Path $Out 'simhash_base\wfc.log') -Pattern 'SIMHASH \d+ [0-9a-f]{16}$' | ForEach-Object { $_.Line -replace '^.*SIMHASH', '' })
$b = @(Select-String -LiteralPath (Join-Path $Out 'simhash_new\wfc.log') -Pattern 'SIMHASH \d+ [0-9a-f]{16}$' | ForEach-Object { $_.Line -replace '^.*SIMHASH', '' })
$diff = 0; for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) { if ($a[$i] -ne $b[$i]) { $diff++ } }
Write-Host ("SIMHASH base {0} steps, new {1}, differing {2}: {3}" -f $a.Count, $b.Count, $diff, $(if ($diff -eq 0 -and $a.Count -eq $b.Count -and $a.Count) { 'IDENTICAL' } else { 'DIFFER' }))
foreach ($arm in $arms.Keys) { Select-String -LiteralPath (Join-Path $Out "simhash_$arm\wfc.log") -Pattern 'BOTTEST SUMMARY' | ForEach-Object { Write-Host "  $arm $($_.Line.Trim())" } }
foreach ($map in ($MapList -split ',')) {
    foreach ($arm in $arms.Keys) {
        $dir = Join-Path $Out "$map`_$arm"
        Run $arms[$arm] $dir @{ WFC_SKIPINTRO = '1'; WFC_LOCKSTEP = $(if ($Realtime) { '' } else { '1' }); WFC_FRAMELIMIT = $(if ($Paced) { '60' } else { '' }); WFC_PLAYERBOT = '1'; WFC_RT_MODE = '0'; WFC_LOGTIME = '1'; WFC_CUELOG = '1'; WFC_AMBLOG = '1';
                                 WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000'; WFC_MATCH_URL = "$($map)?GameModeTag=TDM?$($Bots)?PointsToWin=1000" }
        $log = Join-Path $dir 'wfc.log'
        $amb = @(Select-String -LiteralPath $log -Pattern 'voices=(\d+) \(max (\d+), dropped (\d+), stolen (\d+), virtual (\d+)\)' | ForEach-Object { $_.Matches[0] })
        $last = $amb | Select-Object -Last 1
        $peak = ($amb | ForEach-Object { [int]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum
        $full = @($amb | Where-Object { [int]$_.Groups[1].Value -ge 96 }).Count
        $vmax = ($amb | ForEach-Object { [int]$_.Groups[5].Value } | Measure-Object -Maximum).Maximum
        $nc = @(Select-String -LiteralPath $log -Pattern 'no channel for the local player').Count
        Write-Host ("{0,-16} {1,-4}: samples {2}, real voices peak {3}, at 96 in {4} samples; final dropped {5} stolen {6}; virtual max {7}; local 'no channel' {8}" -f $map, $arm,
                    $amb.Count, $peak, $full, $last.Groups[3].Value, $last.Groups[4].Value, $vmax, $nc)
    }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
