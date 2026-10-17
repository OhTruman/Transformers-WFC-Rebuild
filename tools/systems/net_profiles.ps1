# Netcode impairment rows (Systems; NETCODE_HARNESS.md sections 4 / 5): per netshim profile, two headless wfc_rebuild peers (null render,
# no card) through tools/fidelity/netshim.py, paced at 60 Hz (WFC_NET_PACE). Reports per peer: exit code, steps, total stall, resends,
# RTT and bytes/s, plus the shim's per-direction stats. -HashEvery 0 skips the desync detector (transport behaviour only, e.g. before
# the seat-order fix); 1 = every tick (the NET.identity row). One profile at a time; processes killed by PID on timeout.
param([Parameter(Mandatory)] [string]$Bin, [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\netprof',
      [string]$Profiles = 'lan,good,typical,bad,burst,outage', [int]$Seconds = 30, [int]$HashEvery = 0, [int]$Delay = 0,
      [string]$Map = 'MP_IAC_Streets', [string]$Shim = 'F:\Transformers Rebuild\Rebuild-Systems\work\netshim\netshim.py')
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
$hold = 'F:\Transformers Rebuild\Rebuild-Experimental\work\PERF_HOLD'
foreach ($prof in ($Profiles -split ',')) {
    while (Test-Path $hold) { Start-Sleep 2 }
    $root = Join-Path $Out $prof
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    $mapFile = Join-Path $root 'ports.json'
    $shimProc = Start-Process -FilePath $py -ArgumentList @('-I', "`"$Shim`"", '--peer', 'A=127.0.0.1:5201', '--peer', 'B=127.0.0.1:5202', '--profile', $prof,
                                                        '--seed', '1', '--base-port', '6200', '--map', "`"$mapFile`"", '--log', "`"$(Join-Path $root 'shim.jsonl')`"",
                                                        '--secs', "$($Seconds + 90)") -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'shim.out')
    for ($i = 0; $i -lt 100 -and -not (Test-Path $mapFile); ++$i) { Start-Sleep -Milliseconds 100 }
    if (-not (Test-Path $mapFile)) { Write-Host "${prof}: the shim did not start"; & taskkill.exe /T /F /PID $shimProc.Id | Out-Null; continue }
    $ports = (Get-Content -Raw $mapFile | ConvertFrom-Json).ports
    $procs = @()
    foreach ($slot in 0, 1) {
        $dir = Join-Path $root "s$slot"; New-Item -ItemType Directory -Force $dir | Out-Null
        Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
        $to = if ($slot -eq 0) { $ports.'A>B' } else { $ports.'B>A' }
        $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_NOMOUSE = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7';
                WFC_SKIPINTRO = '1'; WFC_PLAYERBOT = '1'; WFC_BOOT = 'match'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render';
                WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_SLOT = "$slot"; WFC_NET_SEATS = '2';
                WFC_NET_BIND = "127.0.0.1:$(5201 + $slot)"; WFC_NET_PEERS = "$(1 - $slot)=127.0.0.1:$to"; WFC_NET_HASH_EVERY = "$HashEvery";
                WFC_NET_DELAY = "$Delay"; WFC_NET_PACE = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv'); WFC_NET_TIMEOUT_S = '10' }
        foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
        $p = Start-Process -FilePath (Join-Path $Bin "wfc_rebuild.exe") -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle; $procs += $p   # null render + no console window: no hwnd at all
    }
    Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }
    foreach ($p in $procs) { if (-not $p.WaitForExit(($Seconds + 80) * 1000)) { & taskkill.exe /T /F /PID $p.Id | Out-Null; Write-Host "  TIMEOUT pid $($p.Id)" } }
    if (-not $shimProc.HasExited) { & taskkill.exe /T /F /PID $shimProc.Id | Out-Null }
    foreach ($slot in 0, 1) {
        $log = Join-Path $root "s$slot\wfc.log"
        $done = Select-String -LiteralPath $log -Pattern 'NET done: (\d+) steps in ([\d.]+) s wall, status (\d+)' | Select-Object -Last 1
        $bye = Select-String -LiteralPath $log -Pattern 'NET bye: sent (\d+) B .* received (\d+) B .* resends (\d+), rtt ([\d.]+) ms, stalled (\d+) ms' | Select-Object -Last 1
        $csv = @(Import-Csv (Join-Path $root "s$slot\net.csv"))
        $maxStall = ($csv | Measure-Object -Property stall_ms -Maximum).Maximum
        $line = "{0} seat {1}: exit {2}" -f $prof, $slot, $procs[$slot].ExitCode
        if ($done) { $line += (", {0} steps in {1} s" -f $done.Matches[0].Groups[1].Value, $done.Matches[0].Groups[2].Value) }
        if ($bye) {
            $g = $bye.Matches[0].Groups; $wall = if ($done) { [double]$done.Matches[0].Groups[2].Value } else { 1 }
            $line += (", out {0:N0} B/s, resends {1}, rtt {2} ms, stalled {3} ms total, worst tick stall {4:N0} ms" -f ([double]$g[1].Value / [Math]::Max(1, $wall)), $g[3].Value, $g[4].Value, $g[5].Value, $maxStall)
        }
        Write-Host $line
        Select-String -LiteralPath $log -Pattern 'NET .*(DESYNC|timeout|left|mismatch)' | Select-Object -First 2 | ForEach-Object { Write-Host ('    ' + $_.Line.Substring(0, [Math]::Min(170, $_.Line.Length))) }
    }
    $stats = Get-Content (Join-Path $root 'shim.out') -ErrorAction SilentlyContinue | Select-Object -Last 1
    if ($stats) { Write-Host "  shim: $stats" }
}
$own = @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '*Rebuild-Systems*' }); Write-Host "own exes left: $($own.Count)"
