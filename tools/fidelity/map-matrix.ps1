# Multi-map readiness matrix on ONE executable (Milestone 06: the generic map path).
#
#   .\tools\fidelity\map-matrix.ps1 -Root <work\ab\<target>> -OutDir <dir> [-Maps a,b] [-Views 20] [-Chaos 20]
#
# Per map, independent of the product's own claims:
#   LOADS         direct load (WFC_BOOT=match WFC_MAP=<map>): the map's world, render data and audio come up; load
#                 log errors / material or shader failures counted
#   VISUAL        views spread over the WHOLE map (farthest-point sample of the navigation nodes, 2 directions each,
#                 plus 2 overviews), composed product frames judged in pixels: black, flat untextured grey, washed out
#                 (low contrast, high mean), magenta default material, saturation; contact sheet for the human
#   STRUCTURAL    Gameplay's own WFC_CHAOS (N starts x 20 s) and WFC_XFORMTEST on that map (WFC_MAP), parsed with locations
# The frontend-selected TDM lifecycle per map is m06-maps-tdm.ps1; collision / transform stress per map are
# vehicle-collision.ps1 / transform-stress.ps1 -Map.
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$OutDir, [string[]]$Maps = @(), [int]$Views = 20, [int]$Chaos = 20, [switch]$NoSelfTests)
$ErrorActionPreference = "Stop"
$Maps = @($Maps | ForEach-Object { $_ -split "," } | Where-Object { $_ })
if (-not $Maps.Count) { $Maps = @("MP_IAC_Streets", "MP_UND_Gorge", "MP_IAC_Seed", "MP_IAC_Berth", "MP_UND_Complex", "MP_IAC_Rust", "MP_ORB_Debris", "MP_KON_Molten", "MP_ESC_BrokenHope", "MP_ESC_Remnant") }
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"; $rd = Join-Path $Root "work\render"
$H = Get-ExeHooks $exe
$res = New-WfcResults
function Res($id, $status, $note, $owner = "", $m = $null) { Add-WfcResult $res "maps.$id" $status $m $note $owner }

# image measures on one frame: black, flat grey, washed out (contrast), magenta, saturation
function Frame-Measures([string]$img) {
    $c = [WfcImage]::Rgb($img, 8); $w = [int]$c[0]; $h2 = [int]$c[1]; $n = $w * $h2
    $sumL = 0.0; $sumL2 = 0.0; $black = 0; $mag = 0; $sat = 0.0
    for ($i = 0; $i -lt $n; $i++) { $r = $c[2 + 3 * $i]; $g = $c[3 + 3 * $i]; $b = $c[4 + 3 * $i]; $l = 0.299 * $r + 0.587 * $g + 0.114 * $b
        $sumL += $l; $sumL2 += $l * $l; if ($l -lt 6) { $black++ }; if ($r -gt 170 -and $b -gt 170 -and $g -lt 90) { $mag++ }
        $mx = [Math]::Max([Math]::Max($r, $g), $b); $mn = [Math]::Min([Math]::Min($r, $g), $b); if ($mx -gt 0) { $sat += ($mx - $mn) / $mx } }
    $mean = $sumL / $n; $std = [Math]::Sqrt([Math]::Max(0, $sumL2 / $n - $mean * $mean))
    return [pscustomobject]@{ mean = [Math]::Round($mean, 1); std = [Math]::Round($std, 1); black = [Math]::Round($black / $n, 3); magenta = [Math]::Round($mag / $n, 4); sat = [Math]::Round($sat / $n, 3); flatgrey = (FlatGreyFraction $img) }
}

