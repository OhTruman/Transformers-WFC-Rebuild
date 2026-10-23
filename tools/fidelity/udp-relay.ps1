# Relay-fallback arm (Experimental 2026-10-23, daf03569+): a local tools/net/lobby_service.py (+ its relay port), a host that registers
# (WFC_UDPHOST + WFC_UDPSERVICE) and a member that joins by the logged 6-letter code (WFC_UDPJOIN=<code>), with WFC_NET_DIRECT_DROP=1 on
# both so they can only reach each other through the relay. Headless, loopback, hash every tick; kills only its own PIDs.
param([Parameter(Mandatory)][string]$Bin, [Parameter(Mandatory)][string]$Out, [Parameter(Mandatory)][string]$Service, [int]$Seconds = 70,
      [int]$SvcPort = 14510, [int]$Port = 14500, [string]$Map = 'MP_IAC_Streets')
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }; New-Item -ItemType Directory -Force $Out | Out-Null
$svc = Start-Process -FilePath $py -ArgumentList @('-I', "`"$Service`"", '--bind', "127.0.0.1:$SvcPort", '--log', "`"$(Join-Path $Out 'service.log')`"") -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $Out 'service.out') -RedirectStandardError (Join-Path $Out 'service.err')
Start-Sleep -Milliseconds 1500
$procs = @{}
function Start-Peer($role, $extra) {
    $dir = Join-Path $Out $role; New-Item -ItemType Directory -Force $dir | Out-Null
    $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
    $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7'; WFC_SKIPINTRO = '1';
            WFC_PLAYERBOT = '1'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_UDPSEATS = '2';
            WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_HASH_EVERY = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv');
            WFC_UDPSERVICE = "127.0.0.1:$SvcPort"; WFC_NET_DIRECT_DROP = '1' }
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle
    Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    $script:procs[$role] = $p
}
Start-Peer 'host' @{ WFC_UDPHOST = "$Port" }
$hl = Join-Path $Out 'host\wfc.log'; $code = $null; $t0 = Get-Date
while (-not $code -and ((Get-Date) - $t0).TotalSeconds -lt 30) {
    Start-Sleep -Milliseconds 300
    $m = Select-String -LiteralPath $hl -Pattern 'lobby service code ([A-Z0-9]{6})' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($m) { $code = $m.Matches[0].Groups[1].Value }
}
if (-not $code) { Write-Host "no service code from the host in 30 s" } else {
    Write-Host "host code $code"
    Start-Peer 'member' @{ WFC_UDPJOIN = $code; WFC_UDPBIND = "$($Port + 1)" }
}
foreach ($r in @($procs.Keys)) { if (-not $procs[$r].WaitForExit(($Seconds + 240) * 1000)) { Stop-Process -Id $procs[$r].Id -Force; Write-Host "  $r TIMEOUT (killed PID $($procs[$r].Id))" } }
if (-not $svc.HasExited) { Stop-Process -Id $svc.Id -Force }
foreach ($r in @($procs.Keys | Sort-Object)) {
    $log = Join-Path $Out "$r\wfc.log"
    $ev = Select-String -LiteralPath $log -Pattern '(UDPMATCH done|relay[^|]{0,80}|joining service code[^|]{0,40}|JoinFailed[^|]{0,30})' -ErrorAction SilentlyContinue | % { $_.Matches[0].Value } | Select-Object -Unique -First 4
    Write-Host "$r exit $($procs[$r].ExitCode): $($ev -join ' / ')"
}
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -like "$Bin*" }).Count)
