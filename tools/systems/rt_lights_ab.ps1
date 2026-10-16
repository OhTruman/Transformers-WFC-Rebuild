# RT-era light A/B (Systems RT queue items 1 / 3): the same lockstep Streets bot match in Realistic (WFC_RT_MODE=2, direct boot)
# with three arms - base (no RT-era lights), fx (WFC_FXLIGHTS=1), head (headlights on) - captured by match step (WFC_SHOTMATCH).
# One RT-on exe at a time, muted, NOINTERACT; each run waits for the previous exe to exit (killed by PID on timeout).
param([Parameter(Mandatory)] [string]$Bin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\rt_lights_ab', [string]$Map = 'MP_IAC_Streets',
      [int]$From = 600, [int]$To = 2400, [int]$Stride = 20, [int]$Seconds = 50, [string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$arms = [ordered]@{ base = @{ WFC_FXLIGHTS = '0'; WFC_HEADLIGHTS = '0' }; fx = @{ WFC_FXLIGHTS = '1'; WFC_HEADLIGHTS = '0' }; head = @{ WFC_FXLIGHTS = '0'; WFC_HEADLIGHTS = '1' } }
foreach ($name in @($arms.Keys | Where-Object { -not $Only.Count -or $Only -contains $_ })) {
    $dir = Join-Path $Out $name; $shots = Join-Path $dir 'shots'
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $shots | Out-Null
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'
    $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_PLAYERBOT = '1'
    $env:WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; $env:WFC_MUZZLELOG = '1'; $env:WFC_RT_MODE = '2'
    $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = "$Seconds"; $env:WFC_SMOKE_FRAMES = '2000000'
    $env:WFC_MATCH_URL = "$Map" + '?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000'
    foreach ($k in $arms[$name].Keys) { Set-Item "env:$k" $arms[$name][$k] }
    $env:WFC_SHOTMATCH = ($shots -replace '\\', '/') + ",$From,$To,$Stride"
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru; $null = $p.Handle
    if (-not $p.WaitForExit(300000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "[$name] TIMEOUT (killed PID $($p.Id))" }
    $log = Join-Path $dir 'wfc.log'
    $lines = if (Test-Path $log) { @(Select-String -LiteralPath $log -Pattern 'MUZZLE LIGHTS |HEADLIGHTS |RT mode|ray tracing mode' | Select-Object -Last 6 | ForEach-Object { $_.Line }) } else { @('no log') }
    $fx = if (Test-Path $log) { (Select-String -LiteralPath $log -Pattern 'FX LIGHT ').Count } else { 0 }
    Write-Host ("[{0}] exit {1} in {2:n0} s, {3} captures, {4} FX LIGHT lines" -f $name, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds, @(Get-ChildItem $shots -Filter *.bmp).Count, $fx)
    $lines | ForEach-Object { Write-Host "   $_" }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
