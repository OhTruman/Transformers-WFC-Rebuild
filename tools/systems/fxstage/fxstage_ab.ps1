# FX-order staging A/B (tools/systems/fxstage; build both exes with fxstage_hook.py applied, pass -BinA / -BinB). Was: smoke-behind-player A/B (user playtest 2026-10-14): the same seeded playerbot match on Streets, RT off, windowed, muted,
# captured by match step (WFC_SHOTMATCH) with the base exe and the patched exe; then work/smoke_diff.py pairs the frames.
param([string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\smoke_ab', [int]$From = 300, [int]$To = 3300, [int]$Stride = 15,
      [int]$Seconds = 75, [switch]$Repro, [string]$Only = '', [string]$ExtraEnv = '', [string]$BinA = '', [string]$BinB = '')
$ErrorActionPreference = 'Stop'
$runs = [ordered]@{
    base    = $(if ($BinA) { $BinA } else { 'F:\Transformers Rebuild\Rebuild-Systems\work\fxstage_A\build\bin' })
    patched = $(if ($BinB) { $BinB } else { 'F:\Transformers Rebuild\Rebuild-Systems\work\fxstage_B\build\bin' })
}
foreach ($name in @($runs.Keys | Where-Object { -not $Only -or $_ -eq $Only })) {
    $dir = Join-Path $Out $name
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    $bin = Join-Path $dir 'bin'; $shots = Join-Path $dir 'shots'
    New-Item -ItemType Directory -Force -Path $bin, $shots | Out-Null
    Copy-Item -Path (Join-Path $runs[$name] '*') -Destination $bin -Recurse -Force -Exclude 'wfc.log', 'wfc_profile.ini', 'wfc_crash_*'
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_FXSTAGE = '1'; $env:WFC_LOBBY_OPTIONS = 'BotsAutobot=4;BotsDecepticon=4'; $env:WFC_LOCKSTEP = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_NOMOUSE = '1'; $env:WFC_SKIPINTRO = '1'; $env:WFC_GPULOST_DIALOG_S = '0'
    $env:WFC_BOOT = 'frontend'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_RT_MODE = '0'
    # (verify_slim's route: party lobby -> private TDM on Streets (508) -> InGame, then $Seconds of play)
    $env:WFC_FRONTEND_SCRIPT = "wait:frontend;wait:ui=FrontEnd;wait:t=2;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;wait:ui=InLobby;wait:t=1;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;wait:ui=InLobby;wait:t=1.5;call:Online.SetSelectedMapID,508;wait:t=1;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=$Seconds;quit"
    foreach ($kv in ($ExtraEnv -split ';' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; Set-Item -LiteralPath "env:$k" -Value $v }
    $env:WFC_SHOTMATCH = ($shots -replace '\\', '/') + ",$From,$To,$Stride"
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $bin 'wfc_rebuild.exe') -WorkingDirectory $bin -PassThru
    if (-not $p.WaitForExit(600000)) { Stop-Process -Id $p.Id -Force; Write-Host "[$name] TIMEOUT (killed PID $($p.Id))" }
    $n = @(Get-ChildItem -LiteralPath $shots -Filter *.bmp).Count
    Write-Host ("[{0}] exit {1} in {2:n0} s, {3} captures" -f $name, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds, $n)
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
