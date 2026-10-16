param([Parameter(Mandatory)] [string]$Template, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\homrocket_ab')
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
$arms = [ordered]@{ before = 'F:/Transformers Rebuild/Rebuild/work/render'; after = 'F:/Transformers Rebuild/Rebuild-Systems/work/render_after' }
foreach ($arm in $arms.Keys) {
    $d = Join-Path $Out $arm
    if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force }
    New-Item -ItemType Directory -Force "$d\shots" | Out-Null
    while (Test-Path $hold) { Start-Sleep 2 }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    $env:WFC_MUTE = '1'; $env:WFC_NOINTERACT = '1'; $env:WFC_NOMOUSE = '1'; $env:WFC_AUTODETECT = '0'; $env:WFC_SKIPINTRO = '1'; $env:WFC_GPULOST_DIALOG_S = '0'
    $env:WFC_LOCKSTEP = '1'; $env:WFC_SEED = '7'; $env:WFC_FLOWSEED = '7'; $env:WFC_RENDER_DATA = $arms[$arm]; $env:WFC_RT_MODE = '0'; $env:WFC_LOGTIME = '1'
    $env:WFC_EXPSTAGE = $Template; $env:WFC_BOOT = 'match'; $env:WFC_MATCH_SECONDS = '12'; $env:WFC_SMOKE_FRAMES = '2000000'
    $env:WFC_MATCH_URL = 'MP_IAC_Streets?GameModeTag=TDM?BotsFriendly=1?BotsEnemy=1?PointsToWin=1000'
    $env:WFC_SHOTMATCH = (($d -replace '\\', '/') + '/shots,170,290,3')
    $p = Start-Process -FilePath 'F:\Transformers Rebuild\Rebuild-Systems\work\eye\rel\bin\wfc_rebuild.exe' -WorkingDirectory $d -PassThru; $null = $p.Handle
    if (-not $p.WaitForExit(300000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "$arm killed" }
    Write-Host "$arm exit $($p.ExitCode)"
    Select-String -LiteralPath "$d\wfc.log" -Pattern 'EXPSTAGE|VelOverLife|render data:' | Select-Object -First 4 | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
}
Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
