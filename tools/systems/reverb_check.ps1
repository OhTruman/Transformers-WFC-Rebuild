# MP room-reverb check (Systems, RE MP_REVERB_ROOMS_RE 54f4519): one windowed paced lockstep TDM per map (muted, NOINTERACT,
# playerbot, 4 v 5), the "ambient: entered zone" log (each SeqAct_Reverb switch the local pawn's Touch / UnTouched caused) compared
# with RE's per-map preset list (RE-Workspace/notes/data/mp_reverb/<MAP>.json, read only): every preset entered must be one of the
# map's authored REVERB_* presets; the switch count and the distinct rooms reached are reported. One exe at a time, killed by PID on
# timeout, waits for PERF_HOLD.
param([Parameter(Mandatory)] [string]$Bin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\reverb_check',
      [string]$MapList = 'MP_IAC_Streets,MP_IAC_Rust,MP_ORB_Debris', [int]$Seconds = 120)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
$re = 'F:\Transformers Rebuild\RE-Workspace\notes\data\mp_reverb'
foreach ($map in ($MapList -split ',')) {
    $dir = Join-Path $Out $map
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $envs = @{ WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
               WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_GPULOST_DIALOG_S = '0'; WFC_SKIPINTRO = '1';
               WFC_LOCKSTEP = '1'; WFC_FRAMELIMIT = '60'; WFC_PLAYERBOT = '1'; WFC_RT_MODE = '0'; WFC_LOGTIME = '1'; WFC_LEVELAUDIOLOG = '1'; WFC_BOOT = 'match';
               WFC_MATCH_SECONDS = "$Seconds"; WFC_SMOKE_FRAMES = '2000000'; WFC_MATCH_URL = "$($map)?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000" }
    foreach ($k in $envs.Keys) { Set-Item "env:$k" $envs[$k] }
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru
    $null = $p.Handle
    if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
    $log = Join-Path $dir 'wfc.log'
    $authored = @((Get-Content -Raw -LiteralPath (Join-Path $re "$map.json") | ConvertFrom-Json).presets.PSObject.Properties.Name)
    # the Kismet graph path (SYSTEMS_REVERB_LOG): every SeqAct_Reverb switch + the touch edges that caused it
    $sw = @(Select-String -LiteralPath $log -Pattern 'LEVELAUDIO reverb (\S+) -> (\S+)' | ForEach-Object { $_.Matches[0] })
    $presets = @($sw | ForEach-Object { $_.Groups[2].Value })
    $unt = @(Select-String -LiteralPath $log -Pattern 'LEVELAUDIO touch \S+ UnTouched').Count
    $bad = @($presets | Where-Object { $authored -notcontains $_ } | Sort-Object -Unique)
    Write-Host ("{0}: {1} reverb switches ({5} UnTouched edges), {2} distinct rooms ({3}); presets not in RE's list: {4}" -f $map, $sw.Count,
                @($presets | Sort-Object -Unique).Count, (($presets | Sort-Object -Unique) -join ', '), $(if ($bad.Count) { $bad -join ', ' } else { 'none' }), $unt)
    $sw | Select-Object -First 8 | ForEach-Object { Write-Host ("    {0} -> {1}" -f $_.Groups[1].Value, $_.Groups[2].Value) }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
