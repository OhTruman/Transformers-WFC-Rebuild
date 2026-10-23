# Member-leaves arm (Experimental 2026-10-23): host (WFC_UDPHOST, 3 seats) + a member that stays + a member that leaves at -LeaveAt s of
# match: -Mode end = the leaver's WFC_MATCH_SECONDS ends early (its own "match ended, leave"); -Mode kill = its PID is killed at
# -LeaveAt s after AllLoaded (a hard drop); -Mode switch = WFC_UDPLEAVE_AT (if the build has it). Direct loopback, hash every tick.
param([Parameter(Mandatory)][string]$Bin, [Parameter(Mandatory)][string]$Out, [ValidateSet('end', 'kill', 'switch')][string]$Mode = 'end',
      [int]$Seconds = 120, [int]$LeaveAt = 60, [string]$Map = 'MP_IAC_Streets', [int]$Port = 14400, [int]$Seats = 3, [switch]$Empty, [int]$Bots = 2)   # -Bots N = BotsFriendly / BotsEnemy (0 = humans only: a Forfeit needs a team with nobody, bots count)   # -Empty: WFC_UDPLEAVE_EMPTY=1 on every peer (empty seat + Forfeit; default since ae9bbb6e = a bot takes the slot)   # -Seats 2 = a 1v1 (host + leaver, [string[]]$PkgEnv = @())
if (Test-Path $Out) { Remove-Item -Recurse -Force $Out }; New-Item -ItemType Directory -Force $Out | Out-Null
$procs = @{}
function Start-Peer($role, $extra) {
    $dir = Join-Path $Out $role; New-Item -ItemType Directory -Force $dir | Out-Null
    $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
    $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7'; WFC_SKIPINTRO = '1';
            WFC_PLAYERBOT = '1'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render'; WFC_UDPSEATS = "$Seats";
            WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=$Bots?BotsEnemy=$Bots?PointsToWin=1000"; WFC_NET_HASH_EVERY = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv') }
    if ($Empty) { $e['WFC_UDPLEAVE_EMPTY'] = '1' }
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    foreach ($kv in $PkgEnv) { $kk, $vv = $kv -split '=', 2; $e[$kk] = $vv }   # -PkgEnv: a package's data roots etc., applied last
    foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
    $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle
    Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    $script:procs[$role] = $p
}
$leaver = @{ WFC_UDPJOIN = "127.0.0.1:$Port"; WFC_UDPBIND = "$($Port + 2)" }
if ($Mode -eq 'end') { $leaver['WFC_MATCH_SECONDS'] = "$LeaveAt" }
if ($Mode -eq 'switch') { $leaver['WFC_UDPLEAVE_AT'] = "$LeaveAt" }
Start-Peer 'host' @{ WFC_UDPHOST = "$Port" }
Start-Sleep -Milliseconds 500
if ($Seats -ge 3) { Start-Peer 'stay' @{ WFC_UDPJOIN = "127.0.0.1:$Port"; WFC_UDPBIND = "$($Port + 1)" } }
Start-Peer 'leaver' $leaver
if ($Mode -eq 'kill') {
    $log = Join-Path $Out 'leaver\wfc.log'; $t0 = Get-Date
    while (-not (Select-String -LiteralPath $log -Pattern 'AllLoaded' -Quiet -ErrorAction SilentlyContinue)) { if (((Get-Date) - $t0).TotalSeconds -gt 120) { break }; Start-Sleep -Milliseconds 200 }
    Start-Sleep -Seconds $LeaveAt; Stop-Process -Id $procs['leaver'].Id -Force; Write-Host "  killed the leaver PID $($procs['leaver'].Id) at +$LeaveAt s"
}
$roles = @($procs.Keys | Sort-Object)
foreach ($r in $roles) { if (-not $procs[$r].WaitForExit(($Seconds + 240) * 1000)) { Stop-Process -Id $procs[$r].Id -Force; Write-Host "  $r TIMEOUT (killed PID $($procs[$r].Id))" } }
foreach ($r in $roles) {
    $log = Join-Path $Out "$r\wfc.log"
    $ev = Select-String -LiteralPath $log -Pattern '(UDPMATCH (done|<-)|NET (driver: )?seat \d+ (left|leaves|dropped|timed out)|[Ff]orfeit|dormant|MATCH (leave|end))[^|]{0,110}' -ErrorAction SilentlyContinue | % { $_.Matches[0].Value } | Select-Object -Unique -Last 5
    Write-Host "$r exit $($procs[$r].ExitCode): $($ev -join ' / ')"
}
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | ? { $_.Path -like "$Bin*" }).Count)
