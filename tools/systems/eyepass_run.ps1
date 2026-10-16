# Eye-pass audit runs (Systems): one lockstep bot match per map, RT off, windowed, muted (the mix / cue logic still runs),
# NOINTERACT, with the effect / audio diagnostics on (cue log, audio timing, muzzle log, level / match audio logs, log time)
# and a few captures per map. One exe at a time, killed by PID on timeout; waits for PERF_HOLD.
param([Parameter(Mandatory)] [string]$Bin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\eyepass',
      [string]$MapList = 'MP_IAC_Streets,MP_IAC_Rust,MP_KON_Molten,MP_UND_Gorge,MP_ORB_Debris', [int]$Seconds = 70, [string]$Seed = '7')
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
foreach ($map in ($MapList -split ',')) {
    $dir = Join-Path $Out $map; $shots = Join-Path $dir 'shots'
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $shots | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'
    $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = $Seed; $env:WFC_FLOWSEED = $Seed; $env:WFC_PLAYERBOT = '1'
    $env:WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; $env:WFC_RT_MODE = '0'
    $env:WFC_LOGTIME = '1'; $env:WFC_CUELOG = '1'; $env:WFC_AUDIOTIME = '1'; $env:WFC_MUZZLELOG = '1'; $env:WFC_LEVELAUDIOLOG = '1'; $env:WFC_MATCHAUDIOLOG = '1'
    $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = "$Seconds"; $env:WFC_SMOKE_FRAMES = '2000000'
    $env:WFC_MATCH_URL = "$map" + '?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000'
    $env:WFC_SHOTMATCH = ($shots -replace '\\', '/') + ',600,4200,600'
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru; $null = $p.Handle
    $killed = $false
    while (-not $p.HasExited) {
        if (Test-Path $hold) { & taskkill.exe /T /F /PID $p.Id | Out-Null; $killed = $true; Write-Host "[$map] stopped for the hold"; break }
        if (((Get-Date) - $t0).TotalSeconds -gt 400) { & taskkill.exe /T /F /PID $p.Id | Out-Null; $killed = $true; Write-Host "[$map] TIMEOUT (killed PID $($p.Id))"; break }
        Start-Sleep 1
    }
    Write-Host ("[{0}] exit {1} in {2:n0} s{3}" -f $map, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds, $(if ($killed) { ' (killed)' } else { '' }))
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
