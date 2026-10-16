# Item 6 material A/B: the same lockstep bot match (RT off by default) with two render-data roots - the current one (base) and
# one whose map carries the regenerated material set (new). Frames differ only where the newly compiled materials draw.
# One exe at a time, muted, NOINTERACT, killed by PID on timeout.
param([Parameter(Mandatory)] [string]$Bin, [Parameter(Mandatory)] [string]$NewRender, [string]$BaseRender = 'F:/Transformers Rebuild/Rebuild/work/render',
      [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\mat_ab', [string]$Map = 'MP_IAC_Streets', [string]$RtMode = '0',
      [int]$From = 600, [int]$To = 3600, [int]$Stride = 30, [int]$Seconds = 80, [string]$Bots = 'BotsFriendly=7?BotsEnemy=8', [string]$ExtraEnv = '')
$ErrorActionPreference = 'Stop'
foreach ($name in 'base', 'new') {
    $dir = Join-Path $Out $name; $shots = Join-Path $dir 'shots'
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $shots | Out-Null
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'
    $env:WFC_GPULOST_DIALOG_S = '0'; $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_PLAYERBOT = '1'
    $env:WFC_RENDER_DATA = $(if ($name -eq 'new') { $NewRender } else { $BaseRender }); $env:WFC_RT_MODE = $RtMode
    $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = "$Seconds"; $env:WFC_SMOKE_FRAMES = '2000000'
    $env:WFC_MATCH_URL = "$Map" + "?GameModeTag=TDM?$Bots?PointsToWin=1000"
    foreach ($kv in ($ExtraEnv -split ';' | Where-Object { $_ })) { $k, $v = $kv -split '=', 2; Set-Item -LiteralPath "env:$k" -Value $v }
    $env:WFC_SHOTMATCH = ($shots -replace '\\', '/') + ",$From,$To,$Stride"
    $t0 = Get-Date
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -PassThru; $null = $p.Handle
    if (-not $p.WaitForExit(400000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "[$name] TIMEOUT (killed PID $($p.Id))" }
    $log = Join-Path $dir 'wfc.log'
    $fb = if (Test-Path $log) { @(Select-String -LiteralPath $log -Pattern 'using glTF fallback' | ForEach-Object { ($_.Line -replace '^.*material ', '') -replace ' has no.*| failed.*', '' }) } else { @() }
    Write-Host ("[{0}] exit {1} in {2:n0} s, {3} captures; glTF-fallback materials logged: {4}" -f $name, $p.ExitCode, ((Get-Date) - $t0).TotalSeconds,
                @(Get-ChildItem $shots -Filter *.bmp).Count, $fb.Count)
    $fb | Sort-Object -Unique | ForEach-Object { Write-Host "   fallback: $_" }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
