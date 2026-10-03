# Release-build frame cost per scenario (real wall clock, no per-frame logging), from the renderer's own
# 120-frame averages (WFC_RENDERSTATS: "wfc: avg frame N ms") and spike lines ("wfc spike: ...").
#
#   .\tools\fidelity\perf-release.ps1 -Exe <Release wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir>
#
# The first 120-frame window (startup, first-use builds) is excluded. Results are regression baselines,
# not fidelity targets: INFO, with a "possible regression" note when the median exceeds the integration
# baseline's upper bound by > 25% + 0.5 ms (M03 integration record, RX 7900 XTX).
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [string]$OutDir = "", [string[]]$Scenarios = @())
$ErrorActionPreference = "Stop"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes "a,b" as one string
. (Join-Path $PSScriptRoot "lib\Run.ps1")
if (-not $OutDir) { $OutDir = Join-Path (Get-WfcRoot) "work\fidelity\perf_release" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path   # the exe runs with cwd = OutDir: every path passed to it must be absolute
$defs = [ordered]@{
    idle          = @{ f = 840;  env = @{}; base = @(3.8, 4.1) }
    movement      = @{ f = 840;  env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5" }; base = @(1.0, 1.5) }
    firing        = @{ f = 600;  env = @{ WFC_AUTOFIRE = "1" }; base = @(4.0, 4.1) }
    sustained     = @{ f = 6000; env = @{ WFC_AUTOFIRE = "1" }; base = @(3.75, 4.6) }
    fine_aim      = @{ f = 840;  env = @{ WFC_FINEAIM_ON = "30"; WFC_AUTOFIRE = "1" }; base = @(3.1, 4.2) }
    transform_r2v = @{ f = 840;  env = @{ WFC_AUTOWALK = "1"; WFC_PRESSTRANSFORM = "60" }; base = @(0.9, 1.7) }
    vehicle_idle  = @{ f = 840;  env = @{ WFC_STARTVEHICLE = "1" }; base = @(4.3, 4.5) }
    hover_move    = @{ f = 840;  env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.5" }; base = @(1.2, 1.8) }
    boost         = @{ f = 1200; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1" }; base = @(1.2, 1.7) }
    nitro         = @{ f = 1200; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "240" }; base = @(1.2, 1.8) }
    transform_v2r = @{ f = 840;  env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_PRESSTRANSFORM = "60" }; base = @(1.1, 2.1) }
    # Frame cost is VIEW-dependent (the spawn view costs ~4 ms, turning views ~1-2 ms): the same scenarios
    # with the camera turning, as the movement scenario does, separate view cost from behaviour cost.
    transform_r2v_turning = @{ f = 840;  env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5"; WFC_PRESSTRANSFORM = "60" }; base = @(0.9, 1.7) }
    boost_turning         = @{ f = 1200; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5"; WFC_AUTOBOOST = "1" }; base = @(1.2, 1.7) }
    nitro_turning         = @{ f = 1200; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "240" }; base = @(1.2, 1.8) }
    # Milestone 04 locations: FFA 0 has 3 level steam emitters within 25 m (camera sweeps across them);
    # FFA 18's vehicle route had the most audible ambient emitters (m04-ambient.ps1). Baseline = M03 numbers.
    heavy_fx       = @{ f = 840;  env = @{ WFC_SPAWN_INDEX = "0"; WFC_AUTOTURN = "0.5" }; base = @(1.0, 4.1) }
    ambient_active = @{ f = 1200; env = @{ WFC_SPAWN_INDEX = "18"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.25" }; base = @(1.2, 1.8) }
    transform_v2r_turning = @{ f = 840;  env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5"; WFC_PRESSTRANSFORM = "60" }; base = @(1.1, 2.1) }
}
if (-not $Scenarios.Count) { $Scenarios = @($defs.Keys) }
$res = New-WfcResults
$rows = @()
foreach ($n in $Scenarios) {
    $d = $defs[$n]; $dir = Join-Path $OutDir $n
    $envs = @{ WFC_SMOKE_FRAMES = "$($d.f)"; WFC_RENDERSTATS = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0" } + $d.env
    if ($RenderData) { $envs.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $envs
    $lines = Get-Content (Join-Path $dir "wfc.log")
    $avg = @($lines | Select-String "wfc: avg frame ([\d.]+) ms" | ForEach-Object { [double]$_.Matches[0].Groups[1].Value })
    $win = if ($avg.Count -gt 1) { $avg[1..($avg.Count - 1)] } else { $avg }
    $sorted = @($win | Sort-Object)
    $med = if ($sorted.Count) { $sorted[[int](($sorted.Count - 1) / 2)] } else { -1 }
    $spikes = @($lines | Select-String "wfc spike: frame (\d+)" | Where-Object { [int]$_.Matches[0].Groups[1].Value -gt 130 }).Count
    $hi = $d.base[1]
    $reg = $med -gt ($hi * 1.25 + 0.5)
    $rows += [pscustomobject][ordered]@{ scenario = $n; windows = $sorted.Count; median_ms = [Math]::Round($med, 2); min_ms = $(if ($sorted.Count) { $sorted[0] } else { -1 }); max_ms = $(if ($sorted.Count) { $sorted[-1] } else { -1 })
                                         baseline = ("{0}-{1}" -f $d.base[0], $d.base[1]); spikes_after_startup = $spikes; exit = $rc; possible_regression = $reg }
    Add-WfcResult $res "perf_release.$n" "INFO" $med ("median of {0} 120-frame windows (range {1:F2}-{2:F2} ms); integration baseline {3}-{4} ms; {5} spikes after startup{6}" -f $sorted.Count, $(if ($sorted.Count) { $sorted[0] } else { -1 }), $(if ($sorted.Count) { $sorted[-1] } else { -1 }), $d.base[0], $d.base[1], $spikes, $(if ($reg) { "; POSSIBLE REGRESSION (> baseline max x1.25 + 0.5 ms)" } else { "" })) "" $null "ms"
    "{0,-14} median {1,6:F2} ms  [{2:F2}..{3:F2}]  baseline {4}  spikes {5}{6}" -f $n, $med, $(if ($sorted.Count) { $sorted[0] } else { -1 }), $(if ($sorted.Count) { $sorted[-1] } else { -1 }), ("{0}-{1}" -f $d.base[0], $d.base[1]), $spikes, $(if ($reg) { "  <-- check" } else { "" })
}
Write-WfcCsv $rows (Join-Path $OutDir "perf_release.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
