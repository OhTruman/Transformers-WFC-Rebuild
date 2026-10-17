# NET.identity (NETCODE_HARNESS.md section 5; Experimental's independent row beside Systems' net_profiles.ps1): per netshim profile,
# two headless wfc_rebuild peers (null render, no card, no window) through tools/fidelity/netshim.py, paced at 60 Hz, SIMHASH every
# tick (WFC_NET_HASH_EVERY=1); then netlog_join.py on the two per-tick logs: first SIMHASH split vs first command-bundle split,
# stalls, bytes/s. Own port range (default peers 5301 / 5302, shim 6300+) so it can run beside Systems' rows. PIDs killed on timeout.
#   powershell -File tools/fidelity/net-identity.ps1 -Bin <dir with wfc_rebuild.exe> -Out <dir> [-Profiles lan,typical,bad] [-Seconds 30]
param([Parameter(Mandatory)][string]$Bin, [Parameter(Mandatory)][string]$Out, [string]$Profiles = 'lan,typical,bad', [int]$Seconds = 30,
      [int]$HashEvery = 1, [int]$PortBase = 5300, [string]$Map = 'MP_IAC_Streets', [string]$RenderData = 'F:\Transformers Rebuild\Rebuild\work\render', [switch]$Replay, [int]$StateDumpAt = 0)   # (-StateDumpAt <tick>: WFC_NET_STATEDUMP on both peers and the replay, Gameplay's per-pawn dump)
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'; $here = $PSScriptRoot
$hold = Join-Path (Split-Path (Split-Path $here -Parent) -Parent) 'work\PERF_HOLD'
$exe = Join-Path $Bin 'wfc_rebuild.exe'; if (-not (Test-Path $exe)) { "no exe $exe"; exit 1 }
foreach ($prof in ($Profiles -split ',')) {
    while (Test-Path $hold) { Start-Sleep 2 }
    $root = Join-Path $Out $prof; New-Item -ItemType Directory -Force $root | Out-Null
    $mapFile = Join-Path $root 'ports.json'; if (Test-Path $mapFile) { Remove-Item $mapFile }
    $pa = $PortBase + 1; $pb = $PortBase + 2
    $shim = Start-Process -FilePath $py -ArgumentList @('-I', "`"$(Join-Path $here 'netshim.py')`"", '--peer', "A=127.0.0.1:$pa", '--peer', "B=127.0.0.1:$pb",
              '--profile', $prof, '--seed', '1', '--base-port', "$($PortBase + 1000)", '--map', "`"$mapFile`"", '--log', "`"$(Join-Path $root 'shim.jsonl')`"",
              '--secs', "$($Seconds + 90)") -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'shim.out')
    for ($i = 0; $i -lt 100 -and -not (Test-Path $mapFile); ++$i) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $mapFile)) { "${prof}: the shim did not start"; if (-not $shim.HasExited) { Stop-Process -Id $shim.Id -Force }; continue }
    $ports = (Get-Content -Raw $mapFile | ConvertFrom-Json).ports
    $procs = @()
    foreach ($slot in 0, 1) {
        $dir = Join-Path $root "s$slot"; New-Item -ItemType Directory -Force $dir | Out-Null
        $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item "env:$($_.Name)" }
        $to = if ($slot -eq 0) { $ports.'A>B' } else { $ports.'B>A' }
        $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
                WFC_SKIPINTRO = '1'; WFC_PLAYERBOT = '1'; WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = $RenderData;
                WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_SLOT = "$slot"; WFC_NET_SEATS = '2';
                WFC_NET_BIND = "127.0.0.1:$($PortBase + 1 + $slot)"; WFC_NET_PEERS = "$(1 - $slot)=127.0.0.1:$to"; WFC_NET_HASH_EVERY = "$HashEvery";
                WFC_NET_PACE = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv'); WFC_NET_TIMEOUT_S = '10' }
        if ($slot -eq 0 -and $Replay) { $e.WFC_NET_RECORD = (Join-Path $root 'rec.bin') }
        if ($StateDumpAt) { $e.WFC_NET_STATEDUMP = "$StateDumpAt" }   # (c702c93b+: the host records every applied tick)
        foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
        $p = Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle; $procs += $p
        Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
    }
    foreach ($p in $procs) { if (-not $p.WaitForExit(($Seconds + 80) * 1000)) { Stop-Process -Id $p.Id -Force; "  TIMEOUT pid $($p.Id)" } }
    if (-not $shim.HasExited) { Stop-Process -Id $shim.Id -Force }
    $codes = ($procs | % { $_.ExitCode }) -join ' / '
    $done = foreach ($slot in 0, 1) { (Select-String -LiteralPath (Join-Path $root "s$slot\wfc.log") -Pattern 'NET done: .*' -ErrorAction SilentlyContinue | Select-Object -Last 1).Matches.Value }
    "${prof}: exit codes $codes; $($done -join ' | ')"
    & $py -I (Join-Path $here 'netlog_join.py') (Join-Path $root 's0\net.csv') (Join-Path $root 's1\net.csv') --json (Join-Path $root 'join.json') | Select-Object -First 1 | % { "  live peers: $_" }
    # replay equivalence (WFC_NET_REPLAY, c702c93b+): one process, no network, fed the host's recording - its simhash stream must equal
    # the live peers' (exit 8 = the end of the recording)
    if ($Replay -and (Test-Path (Join-Path $root 'rec.bin'))) {
        $dir = Join-Path $root 'replay'; New-Item -ItemType Directory -Force $dir | Out-Null
        $saved = @{}; Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { $saved[$_.Name] = $_.Value; Remove-Item "env:$($_.Name)" }
        $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
                WFC_SKIPINTRO = '1'; WFC_PLAYERBOT = '1'; WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = $RenderData;
                WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_SLOT = '0'; WFC_NET_SEATS = '2';
                WFC_NET_HASH_EVERY = "$HashEvery"; WFC_NET_REPLAY = (Join-Path $root 'rec.bin'); WFC_NET_LOG = (Join-Path $dir 'net.csv') }
        if ($StateDumpAt) { $e.WFC_NET_STATEDUMP = "$StateDumpAt" }
        foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
        $p = Start-Process -FilePath $exe -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle
        Get-ChildItem env: | ? { $_.Name -like 'WFC_*' } | % { Remove-Item "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
        if (-not $p.WaitForExit(($Seconds + 80) * 1000)) { Stop-Process -Id $p.Id -Force; "  replay TIMEOUT pid $($p.Id)" }
        & $py -I (Join-Path $here 'netlog_join.py') (Join-Path $root 's0\net.csv') (Join-Path $dir 'net.csv') --json (Join-Path $root 'join_replay.json') | Select-Object -First 1 | % { "  host vs replay (exit $($p.ExitCode)): $_" }
    }
}
