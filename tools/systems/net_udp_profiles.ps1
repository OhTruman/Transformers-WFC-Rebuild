# The UdpSession identity rows (Systems, online phase 2): per netshim profile, a host and a member headless wfc_rebuild (null render, no
# card) running a real UdpSession + NetMatchDriver match (WFC_UDPHOST / WFC_UDPJOIN) through tools/net/netshim.py, hash every tick.
# The host binds 127.0.0.1:14200 (shim peer A), the member 127.0.0.1:14201 (peer B) and joins the shim's B>A port. Reports per peer:
# exit code, steps, D / RTT, and the first tick where the two net.csv simhash columns differ. Kills only its own PIDs.
#   powershell -File net_udp_profiles.ps1 -Bin <build\bin> [-Profiles lan,typical,bad,burst,rttjump] [-Seconds 60] [-Out DIR]
param([Parameter(Mandatory)] [string]$Bin, [string]$Profiles = 'lan,typical,bad,burst,rttjump', [int]$Seconds = 60,
      [string]$Out = 'F:\Transformers Rebuild\Rebuild-Systems\work\net_udp', [string]$Map = 'MP_IAC_Streets',
      [string]$Shim = 'F:\Transformers Rebuild\Rebuild-Systems\tools\net\netshim.py')
$py = 'F:\Transformers Rebuild\AssetTools\bin\py\python.exe'
foreach ($prof in ($Profiles -split ',')) {
    $root = Join-Path $Out $prof
    if (Test-Path $root) { Remove-Item -Recurse -Force $root }
    New-Item -ItemType Directory -Force $root | Out-Null
    $shimProc = Start-Process -FilePath $py -ArgumentList @('-I', "`"$Shim`"", '--peer', 'A=127.0.0.1:14200', '--peer', 'B=127.0.0.1:14201', '--profile', $prof,
                                                        '--seed', '1', '--base-port', '6300', '--secs', "$($Seconds + 120)") -PassThru -WindowStyle Hidden -RedirectStandardOutput (Join-Path $root 'shim.out')
    Start-Sleep -Milliseconds 800
    $procs = @()
    foreach ($role in 'host', 'member') {
        $dir = Join-Path $root $role
        New-Item -ItemType Directory -Force $dir | Out-Null
        $saved = @{}; Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { $saved[$_.Name] = $_.Value; Remove-Item -LiteralPath "env:$($_.Name)" }
        $e = @{ WFC_NULLRENDER = '1'; WFC_MUTE = '1'; WFC_NOINTERACT = '1'; WFC_AUTODETECT = '0'; WFC_SEED = '7'; WFC_FLOWSEED = '7'; WFC_SKIPINTRO = '1';
                WFC_PLAYERBOT = '1'; WFC_MATCH_SECONDS = "$Seconds"; WFC_RENDER_DATA = 'F:/Transformers Rebuild/Rebuild/work/render';
                WFC_MATCH_URL = "$($Map)?GameModeTag=TDM?BotsFriendly=2?BotsEnemy=2?PointsToWin=1000"; WFC_NET_HASH_EVERY = '1'; WFC_NET_LOG = (Join-Path $dir 'net.csv') }
        if ($role -eq 'host') { $e['WFC_UDPHOST'] = '14200' } else { $e['WFC_UDPJOIN'] = '127.0.0.1:6301'; $e['WFC_UDPBIND'] = '14201' }
        foreach ($k in $e.Keys) { Set-Item "env:$k" $e[$k] }
        $p = Start-Process -FilePath (Join-Path $Bin 'wfc_rebuild.exe') -WorkingDirectory $dir -WindowStyle Hidden -PassThru; $null = $p.Handle; $procs += $p
        Get-ChildItem env: | Where-Object { $_.Name -like 'WFC_*' } | ForEach-Object { Remove-Item -LiteralPath "env:$($_.Name)" }; foreach ($k in $saved.Keys) { Set-Item "env:$k" $saved[$k] }
        Start-Sleep -Milliseconds 300
    }
    foreach ($p in $procs) { if (-not $p.WaitForExit(($Seconds + 180) * 1000)) { Stop-Process -Id $p.Id -Force; Write-Host "  TIMEOUT (killed PID $($p.Id))" } }
    if (-not $shimProc.HasExited) { Stop-Process -Id $shimProc.Id -Force }
    $line = @()
    foreach ($i in 0, 1) {
        $role = @('host', 'member')[$i]
        $done = Select-String -LiteralPath (Join-Path $root "$role\wfc.log") -Pattern 'UDPMATCH done: (\d+) steps.*D (\d+), rtt ([\d.]+)' | Select-Object -Last 1
        $line += "$role exit $($procs[$i].ExitCode)" + $(if ($done) { ", $($done.Matches[0].Groups[1].Value) steps, D $($done.Matches[0].Groups[2].Value), rtt $($done.Matches[0].Groups[3].Value) ms" } else { ', no match' })
    }
    $cmp = 'no logs'
    $a = Join-Path $root 'host\net.csv'; $b = Join-Path $root 'member\net.csv'
    if ((Test-Path $a) -and (Test-Path $b)) {
        $x = Import-Csv $a; $y = Import-Csv $b; $n = [Math]::Min($x.Count, $y.Count); $d = -1
        for ($j = 0; $j -lt $n; ++$j) { if ($x[$j].simhash -ne $y[$j].simhash) { $d = $x[$j].tick; break } }
        $stall = ($y | Measure-Object -Property stall_ms -Sum).Sum
        $cmp = "$n ticks compared, first simhash diff $(if ($d -lt 0) { 'none' } else { $d }), member stalled $([int]$stall) ms"
    }
    Write-Host "$prof : $($line -join ' | ') | $cmp"
}
Write-Host ("own exes left: " + @(Get-Process wfc_rebuild -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Bin*" }).Count)
