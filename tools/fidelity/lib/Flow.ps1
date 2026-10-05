# Helpers for the frontend flow trace (agents/frontend FlowTrace: WFC_FLOWLOG JSON lines, mirrored into wfc.log as
# "FLOW <ev> k=v ..."). Dot-source after lib\Run.ps1.

# JSON-lines trace -> array of objects (ev, t, seq, fields...). Missing / empty file -> empty array.
function Read-FlowLog([string]$Path) {
    if (-not (Test-Path $Path)) { return , @() }; $Path = (Resolve-Path $Path).Path
    $out = New-Object System.Collections.Generic.List[object]
    foreach ($ln in [IO.File]::ReadLines($Path)) { if ($ln.Trim()) { try { $out.Add(($ln | ConvertFrom-Json)) } catch { } } }
    return , $out.ToArray()
}
function Flow-Ev($flow, [string]$ev) { $flow | Where-Object { $_.ev -eq $ev } }   # unrolled: wrap callers in @()
# Parse a travel URL "Map?k=v?flag" -> @{ map; keys = @{}; flags = @() }
function Parse-Url([string]$url) {
    $p = $url -split '\?'; $o = @{ map = $p[0]; keys = @{}; flags = @() }
    foreach ($x in $p[1..($p.Count - 1)]) { if (-not $x) { continue }; $kv = $x -split '=', 2; if ($kv.Count -eq 2) { $o.keys[$kv[0]] = $kv[1] } else { $o.flags += $x } }
    return $o
}
# wfc.log lines in order, tagged: @{ i; kind = flow|frame|amb|music|other; text; ev; f (frame fields) }
function Read-RunLog([string]$WfcLog) {
    $out = New-Object System.Collections.Generic.List[object]; $i = 0
    if (-not (Test-Path $WfcLog)) { return , @() }; $WfcLog = (Resolve-Path $WfcLog).Path
    foreach ($ln in [IO.File]::ReadLines($WfcLog)) {
        $i++
        if ($ln -match '\] FLOW (\S+)') { $out.Add([pscustomobject]@{ i = $i; kind = "flow"; ev = $Matches[1]; text = $ln }) }
        elseif ($ln -match '\] frame (\d+) pos (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) .*hspeed=([\d.]+)') { $out.Add([pscustomobject]@{ i = $i; kind = "frame"; frame = [int]$Matches[1]; x = [double]$Matches[2]; y = [double]$Matches[3]; z = [double]$Matches[4]; hspeed = [double]$Matches[5]; text = $ln }) }
        elseif ($ln -match '\] AMB ') { $out.Add([pscustomobject]@{ i = $i; kind = "amb"; text = $ln }) }
        elseif ($ln -match '\] MUSIC ') { $out.Add([pscustomobject]@{ i = $i; kind = "music"; text = $ln }) }
    }
    return , $out.ToArray()
}
# Start a product process with extra environment, cwd = $Dir (stdout/stderr to run.log); returns the Process.
function Start-WfcProcess([string]$Exe, [string]$Dir, [hashtable]$Env) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $saved = @{}; foreach ($k in $Env.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k); [Environment]::SetEnvironmentVariable($k, [string]$Env[$k]) }
    try { $p = Start-Process -FilePath $Exe -WorkingDirectory $Dir -PassThru -RedirectStandardOutput (Join-Path $Dir "run.log") -RedirectStandardError (Join-Path $Dir "run.log.err"); $null = $p.Handle; return $p }   # touching Handle keeps ExitCode readable
    finally { foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k]) } }
}
# Run to exit (or timeout) while sampling memory/handles every $Every s and the number of flow-log lines at each
# sample (so samples can be tied to flow events). Returns @{ rc; timedOut; samples = [ {t, private_mb, ws_mb, handles, threads, flow_lines} ] }
function Invoke-WfcSampled([string]$Exe, [string]$Dir, [hashtable]$Env, [int]$TimeoutSec = 900, [double]$Every = 1.0) {
    $p = Start-WfcProcess $Exe $Dir $Env
    $flowPath = $Env.WFC_FLOWLOG
    $t0 = Get-Date; $samples = New-Object System.Collections.Generic.List[object]; $timedOut = $false
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds ([int]($Every * 1000))
        $el = ((Get-Date) - $t0).TotalSeconds
        if ($el -gt $TimeoutSec) { $timedOut = $true; try { Stop-Process -Id $p.Id -Force } catch { }; break }
        try {
            $p.Refresh()
            $fl = 0; if ($flowPath -and (Test-Path $flowPath)) { try { $fs = [IO.File]::Open($flowPath, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); while ($null -ne $sr.ReadLine()) { $fl++ }; $sr.Close() } catch { } }
            $samples.Add([pscustomobject]@{ t = [Math]::Round($el, 1); private_mb = [Math]::Round($p.PrivateMemorySize64 / 1MB, 1); ws_mb = [Math]::Round($p.WorkingSet64 / 1MB, 1); handles = $p.HandleCount; threads = $p.Threads.Count; flow_lines = $fl })
        } catch { }
    }
    $p.WaitForExit(5000) | Out-Null
    try { $samples.ToArray() | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $Dir "samples.csv") } catch {}   # process samples kept with the run (resource checks reuse them)
    return @{ rc = $(if ($p.HasExited) { $p.ExitCode } else { -1 }); timedOut = $timedOut; samples = $samples.ToArray() }
}

