# Late-join arm (Experimental 2026-10-23): host (WFC_UDPHOST, 2 seats + WFC_UDPRESERVE=1), an on-time member, and a member that joins
# while the match runs (WFC_UDPLATEJOIN=1) after -LateAfter s; direct loopback (no shim); hash every tick; kills only its own PIDs.
param([Parameter(Mandatory)][string]$Bin, [Parameter(Mandatory)][string]$Out, [int]$Seconds = 90, [int]$LateAfter = 25, [string]$Map = 'MP_IAC_Streets')
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }; New-Item -ItemType Directory -Force $Out | Out-Null
$procs = @{}
function Start-Peer($role, $extra) {
    $dir = Join-Path $Out $role; New-Item -ItemType Directory -Force $dir | Out-Null
    $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
    $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7'; WFC_SKIPINTRO = '1';
            WFC_PLAYERBOT = '1'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render';
            WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_HASH_EVERY = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv') }
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle
    Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    $script:procs[$role] = $p
}
Start-Peer 'host' @{ WFC_UDPHOST = '14300'; WFC_UDPSEATS = '2'; WFC_UDPRESERVE = '1' }
Start-Sleep -Milliseconds 500
Start-Peer 'member' @{ WFC_UDPJOIN = '127.0.0.1:14300'; WFC_UDPBIND = '14301' }
Start-Sleep -Seconds $LateAfter
Start-Peer 'late' @{ WFC_UDPJOIN = '127.0.0.1:14300'; WFC_UDPBIND = '14302'; WFC_UDPLATEJOIN = '1' }
foreach ($r in 'host', 'member', 'late') { if (-not $procs[$r].WaitForExit(($Seconds + 240) * 1000)) { Stop-Process -Id $procs[$r].Id -Force; Write-Host "  $r TIMEOUT (killed PID $($procs[$r].Id))" } }
foreach ($r in 'host', 'member', 'late') {
    $log = Join-Path $Out "$r\wfc.log"
    $done = Select-String -LiteralPath $log -Pattern 'UDPMATCH (done|<-|join|late)[^|]{0,120}' -ErrorAction SilentlyContinue | Select-Object -Last 3 | % { $_.Matches[0].Value }
    Write-Host "$r exit $($procs[$r].ExitCode): $($done -join ' / ')"
}
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -like "$Bin*" }).Count)
