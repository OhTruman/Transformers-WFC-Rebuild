# Per-map fix-loop summary (Integration 2026-10-09 "all maps 300+"): for work\permap\v2-<id>-* (base / head x overview / real play x
# 1080 / 2160) prints the second-match row per population with p50 / p90 / p99 / 1 % low / stall counts and the SLOWFRAME cause
# split (capacity-stress's slowframes line), plus a GPU-bound vs CPU verdict (Systems' criterion: GPU-bound share < 50 % = CPU side).
#   .\tools\fidelity\permap-v2-summary.ps1 -Dir work\permap -Map 502
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$Map)
$out = New-Object System.Collections.Generic.List[string]
$out.Add("| build | view | 3D | players | p50 | p90 | p99 | 1 % low | >16.7 / >33 | p90 <= 3.33 | slow frames: causes | avg slow frame | side |")
$out.Add("|---|---|---|---|---|---|---|---|---|---|---|---|---|")
foreach ($d in Get-ChildItem $Dir -Directory | Where-Object { $_.Name -match "^v2-$Map-(base|head)-(ov|rp)-(1080|2160)$" } | Sort-Object Name) {
    $null = $d.Name -match "^v2-$Map-(base|head)-(ov|rp)-(1080|2160)$"; $b = $Matches[1]; $v = @{ ov = "overview"; rp = "real play" }[$Matches[2]]; $s = $Matches[3]
    $csv = Join-Path $d.FullName "capacity.csv"; $rep = Join-Path $d.FullName "report.json"
    if (-not (Test-Path $csv)) { $out.Add("| $b | $v | $s | - | (no data) |||||||||"); continue }
    $res = if (Test-Path $rep) { (Get-Content -Raw $rep | ConvertFrom-Json).results } else { @() }
    foreach ($r in @(Import-Csv $csv | Where-Object { $_.match -eq '2' } | Sort-Object { [int]$_.participants })) {
        $sf = @($res | Where-Object { $_.id -like "*_t$($r.participants)_*m2.slowframes" })[0]
        $causes = "-"; $avg = "-"; $side = "-"
        if ($sf) {
            $m = [regex]::Match($sf.note, 'by cause: (.*?); avg slow frame (.*)$')
            if ($m.Success) { $causes = $m.Groups[1].Value; $avg = $m.Groups[2].Value }
            $g = [regex]::Match($sf.note, 'GPU-bound \d+ \((\d+)%\)')
            $side = if ($g.Success -and [int]$g.Groups[1].Value -ge 50) { "GPU ($($g.Groups[1].Value) %)" } elseif ($g.Success) { "CPU (GPU $($g.Groups[1].Value) %)" } else { "CPU (GPU 0 %)" }
        }
        $met = if ([double]$r.p90_ms -le 3.33) { "MET" } else { "**MISS**" }
        $out.Add(("| {0} | {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} / {9} | {10} | {11} | {12} | {13} |" -f $b, $v, $s, $r.participants, $r.p50_ms, $r.p90_ms, $r.p99_ms, $r.low1_fps, $r.over_16_7, $r.over_33, $met, $causes, $avg, $side))
    }
}
$out