# ---- capture of the product window from outside the process (user32 PrintWindow, PW_RENDERFULLCONTENT) ----
# The "user actually sees it" layer: what the window shows, independent of what the product logs.
if (-not ("WfcWin" -as [type])) {
Add-Type -ReferencedAssemblies System.Drawing @"
using System; using System.Drawing; using System.Runtime.InteropServices;
public static class WfcWin {
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h, out RECT r);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    public static bool Capture(IntPtr h, string path) {
        RECT r; if (h == IntPtr.Zero || !GetClientRect(h, out r)) return false;
        int w = r.R - r.L, hh = r.B - r.T; if (w < 16 || hh < 16) return false;
        using (var bmp = new Bitmap(w, hh)) {
            using (var g = Graphics.FromImage(bmp)) { IntPtr dc = g.GetHdc(); bool ok = PrintWindow(h, dc, 3); g.ReleaseHdc(dc); if (!ok) return false; }
            bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png); return true;
        }
    }
}
"@
}
# Like Invoke-WfcSampled, plus a window capture whenever the flow trace gains an event whose name matches
# $CaptureOn (regex over "ev|key=value ..."), at most one per $MinGap seconds. Captures: <Dir>\cap_<seq>_<ev>.png
function Invoke-WfcObserved([string]$Exe, [string]$Dir, [hashtable]$Env, [int]$TimeoutSec = 900, [double]$Every = 0.5, [string]$CaptureOn = '^(ui\.state|level\.begin|loading\.start|movie\.play|ui\.hud)', [double]$MinGap = 0.4) {
    $p = Start-WfcProcess $Exe $Dir $Env
    $flowPath = $Env.WFC_FLOWLOG
    $t0 = Get-Date; $samples = New-Object System.Collections.Generic.List[object]; $caps = New-Object System.Collections.Generic.List[object]
    $seen = 0; $lastCap = -10.0; $timedOut = $false; $pending = $null
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds ([int]($Every * 1000))
        $el = ((Get-Date) - $t0).TotalSeconds
        if ($el -gt $TimeoutSec) { $timedOut = $true; try { Stop-Process -Id $p.Id -Force } catch { }; break }
        try { $p.Refresh() } catch { }
        $lines = @()
        if ($flowPath -and (Test-Path $flowPath)) { try { $fs = [IO.File]::Open($flowPath, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); while ($null -ne ($l = $sr.ReadLine())) { $lines += $l }; $sr.Close() } catch { } }
        for ($k = $seen; $k -lt $lines.Count; $k++) { try { $e = $lines[$k] | ConvertFrom-Json; if ($e.ev -match $CaptureOn) { $pending = $e } } catch { } }
        $seen = $lines.Count
        if ($pending -and ($el - $lastCap) -ge $MinGap) {
            $h = $p.MainWindowHandle; $name = "cap_{0:D4}_{1}.png" -f [int]$pending.seq, ($pending.ev -replace '[^\w]', '_')
            $ok = $false; try { $ok = [WfcWin]::Capture($h, (Join-Path $Dir $name)) } catch { }
            $caps.Add([pscustomobject]@{ t = [Math]::Round($el, 2); seq = [int]$pending.seq; ev = $pending.ev; detail = (($pending.PSObject.Properties | Where-Object { $_.Name -in 'to', 'level', 'movie', 'kind', 'visible' } | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " "); file = $(if ($ok) { $name } else { "" }) })
            $lastCap = $el; $pending = $null
        }
        try { $samples.Add([pscustomobject]@{ t = [Math]::Round($el, 1); private_mb = [Math]::Round($p.PrivateMemorySize64 / 1MB, 1); ws_mb = [Math]::Round($p.WorkingSet64 / 1MB, 1); handles = $p.HandleCount; threads = $p.Threads.Count; flow_lines = $seen }) } catch { }
    }
    $p.WaitForExit(5000) | Out-Null
    return @{ rc = $(if ($p.HasExited) { $p.ExitCode } else { -1 }); timedOut = $timedOut; samples = $samples.ToArray(); captures = $caps.ToArray() }
}

# Periodic window capture from outside the process (works while the product's main loop is blocked, e.g. during a
# blocking map load, when no product-side shot can run). Every $Every s from $FromSec: cap_<n>.png + a row with the
# elapsed time and the flow-trace line count at that moment (to place each capture between flow events).
function Invoke-WfcPeriodic([string]$Exe, [string]$Dir, [hashtable]$Env, [int]$TimeoutSec = 600, [double]$Every = 0.25, [double]$FromSec = 0, [double]$ToSec = 1e9) {
    $p = Start-WfcProcess $Exe $Dir $Env
    $flowPath = $Env.WFC_FLOWLOG
    $t0 = Get-Date; $caps = New-Object System.Collections.Generic.List[object]; $n = 0; $timedOut = $false
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds ([int]($Every * 1000))
        $el = ((Get-Date) - $t0).TotalSeconds
        if ($el -gt $TimeoutSec) { $timedOut = $true; try { Stop-Process -Id $p.Id -Force } catch { }; break }
        if ($el -lt $FromSec -or $el -gt $ToSec) { continue }
        try { $p.Refresh() } catch { }
        $fl = 0; if ($flowPath -and (Test-Path $flowPath)) { try { $fs = [IO.File]::Open($flowPath, 'Open', 'Read', 'ReadWrite'); $sr = New-Object IO.StreamReader($fs); while ($null -ne $sr.ReadLine()) { $fl++ }; $sr.Close() } catch { } }
        $n++; $name = "pcap_{0:D4}.png" -f $n; $ok = $false
        try { $ok = [WfcWin]::Capture($p.MainWindowHandle, (Join-Path $Dir $name)) } catch { }
        $caps.Add([pscustomobject]@{ n = $n; t = [Math]::Round($el, 2); flow_lines = $fl; file = $(if ($ok) { $name } else { "" }) })
    }
    $p.WaitForExit(5000) | Out-Null
    return @{ rc = $(if ($p.HasExited) { $p.ExitCode } else { -1 }); timedOut = $timedOut; captures = $caps.ToArray() }
}
