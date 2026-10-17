# Authored notify FX check + the CUELOG list (Systems, SYSTEMS_NOTIFY_FX): base vs new exe, one windowed paced lockstep TDM each
# (lockstep + WFC_FRAMELIMIT=60, RT off, muted, NOINTERACT, playerbot, 4 v 5), captures every 150 steps, WFC_NOTIFYLOG + WFC_CUELOG.
# Reports: the new arm's NOTIFYFX lines (local / participant, per template), the captures that differ (crops via
# shot_diff_crops.py) and every cue that started in the base arm (the CUELOG list for the E-class check), written to
# <Out>\cues_started.txt. One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([switch]$MuzzlePair, [Parameter(Mandatory)] [string]$BaseBin, [Parameter(Mandatory)] [string]$NewBin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\notifyfx_check',
      [string]$Map = 'MP_IAC_Streets', [int]$Seconds = 90)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
function Run([string]$bin, [string]$dir, [hashtable]$envs) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dir 'shots') | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0'; WFC_FXINACTIVE = '0'; WFC_MUZZLEENERGON = '0' }   # isolate from the throttle / muzzle colour patches
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    foreach ($k in $envs.Keys) { if ($envs[$k] -ne '') { Set-Item "env:$k" $envs[$k] } }
    $p = Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru   # windowed (draws frames)
    $null = $p.Handle
    if (-not $p.WaitForExit(900000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
}
foreach ($arm in 'base', 'new') {
    $dir = Join-Path $Out $arm
    # -MuzzlePair: both arms on the new exe, the muzzle light colour authored (base) vs the owner's energon (new)
    $bin = if ($MuzzlePair -or $arm -eq 'new') { $NewBin } else { $BaseBin }
    $mz = if ($MuzzlePair -and $arm -eq 'new') { '1' } else { '' }
    Run $bin $dir @{ WFC_MUZZLEENERGON = $mz; WFC_SKIPINTRO = '1'; WFC_LOCKSTEP = '1'; WFC_FRAMELIMIT = '60'; WFC_PLAYERBOT = '1'; WFC_RT_MODE = '0';
        WFC_LOGTIME = '1'; WFC_CUELOG = '1'; WFC_NOTIFYLOG = '1'; WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000';
        WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000"; WFC_SHOTMATCH = ((Join-Path $dir 'shots') -replace '\\', '/') + ',150,5400,150' }
}
$nl = Join-Path $Out 'new\wfc.log'
$nf = @(Select-String -LiteralPath $nl -Pattern 'NOTIFYFX (local|participant) (\S+) @(\S*)' | ForEach-Object { $_.Matches[0] })
Write-Host ("new arm NOTIFYFX lines: {0} (local {1}, participant {2})" -f $nf.Count, @($nf | Where-Object { $_.Groups[1].Value -eq 'local' }).Count, @($nf | Where-Object { $_.Groups[1].Value -eq 'participant' }).Count)
$nf | Group-Object { $_.Groups[2].Value } | Sort-Object Count -Descending | Select-Object -First 12 | ForEach-Object { Write-Host ("   {0,5}  {1}" -f $_.Count, $_.Name) }
Write-Host "captures base vs new:"
& $py -I (Join-Path $PSScriptRoot 'shot_diff_crops.py') (Join-Path $Out 'base\shots') (Join-Path $Out 'new\shots') (Join-Path $Out 'crops') | Select-Object -Last 6 | ForEach-Object { Write-Host "   $_" }
$cues = Select-String -LiteralPath (Join-Path $Out 'base\wfc.log') -Pattern '\bCUE (\S+) ev' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique
$cues | Set-Content -LiteralPath (Join-Path $Out 'cues_started.txt') -Encoding utf8
Write-Host ("base arm distinct cues started: {0} -> {1}" -f @($cues).Count, (Join-Path $Out 'cues_started.txt'))
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
