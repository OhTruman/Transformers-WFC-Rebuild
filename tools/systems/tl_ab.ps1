# Transient lights windowed A/B (SLOT B): the same lockstep bot match, lights on vs WFC_NOTRANSIENTLIGHTS=1, at RT_MODE 2 and 0.
# Muted + NOINTERACT; captures by match step (WFC_SHOTMATCH). Exe copied from the integration build (read only there).
param([string]$Bin = 'F:\Transformers Rebuild\Rebuild-Systems\work\tl_ab\bin', [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\tl_ab',
      [string]$Map = 'MP_KON_Molten', [int]$From = 600, [int]$To = 1800, [int]$Stride = 20, [string[]]$Only = @())
$ErrorActionPreference = 'Stop'
$runs = [ordered]@{ rt2_on = @('2', $false); rt2_off = @('2', $true); rt0_on = @('0', $false); rt0_off = @('0', $true) }
foreach ($name in @($runs.Keys | Where-Object { -not $Only.Count -or $Only -contains $_ })) {
    $mode, $off = $runs[$name]
    $dir = Join-Path $Out $name; $shots = Join-Path $dir 'shots'
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $shots | Out-Null
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'
    $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_PLAYERBOT = '1'
    $env:WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; $env:WFC_MUZZLELOG = '1'
    $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = '45'; $env:WFC_SMOKE_FRAMES = '2000000'
    $env:WFC_MATCH_URL = "$Map" + '?GameModeTag=TDM?BotsFriendly=4?BotsEnemy=5?PointsToWin=1000'
    $env:WFC_RT_MODE = $mode
    if ($off) { $env:WFC_NOTRANSIENTLIGHTS = '1' }
    $env:WFC_SHOTMATCH = ($shots -replace '\\', '/') + ",$From,$To,$Stride"
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru; $null = $p.Handle
    if (-not $p.WaitForExit(240000)) { Stop-Process -Id $p.Id -Force; Write-Host "[$name] TIMEOUT (killed PID $($p.Id))" }
    $n = @(Get-ChildItem -LiteralPath $shots -Filter *.bmp -ErrorAction SilentlyContinue).Count
    $log = Join-Path $dir 'wfc.log'
    $ml = if (Test-Path $log) { @(Select-String -LiteralPath $log -Pattern 'MUZZLE LIGHTS ' | ForEach-Object { $_.Line -replace '^.*MUZZLE LIGHTS ', '' }) } else { @() }
    Write-Host ("[{0}] exit {1} in {2:n0} s, {3} captures; {4}" -f $name, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds, $n, ($ml -join ' | '))
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
