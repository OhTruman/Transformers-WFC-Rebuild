# POOLED P90 over capacity-stress reps (Integration 2026-10-11: the Gorge real play 4K re-verdict is 300 s timed matches, 3 reps,
# EACH p90 <= 3.33 ms, and the pooled p90 is reported too). Reads each rep's frametimes_m<Match>.txt (capacity-stress writes them;
# rows from before 2026-10-11 get them with a -ReportOnly rerun of capacity-stress on their OutDir) and applies the same percentile
# rule as capacity-stress (sorted, index floor(p * n)), so a single rep reproduces its capacity.csv p90.
#
#   .\tools\fidelity\pooled-p90.ps1 -Dirs <rep OutDir>,<rep OutDir>,... [-Match 2] [-Target 3.333]
param([Parameter(Mandatory)][string[]]$Dirs, [int]$Match = 2, [double]$Target = 3.333)
$ErrorActionPreference = "Stop"
$Dirs = @($Dirs | ForEach-Object { "$_" -split ',' } | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() })
function Pct($s, $p) { return [Math]::Round($s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))], 2) }
$inv = [Globalization.CultureInfo]::InvariantCulture
$all = New-Object System.Collections.Generic.List[double]; $out = @()
foreach ($d in $Dirs) {
    $files = @(Get-ChildItem -Path $d -Recurse -Filter "frametimes_m$Match.txt" -ErrorAction SilentlyContinue)
    if ($files.Count -ne 1) { throw "$d : expected one frametimes_m$Match.txt, found $($files.Count) (rows from before 2026-10-11: rerun capacity-stress -ReportOnly)" }
    $v = [double[]]@([IO.File]::ReadAllLines($files[0].FullName) | Where-Object { $_ } | ForEach-Object { [double]::Parse($_, $inv) })
    $s = [double[]]$v.Clone(); [Array]::Sort($s); $all.AddRange($v)
    $p90 = Pct $s 0.90; $under = [Math]::Round(100.0 * @($v | Where-Object { $_ -le $Target }).Count / [Math]::Max(1, $v.Count), 1)
    $out += "{0}: {1} frames, p90 {2} ms ({3}), under {4} ms {5} %" -f $d, $v.Count, $p90, $(if ($p90 -le $Target) { "MET" } else { "MISS" }), $Target, $under
}
$s = $all.ToArray(); [Array]::Sort($s)
$pp = Pct $s 0.90; $under = [Math]::Round(100.0 * @($s | Where-Object { $_ -le $Target }).Count / [Math]::Max(1, $s.Count), 1)
$out += "POOLED over {0} reps: {1} frames, p90 {2} ms ({3}), p50 {4}, p99 {5}, under {6} ms {7} %" -f $Dirs.Count, $s.Count, $pp, $(if ($pp -le $Target) { "MET" } else { "MISS" }), (Pct $s 0.50), (Pct $s 0.99), $Target, $under
$out
