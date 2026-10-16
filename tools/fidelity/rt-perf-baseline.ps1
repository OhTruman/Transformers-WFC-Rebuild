# RT PERF BASELINE (tier TARGETED, timed - set the lane's PERF_HOLD; Integration 2026-10-14, plan v2's per-item budget reports):
# one build, Ray Tracing Off / Faithful / Realistic as three lockstep arms of the SAME match (lockstep-ab.ps1: direct boot,
# 31 v 32 bots + the bot-driven player, one sim step per frame) per map, at the display's native size in a window (no mode
# change). Per arm: frame p50 / p90 over the measured step range, and the GPU ms of every RT pass from the product's RT summary
# lines ("RT <pass> ...: GPU <avg> ms avg", every 300 frames; averaged over the run), plus the RT device VRAM. Output:
# <OutDir>\RT-PERF-BASELINE.md (+ .csv) - Rendering diffs a later build's table against it after each plan item.
# The arms differ in what they render, so lockstep-ab's workload-parity / same-match validity is not a gate here (reported only).
#
#   .\tools\fidelity\rt-perf-baseline.ps1 -Root work\ab\m9c_30b2fa7 -OutDir <dir> [-Maps 504,507,508,509,510] [-Steps 6000 (the last step)] [-From 2400]
#       [-Width 2560] [-Height 1440] [-ReportOnly]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @("504", "507", "508", "509", "510"),
      [int]$Steps = 6000, [int]$From = 2400, [int]$Width = 2560, [int]$Height = 1440, [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$Maps = @($Maps | ForEach-Object { "$_" -split '[,;\s]+' } | Where-Object { $_ })
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$arms = @("off|$Root|WFC_RT_MODE=0", "faithful|$Root|WFC_RT_MODE=1", "realistic|$Root|WFC_RT_MODE=2")
$rows = New-Object System.Collections.Generic.List[object]
foreach ($m in $Maps) {
    $d = Join-Path $OutDir "map-$m"
    if (-not $ReportOnly) {
        & (Join-Path $PSScriptRoot "lockstep-ab.ps1") -OutDir $d -Map $m -Arms $arms -Steps $Steps -From $From -Width $Width -Height $Height `
            -RenderSize "${Width}x${Height}" *> (Join-Path $OutDir "map-$m.out")
    }
    foreach ($a in "off", "faithful", "realistic") {
        $ad = Join-Path $d "$a-r1"; $lg = Join-Path $ad "wfc.log"
        $row = [ordered]@{ map = $m; mode = $a; p50 = $null; p90 = $null; rt_mode = $null; shadow_ao = $null; denoise = $null; refl = $null; gi = $null; rt_vram_mb = $null; frames = $null }
        if (Test-Path $lg) {
            $rs = Get-WfcRtState $lg; $row.rt_mode = $rs.mode
            function AvgOf([string]$pat) { $v = @(Select-String -Path $lg -Pattern $pat | ForEach-Object { [double]$_.Matches[0].Groups[1].Value }); if ($v.Count) { [Math]::Round(($v | Measure-Object -Average).Average, 3) } else { $null } }
            $row.shadow_ao = AvgOf 'RT shadow \+ AO .*?: GPU ([\d.]+) ms avg'
            $row.denoise = AvgOf 'RT shadow denoiser .*?: GPU ([\d.]+) ms avg'
            $row.refl = AvgOf 'RT reflections .*?: GPU ([\d.]+) ms avg'
            $row.gi = AvgOf 'RT GI texel tracer .*?: GPU ([\d.]+) ms avg'
            $row.rt_vram_mb = AvgOf 'RT device VRAM ([\d.]+) MB'
        }
        # frame percentiles from lockstep-ab's per-arm note ("frame p50 / p90 / p99 a / b / c ms over N frames")
        $rep = Join-Path $d "report.json"
        if (Test-Path $rep) {
            $j = Get-Content $rep -Raw | ConvertFrom-Json; $list = if ($j.results) { $j.results } else { $j }
            $n = @($list | Where-Object { $_.id -match "\.$a\.r1$" } | Select-Object -First 1)
            if ($n.Count -and $n[0].note -match 'frame p50 / p90 / p99 ([\d.]+) / ([\d.]+) / ([\d.]+) ms over (\d+) frames') {
                $row.p50 = [double]$Matches[1]; $row.p90 = [double]$Matches[2]; $row.frames = [int]$Matches[4]
            }
        }
        $rows.Add([pscustomobject]$row)
    }
}
$rows | Export-Csv -NoTypeInformation -Encoding ASCII (Join-Path $OutDir "rt-perf-baseline.csv")
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# RT perf baseline - $(Split-Path $Root -Leaf), ${Width}x${Height} windowed, 31 v 32 bots + player bot, lockstep steps $From-$Steps")
$flags = "WFC_NOINTERACT={0}, WFC_MUTE={1}, WFC_NOMOUSE={2}" -f $(if ($env:WFC_NOINTERACT) { $env:WFC_NOINTERACT } else { "off" }), $(if ($env:WFC_MUTE) { $env:WFC_MUTE } else { "off" }), $(if ($env:WFC_NOMOUSE) { $env:WFC_NOMOUSE } else { "off" })
$md.Add(""); $md.Add("Run conditions: $flags. (2026-10-16: rows under WFC_NOINTERACT are their own reference series - window pacing differs - never diff them against pre-NOINTERACT rows.)")
$md.Add(""); $md.Add("GPU ms per RT pass = the product's RT summary averages over the run; frame p50 / p90 over the measured steps (ms).")
$md.Add(""); $md.Add("| map | mode | frame p50 | frame p90 | shadow+AO | denoiser | reflections | GI tracer (per traced frame) | RT VRAM MB | frames |")
$md.Add("|---|---|---|---|---|---|---|---|---|---|")
foreach ($r in $rows) {
    $want = @{ off = 0; faithful = 1; realistic = 2 }[[string]$r.mode]
    $flag = if ($r.mode -ne "off" -and $null -ne $r.rt_mode -and [int]$r.rt_mode -ne $want) { " (logged mode $($r.rt_mode)!)" } else { "" }
    $cells = @([string]$r.map, ([string]$r.mode + $flag), [string]$r.p50, [string]$r.p90, [string]$r.shadow_ao, [string]$r.denoise, [string]$r.refl, [string]$r.gi, [string]$r.rt_vram_mb, [string]$r.frames)
    $md.Add("| " + ($cells -join " | ") + " |")
}
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "RT-PERF-BASELINE.md")
$md
