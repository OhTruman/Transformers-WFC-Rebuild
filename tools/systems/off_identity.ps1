# Off / Faithful identity A/B (Systems): base vs patched exe. (1) headless BOTTEST + WFC_SIMHASH (null render, seed 7): the per-step
# hashes must match; (2) one lockstep TDM 4 v 5 + playerbot per map, RT off (WFC_RT_MODE=0), windowed, muted, NOINTERACT, captures
# every 150 steps: every capture must be byte-identical. One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([Parameter(Mandatory)] [string]$BaseBin, [Parameter(Mandatory)] [string]$NewBin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\off_identity',
      [string]$MapList = 'MP_IAC_Streets', [int]$Seconds = 40, [string]$RtMode = '0', [switch]$NoSimhash)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
function Run([string]$bin, [string]$dir, [hashtable]$envs) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    if ($envs.ContainsKey('WFC_SHOTMATCH')) { New-Item -ItemType Directory -Force -Path (Join-Path $dir 'shots') | Out-Null }   # the exe does not create it
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0' }
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    foreach ($k in $envs.Keys) { if ($envs[$k] -ne '') { Set-Item "env:$k" $envs[$k] } }
    # windowed runs as eyepass_run.ps1 launches them (a redirected / no-new-window start drew no frames: no captures)
    $p = if ($envs.ContainsKey('WFC_NULLRENDER')) { Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -RedirectStandardOutput "$dir\out.txt" -RedirectStandardError "$dir\err.txt" -NoNewWindow -PassThru }
         else { Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru }
    $null = $p.Handle
    if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
}
$arms = [ordered]@{ base = $BaseBin; new = $NewBin }
if (-not $NoSimhash) {
    foreach ($arm in $arms.Keys) { Run $arms[$arm] (Join-Path $Out "simhash_$arm") @{ WFC_NULLRENDER = '1'; WFC_BOTTEST = '1'; WFC_SIMHASH = '1' } }
    $a = @(Select-String -LiteralPath (Join-Path $Out 'simhash_base\wfc.log') -Pattern 'SIMHASH \d+ [0-9a-f]{16}$' | ForEach-Object { $_.Line -replace '^.*SIMHASH', '' })
    $b = @(Select-String -LiteralPath (Join-Path $Out 'simhash_new\wfc.log') -Pattern 'SIMHASH \d+ [0-9a-f]{16}$' | ForEach-Object { $_.Line -replace '^.*SIMHASH', '' })
    $diff = 0; for ($i = 0; $i -lt [Math]::Min($a.Count, $b.Count); $i++) { if ($a[$i] -ne $b[$i]) { $diff++ } }
    Write-Host ("SIMHASH base {0} steps, new {1}, differing {2}: {3}" -f $a.Count, $b.Count, $diff, $(if ($diff -eq 0 -and $a.Count -eq $b.Count -and $a.Count) { 'IDENTICAL' } else { 'DIFFER' }))
    foreach ($arm in $arms.Keys) { Select-String -LiteralPath (Join-Path $Out "simhash_$arm\wfc.log") -Pattern 'BOTTEST SUMMARY' | ForEach-Object { Write-Host "  $arm $($_.Line.Trim())" } }
}
foreach ($map in ($MapList -split ',')) {
    foreach ($arm in $arms.Keys) {
        $dir = Join-Path $Out "$map`_$arm"
        Run $arms[$arm] $dir @{ WFC_SKIPINTRO = '1'; WFC_LOCKSTEP = '1'; WFC_PLAYERBOT = '1'; WFC_RT_MODE = $RtMode; WFC_LOGTIME = '1'; WFC_MUZZLELOG = '1';
                                 WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000'; WFC_MATCH_URL = "$($map)?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000";
                                 WFC_SHOTMATCH = ((Join-Path $dir 'shots') -replace '\\', '/') + ',150,4200,150' }
    }
    $bs = @(Get-ChildItem -LiteralPath (Join-Path $Out "$map`_base\shots") -Filter *.bmp -ErrorAction SilentlyContinue)
    $same = 0; $differ = @()
    foreach ($f in $bs) {
        $n = Join-Path $Out "$map`_new\shots\$($f.Name)"
        if ((Test-Path -LiteralPath $n) -and ((Get-FileHash -LiteralPath $f.FullName).Hash -eq (Get-FileHash -LiteralPath $n).Hash)) { $same++ } else { $differ += $f.Name }
    }
    $fx = @(Select-String -LiteralPath (Join-Path $Out "$map`_new\wfc.log") -Pattern 'FX LIGHT ').Count
    $rtl = Select-String -LiteralPath (Join-Path $Out "$map`_new\wfc.log") -Pattern 'RT-era lights \(first Realistic frame\)' | Select-Object -First 1
    Write-Host ("{0} RT mode {1}: captures {2}, identical {3}{4}; new-arm FX LIGHT lines {5}; RT-era line: {6}" -f $map, $RtMode, $bs.Count, $same,
                $(if ($differ.Count) { ", DIFFER: $($differ -join ' ')" } else { '' }), $fx, $(if ($rtl) { $rtl.Line.Trim() } else { 'none' }))
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