$rows = New-Object System.Collections.Generic.List[object]
foreach ($map in $Maps) {
    $md = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$map"; $d = Join-Path $OutDir $map; New-Item -ItemType Directory -Force $d | Out-Null
    $hasRd = Test-Path (Join-Path $rd $map)
    if (-not (Test-Path $md)) { Res "$map.loads" "SKIP" "no runtime data for $map (source data absent or not exported)" "AssetTools"; continue }
    # ---- viewpoints: farthest-point sample over the navigation nodes; two directions each (toward / away from the centre)
    $nodes = @((Get-Content -Raw (Join-Path $md "navigation.json") | ConvertFrom-Json).nodes | Where-Object { @($_.location_gltf).Count -ge 3 } | ForEach-Object { , @([double]$_.location_gltf[0], [double]$_.location_gltf[1], [double]$_.location_gltf[2]) })
    if (-not $nodes.Count) { Res "$map.loads" "SKIP" "no navigation nodes" "AssetTools"; continue }
    $cx = ($nodes | ForEach-Object { $_[0] } | Measure-Object -Average).Average; $cy = ($nodes | ForEach-Object { $_[1] } | Measure-Object -Average).Average; $cz = ($nodes | ForEach-Object { $_[2] } | Measure-Object -Average).Average
    $pick = @($nodes[0]); $dmin = @($nodes | ForEach-Object { [double]::MaxValue })
    while ($pick.Count -lt [Math]::Min($Views, $nodes.Count)) {
        $last = $pick[-1]; $best = -1; $bd = -1.0
        for ($i = 0; $i -lt $nodes.Count; $i++) { $p = $nodes[$i]; $dd = [Math]::Pow($p[0] - $last[0], 2) + [Math]::Pow($p[2] - $last[2], 2) + [Math]::Pow($p[1] - $last[1], 2); if ($dd -lt $dmin[$i]) { $dmin[$i] = $dd }; if ($dmin[$i] -gt $bd) { $bd = $dmin[$i]; $best = $i } }
        $pick += , $nodes[$best]
    }
    $shots = @(); $k = 0
    # warm-up: the first frames of a direct boot are captured before the map is drawn
    for ($w = 0; $w -lt 4; $w++) { $p = $pick[0]; $shots += @{ name = "warm$w"; c = @($p[0], ($p[1] + 2.2), $p[2]); t = @(($p[0] + 10), ($p[1] + 1.2), $p[2]) } }
    foreach ($p in $pick) {
        $c = @($p[0], ($p[1] + 2.2), $p[2]); $dx = $cx - $p[0]; $dz = $cz - $p[2]; $ln = [Math]::Max(0.01, [Math]::Sqrt($dx * $dx + $dz * $dz)); $dx /= $ln; $dz /= $ln
        $shots += @{ name = ("v{0:D2}a" -f $k); c = $c; t = @(($c[0] + 15 * $dx), ($c[1] - 1.0), ($c[2] + 15 * $dz)) }
        $shots += @{ name = ("v{0:D2}b" -f $k); c = $c; t = @(($c[0] - 15 * $dz), ($c[1] - 1.0), ($c[2] + 15 * $dx)) }
        $k++
    }
    $ext = [Math]::Sqrt((($nodes | ForEach-Object { [Math]::Pow($_[0] - $cx, 2) + [Math]::Pow($_[2] - $cz, 2) }) | Measure-Object -Maximum).Maximum)
    $shots += @{ name = "ov1"; c = @(($cx + $ext * 0.8), ($cy + $ext * 0.5), ($cz + $ext * 0.8)); t = @($cx, $cy, $cz) }
    $shots += @{ name = "ov2"; c = @(($cx - $ext * 0.8), ($cy + $ext * 0.5), ($cz - $ext * 0.8)); t = @($cx, $cy, $cz) }
    $t0 = Get-Date
    $null = Invoke-ShotList $exe (Join-Path $d "shots") $shots @{ WFC_BOOT = "match"; WFC_MAP = $map } $(if ($hasRd) { $rd } else { "" }) @($shots | ForEach-Object { $_.name })
    $secs = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    $log = Join-Path $d "shots\wfc.log"
    $errs = @(Grep-Log $log '^\[(error|ERROR)'); $warns = @(Grep-Log $log '^\[(warn|WARN)')
    $matFail = @(Grep-Log $log '(?i)(shader|material).*(fail|error|unsupported)|compile error')
    $loaded = @(Grep-Log $log '(?i)starts: \d+ player starts|uploaded mesh 0:|render data').Count -gt 0
    $imgs = @(Get-ChildItem (Join-Path $d "shots") -Filter *.jpg | Where-Object { $_.BaseName -notlike "warm*" } | Sort-Object Name)
    $fm = @($imgs | ForEach-Object { $m = Frame-Measures $_.FullName; $m | Add-Member -NotePropertyName shot -NotePropertyValue $_.BaseName -PassThru })
    Write-WfcCsv $fm (Join-Path $d "frames.csv")
    if ($imgs.Count) { New-WfcSheet @($imgs | Select-Object -First 24 | ForEach-Object { @{ png = $_.FullName; label = "$map $($_.BaseName)" } }) (Join-Path $OutDir "sheet_$map.png") 6 320 180 }
    $med = { param($prop) $v = @($fm | ForEach-Object { $_.$prop } | Sort-Object); if ($v.Count) { $v[[int]($v.Count / 2)] } else { $null } }
    $row = [pscustomobject][ordered]@{ map = $map; render_data = $hasRd; loads = $loaded -and $imgs.Count -gt 0; load_s = $secs; views = $imgs.Count; errors = $errs.Count; warnings = $warns.Count; material_failures = $matFail.Count
        black_views = @($fm | Where-Object { $_.black -gt 0.6 }).Count; magenta_views = @($fm | Where-Object { $_.magenta -gt 0.01 }).Count; flatgrey_views = @($fm | Where-Object { $_.flatgrey -gt 0.15 }).Count
        median_mean = (& $med "mean"); median_std = (& $med "std"); median_sat = (& $med "sat"); washed_views = @($fm | Where-Object { $_.mean -gt 140 -and $_.std -lt 28 }).Count
        chaos = ""; chaos_under = $null; chaos_killz = $null; chaos_stuck = $null; xform = ""; xform_under = $null }
    # ---- Gameplay's structural self-tests on this map
    if (-not $NoSelfTests) {
        if ($H.Contains("WFC_CHAOS")) { $cdir = Join-Path $d "chaos"; $null = Invoke-WfcExe $exe $cdir @{ WFC_CHAOS = "$Chaos"; WFC_MAP = $map; WFC_RENDER_DATA = $rd } "run.log" 1800
            $cs = @(Grep-Log (Join-Path $cdir "wfc.log") 'CHAOS SUMMARY')[0]; $cl = @(Grep-Log (Join-Path $cdir "wfc.log") 'CHAOS (UNDER-FLOOR|STUCK|KILLZ|PROP)')
            if ($cs) { $m = [regex]::Match($cs.text, ': (\d+) runs UNDER THE MAP.*?, (\d+) KillZ, (\d+) stuck'); $row.chaos_under = [int]$m.Groups[1].Value; $row.chaos_killz = [int]$m.Groups[2].Value; $row.chaos_stuck = [int]$m.Groups[3].Value; $row.chaos = (($cl | ForEach-Object { $_.text -replace '^.*\] CHAOS ', '' }) -join " | ") } }
        if ($H.Contains("WFC_XFORMTEST")) { $xdir = Join-Path $d "xform"; $null = Invoke-WfcExe $exe $xdir @{ WFC_XFORMTEST = "1"; WFC_MAP = $map; WFC_RENDER_DATA = $rd } "run.log" 1800
            $xs = @(Grep-Log (Join-Path $xdir "wfc.log") 'XFORM SUMMARY')[0]; if ($xs) { $m = [regex]::Match($xs.text, 'SUMMARY: (\d+)/(\d+)'); $row.xform_under = [int]$m.Groups[1].Value; $row.xform = "$($m.Groups[1].Value)/$($m.Groups[2].Value)" } }
    }
    $rows.Add($row)
    # ---- verdicts per map
    Res "$map.loads" $(if ($row.loads) { "PASS" } else { "FAIL" }) ("direct load: {0} views captured in {1} s; log errors {2}, warnings {3}; render data present {4}" -f $row.views, $row.load_s, $row.errors, $row.warnings, $hasRd) "Integration/Rendering"
    Res "$map.materials" $(if ($row.material_failures -eq 0) { "PASS" } else { "PARTIAL" }) ("material / shader failure lines: {0} {1}" -f $row.material_failures, (($matFail | Select-Object -First 3 | ForEach-Object { $_.text -replace '^.*\] ', '' }) -join " | ")) "Rendering"
    Res "$map.visual_metrics" $(if ($row.black_views -gt $row.views * 0.25 -or $row.magenta_views -or $row.flatgrey_views -gt 2) { "FAIL" } elseif ($row.washed_views -gt $row.views * 0.3) { "HUMAN" } else { "HUMAN" }) ("{0} views over the map: mostly-black {1}, magenta default material {2}, flat untextured grey {3}, washed out (mean > 140, contrast < 28) {4}; median luma {5}, contrast {6}, saturation {7}. Pixels cannot prove the map looks like WFC: sheet_{8}.png is the human check" -f $row.views, $row.black_views, $row.magenta_views, $row.flatgrey_views, $row.washed_views, $row.median_mean, $row.median_std, $row.median_sat, $map) "Rendering/AssetTools"
    if ($row.chaos -ne "" -or $row.chaos_under -ne $null) { Res "$map.chaos" $(if ($row.chaos_under -or $row.chaos_killz) { "FAIL" } elseif ($row.chaos_stuck) { "PARTIAL" } else { "PASS" }) ("WFC_CHAOS {0} starts x 20 s: under the map {1}, KillZ {2}, stuck {3}. {4}" -f $Chaos, $row.chaos_under, $row.chaos_killz, $row.chaos_stuck, $row.chaos) "Gameplay" }
    if ($row.xform) { Res "$map.xform" $(if ($row.xform_under) { "FAIL" } else { "PASS" }) ("WFC_XFORMTEST: {0} transforms ended under the map / KillZ" -f $row.xform) "Gameplay" }
}
Write-WfcCsv $rows (Join-Path $OutDir "map_matrix.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"MAP MATRIX: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
