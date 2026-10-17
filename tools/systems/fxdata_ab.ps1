# FX data A/B (Systems): the same exe, two render data roots (e.g. current vs a pre-regen copy), RT off, lockstep TDM with
# WFC_RENDERSTATS (map FX per 120 frames: ms, sprites, mesh particles, live instances, runtime spawns, sprite draws).
# One exe at a time, killed by PID on timeout, waits for PERF_HOLD.
param([Parameter(Mandatory)] [string]$Bin, [Parameter(Mandatory)] [string]$RootA, [Parameter(Mandatory)] [string]$RootB,
      [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\fxdata_ab', [string]$MapList = 'MP_IAC_Berth,MP_UND_Gorge',
      [string]$Bots = 'BotsFriendly=31?BotsEnemy=32?ExtendedPlayers=1', [int]$Seconds = 60)
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
$arms = [ordered]@{ A = $RootA; B = $RootB }
foreach ($map in ($MapList -split ',')) {
    foreach ($arm in $arms.Keys) {
        $dir = Join-Path $Out "$map`_$arm"
        if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        while (Test-Path $hold) { Start-Sleep 2 }
        Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
        $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'
        $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_PLAYERBOT = '1'
        $env:WFC_RENDER_DATA = $arms[$arm]; $env:WFC_RT_MODE = '0'; $env:WFC_LOGTIME = '1'; $env:WFC_RENDERSTATS = '1'
        $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = "$Seconds"; $env:WFC_SMOKE_FRAMES = '2000000'
        $env:WFC_MATCH_URL = "$($map)?GameModeTag=TDM?$($Bots)?PointsToWin=1000"
        $t0 = Get-Date
        $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru; $null = $p.Handle
        if (-not $p.WaitForExit(600000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT (killed PID $($p.Id))" }
        Write-Host ("[{0} {1}] exit {2} in {3:n0} s" -f $map, $arm, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds)
    }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
