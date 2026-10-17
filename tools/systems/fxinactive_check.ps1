# SecondsBeforeInactive card rows (Systems, SYSTEMS_FX_INACTIVE): per map three windowed lockstep TDM 4 v 5 + playerbot runs, RT off,
# muted, NOINTERACT, captures every 150 steps: base (af4d0731), new with WFC_FXINACTIVE=0, new with WFC_FXINACTIVE=1 + the resume log.
# (i) counters: throttled share of instance-ticks (UE3 PSC tick throttle: 10 Hz unrendered > 0.3 s, 1 Hz past SBI), slow ticks, MAPFXPROF particle build ms (off vs on);
# (ii) identity: base vs off byte-identical (required); off vs on: differing frames with crops (work/.../crops_<map>) for the eye
#      review, and none before the first logged resume; (iii) the crops are the eye check (turning away and back happens in play).
# One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([Parameter(Mandatory)] [string]$BaseBin, [Parameter(Mandatory)] [string]$NewBin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\fxinactive_check',
      [string]$MapList = 'MP_IAC_Streets,MP_KON_Molten', [int]$Seconds = 70)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
function Run([string]$bin, [string]$dir, [hashtable]$envs) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $dir 'shots') | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $base = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0'; WFC_NOTIFYFX = '0'; WFC_MUZZLEENERGON = '0' }   # isolate from SYSTEMS_NOTIFY_FX / SYSTEMS_MUZZLE_ENERGON
    foreach ($k in $base.Keys) { Set-Item "env:$k" $base[$k] }
    foreach ($k in $envs.Keys) { if ($envs[$k] -ne '') { Set-Item "env:$k" $envs[$k] } }
    $p = Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru   # windowed (draws frames)
    $null = $p.Handle
    if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
}
foreach ($map in ($MapList -split ',')) {
    $arms = [ordered]@{ base = @($BaseBin, ''); off = @($NewBin, '0'); on = @($NewBin, '1') }
    foreach ($arm in $arms.Keys) {
        $dir = Join-Path $Out "$map`_$arm"
        Run $arms[$arm][0] $dir @{ WFC_SKIPINTRO = '1'; WFC_LOCKSTEP = '1'; WFC_PLAYERBOT = '1'; WFC_RT_MODE = '0'; WFC_LOGTIME = '1'; WFC_MAPFXPROF = '1';
                                   WFC_FXINACTIVE = $arms[$arm][1]; WFC_FXINACTIVELOG = $(if ($arm -eq 'on') { '1' } else { '' });
                                   WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000'; WFC_MATCH_URL = "$($map)?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000";
                                   WFC_SHOTMATCH = ((Join-Path $dir 'shots') -replace '\\', '/') + ',150,4200,150' }
    }
    $on = Join-Path $Out "$map`_on\wfc.log"
    $fz = @(Select-String -LiteralPath $on -Pattern 'wfc fx inactive: (\d+) of (\d+)' | ForEach-Object { $_.Matches[0] })
    $frozen = ($fz | ForEach-Object { [long]$_.Groups[1].Value } | Measure-Object -Sum).Sum; $total = ($fz | ForEach-Object { [long]$_.Groups[2].Value } | Measure-Object -Sum).Sum
    $res = @(Select-String -LiteralPath $on -Pattern 'FXINACTIVE (resume|slow tick) ')
    function Build([string]$log) { $v = @(Select-String -LiteralPath $log -Pattern 'particle build ([\d.]+)' | ForEach-Object { [double]$_.Matches[0].Groups[1].Value }); if ($v.Count) { ($v | Measure-Object -Average).Average } else { -1 } }
    Write-Host ("(i) {0}: throttled {1} of {2} instance-ticks ({3:P1}); slow (1 Hz) ticks {4}; MAPFXPROF particle build ms/frame off {5:N3} on {6:N3}" -f $map, $frozen, $total,
                $(if ($total) { $frozen / $total } else { 0 }), $res.Count, (Build (Join-Path $Out "$map`_off\wfc.log")), (Build $on))
    $res | Select-Object -First 5 | ForEach-Object { Write-Host "    $($_.Line -replace '^.*FXINACTIVE', 'FXINACTIVE')" }
    Write-Host "(ii) $map base vs off (must be identical):"
    & $py -I (Join-Path $PSScriptRoot 'shot_diff_crops.py') (Join-Path $Out "$map`_base\shots") (Join-Path $Out "$map`_off\shots") (Join-Path $Out "crops_base_off_$map") | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" }
    Write-Host "(ii) $map off vs on (expected: differences only after resumes; crops for the eye review):"
    & $py -I (Join-Path $PSScriptRoot 'shot_diff_crops.py') (Join-Path $Out "$map`_off\shots") (Join-Path $Out "$map`_on\shots") (Join-Path $Out "crops_off_on_$map") | ForEach-Object { Write-Host "    $_" }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
