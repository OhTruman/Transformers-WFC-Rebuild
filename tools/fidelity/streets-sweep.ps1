# Deterministic MP_IAC_Streets visual sweep for before/after comparison between branch checkpoints.
#
#   .\tools\fidelity\streets-sweep.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Baseline <previous sweep dir>]
#
# Coverage from AUTHORED data only (no hand-placed cameras):
#   * navigation.json: every node (starts, pickups, ...) + samples every 10 m (at least the midpoint) along every walkable edge,
#     thinned to one point per 12 m x 3 m (XZ, height) cell; 4 horizontal views at eye height per point.
#   * categories per point: zone of the nearest authored start (audio.json player_start_zones: exterior /
#     tunnel = TRAIN_TUNNEL, NEU_HALL, NEU_STAIRWELL / interior), ramp_stairs (on an edge with slope > 0.12),
#     glass (within 6 m of a glass-floor prop), pickup (pickup node), start (player start node), plus
#     skyline (an extra upward view at exterior points).
#   * targets: movers (movers.json: rotating domes, SkyBeam), particles (map_fx_runtime steam + pickup FX).
#   * characters: robot and vehicle with the normal chase camera at 12 spread authored starts (lighting,
#     shadows, character/world cohesion).
# Each shot: half-size JPEG + a 40x22 luma fingerprint (fingerprints.json) + mean luma / near-black / flat.
# With -Baseline: per-shot mean luma change vs the baseline fingerprint; changed shots ranked, and a
# before/after contact sheet of the most changed ones. Runs are chunked (200 shots per exe) to bound memory.
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [string]$Baseline = "", [int]$Chunk = 200, [double]$Grid = 12.0, [switch]$CompareOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1")
# Fingerprints are written as plain JSON arrays (PowerShell 5.1 ConvertTo-Json wraps typed arrays as {value, Count}).
function Save-Fp($fp, $path) {
    $ic = [Globalization.CultureInfo]::InvariantCulture
    $parts = foreach ($k in $fp.Keys) { '"' + $k + '":[' + (($fp[$k] | ForEach-Object { ([double]$_).ToString("0.#", $ic) }) -join ',') + ']' }
    [IO.File]::WriteAllText($path, '{' + ($parts -join ',') + '}')
}
function Load-Fp($path) {
    $o = Get-Content -Raw $path | ConvertFrom-Json; $h = @{}
    foreach ($pr in $o.PSObject.Properties) { $v = $pr.Value; if ($v.PSObject.Properties.Name -contains 'value') { $v = $v.value }; $h[$pr.Name] = [double[]]@($v) }
    return $h
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$nav = Get-Content -Raw "$WfcSlice\navigation.json" | ConvertFrom-Json
$au = Get-Content -Raw "$WfcSlice\audio.json" | ConvertFrom-Json
$sp = Get-Content -Raw "$WfcSlice\spawnpoints.json" | ConvertFrom-Json
$props = (Get-Content -Raw "$WfcSlice\props.json" | ConvertFrom-Json).props
# ---- authored points ----
$loc = @{}; $cls = @{}; foreach ($n in $nav.nodes) { $loc[$n.node] = $n.location_gltf; $cls[$n.node] = $n.class }
$zoneOf = @{}; foreach ($s in $au.player_start_zones.starts_detail) { $zoneOf[$s.actor] = if (@($s.zones).Count) { @($s.zones)[0] } else { "NONE" } }
$startPts = @($sp.points | Where-Object { $_.class -like "*PlayerStart" -and @($_.location_gltf).Count -ge 3 } | ForEach-Object { @{ actor = $_.actor; p = $_.location_gltf } })
function NearestZone($p) { $b = $null; $bd = 1e9; foreach ($s in $startPts) { $d = [Math]::Pow($s.p[0] - $p[0], 2) + [Math]::Pow($s.p[2] - $p[2], 2) + 4 * [Math]::Pow($s.p[1] - $p[1], 2); if ($d -lt $bd) { $bd = $d; $b = $s } }; return $zoneOf[$b.actor] }
# glass: centroids of the glass primitives themselves (a prop's origin can be far from its glass surface)
$glassPts = @()
foreach ($p in $props) {
    $mats = (@($p.section_materials) + @($p.material_overrides | Where-Object { $_ -is [string] }))
    $gm = @($mats | Where-Object { $_ -match "IAC_Glass|GlassPan2" } | Select-Object -First 1)
    if ($gm.Count) { $short = $gm[0].Split('.')[-1]; $cs = PropCorners $p $short; if ($cs.Count) { $glassPts += , (Centroid $cs) } }
}
$cells = @{}; $pts = New-Object System.Collections.Generic.List[object]
function AddPt($p, $tags) {
    $k = "{0},{1},{2}" -f [Math]::Floor($p[0] / $Grid), [Math]::Floor($p[2] / $Grid), [Math]::Floor($p[1] / 3.0)
    if ($cells.ContainsKey($k)) { foreach ($t in $tags) { if ($cells[$k].tags -notcontains $t) { $cells[$k].tags += $t } }; return }
    $o = @{ p = $p; tags = @($tags) }; $cells[$k] = $o; $pts.Add($o)
}
foreach ($n in $nav.nodes) {
    $t = @(); if ($n.class -like "*PlayerStart") { $t += "start" }; if ($n.class -like "*PickupFactory") { $t += "pickup" }
    AddPt @([double]$n.location_gltf[0], [double]$n.location_gltf[1], [double]$n.location_gltf[2]) $t
}
foreach ($e in $nav.edges) {
    $a = $loc[$e.start.Split('.')[-1]]; $b = $loc[$e.end.Split('.')[-1]]; if (-not $a -or -not $b) { continue }
    $h = [Math]::Sqrt([Math]::Pow($b[0] - $a[0], 2) + [Math]::Pow($b[2] - $a[2], 2)); if ($h -lt 1) { continue }
    $ramp = [Math]::Abs(($b[1] - $a[1]) / $h) -gt 0.12
    $k = [Math]::Max(2, [int][Math]::Floor($h / 10))      # at least the midpoint of every walkable edge
    for ($i = 1; $i -lt $k; $i++) { $f = $i / $k; AddPt @(($a[0] + $f * ($b[0] - $a[0])), ($a[1] + $f * ($b[1] - $a[1])), ($a[2] + $f * ($b[2] - $a[2]))) $(if ($ramp) { @("ramp_stairs") } else { @() }) }
}
foreach ($o in $pts) {
    $z = NearestZone $o.p; $o.zone = $z
    $o.tags += $(if ($z -eq "ENTER_EXTERIOR") { "exterior" } elseif ($z -in "ENTER_TRAIN_TUNNEL", "ENTER_NEU_HALL", "ENTER_NEU_STAIRWELL") { "tunnel" } else { "interior" })
    foreach ($g in $glassPts) { if ([Math]::Abs($g[0] - $o.p[0]) -lt 6 -and [Math]::Abs($g[2] - $o.p[2]) -lt 6 -and [Math]::Abs($g[1] - $o.p[1]) -lt 4) { $o.tags += "glass"; break } }
}
# ---- shots ----
$shots = New-Object System.Collections.Generic.List[object]
$pi = 0
foreach ($o in $pts) {
    $eye = @($o.p[0], ($o.p[1] + 3.0), $o.p[2])
    for ($k = 0; $k -lt 4; $k++) { $az = $k * [Math]::PI / 2 + 0.4; $t = @(($eye[0] + 10 * [Math]::Sin($az)), ($eye[1] - 0.8), ($eye[2] + 10 * [Math]::Cos($az))); $shots.Add(@{ name = ("p{0:D3}_v{1}" -f $pi, $k); c = $eye; t = $t; tags = ($o.tags -join "|"); zone = $o.zone }) }
    if ($o.tags -contains "exterior") { $shots.Add(@{ name = ("p{0:D3}_sky" -f $pi); c = $eye; t = @(($eye[0] + 6), ($eye[1] + 4.5), $eye[2]); tags = (($o.tags + "skyline") -join "|"); zone = $o.zone }) }
    $o.id = $pi; $pi++
}
$mv = Get-Content -Raw (Join-Path $RenderData "MP_IAC_Streets\movers.json") | ConvertFrom-Json
foreach ($m in @($mv.rotating) + @($mv.matinee)) { $p = UeToGltf $m.location_ue[0] $m.location_ue[1] $m.location_ue[2]; for ($k = 0; $k -lt 2; $k++) { $c = RingCam $p ($k * [Math]::PI + 0.6) 14 4; $shots.Add(@{ name = ("mover_{0}_v{1}" -f $m.actor, $k); c = $c; t = $p; tags = "moving_scenery"; zone = "" }) } }
$fx = (Get-Content -Raw (Join-Path $RenderData "MP_IAC_Streets\map_fx_runtime.json") | ConvertFrom-Json).instances
$q = 0; foreach ($i in $fx) { if ($i.template -notlike "*Steam*" -and ($q++ % 9) -ne 0) { continue }; $m = $i.ue_matrix[3]; $p = UeToGltf $m[0] $m[1] $m[2]; $p = @($p[0], ($p[1] + 1), $p[2]); $shots.Add(@{ name = ("fx_{0}" -f $i.owner); c = (RingCam $p 0.8 9 2); t = $p; tags = $(if ($i.template -like "*Steam*") { "particles" } else { "particles|pickup" }); zone = "" }) }
Write-Host ("sweep: {0} points, {1} shots" -f $pts.Count, $shots.Count)
if ($CompareOnly) { $fp = Load-Fp (Join-Path $OutDir "fingerprints.json"); $rows = @(Import-Csv (Join-Path $OutDir "shots.csv")); $shotDir = Join-Path $OutDir "shots" }
# ---- capture (chunked) ----
if (-not $CompareOnly) {
$shotDir = Join-Path $OutDir "shots"; New-Item -ItemType Directory -Force $shotDir | Out-Null
$fp = @{}; $rows = New-Object System.Collections.Generic.List[object]
for ($c0 = 0; $c0 -lt $shots.Count; $c0 += $Chunk) {
    $part = @($shots[$c0..([Math]::Min($shots.Count, $c0 + $Chunk) - 1)])
    $r = Invoke-ShotList $Exe (Join-Path $OutDir ("chunk{0:D2}" -f [int]($c0 / $Chunk))) $part @{} $RenderData -Keep @($part | ForEach-Object { $_.name })
    foreach ($s in $part) {
        $g = $r.grids[$s.name]; if (-not $g) { continue }
        $j = Join-Path (Join-Path $OutDir ("chunk{0:D2}" -f [int]($c0 / $Chunk))) "$($s.name).jpg"; if (Test-Path $j) { Move-Item -Force $j (Join-Path $shotDir "$($s.name).jpg") }
        $w = [int]$g[0]; $h = [int]$g[1]; $fw = 40; $fh = 22; $f = New-Object double[] ($fw * $fh)
        for ($y = 0; $y -lt $fh; $y++) { for ($x = 0; $x -lt $fw; $x++) { $gx = [int]($x * $w / $fw); $gy = [int]($y * $h / $fh); $i = 2 + 3 * ($gy * $w + $gx); $f[$y * $fw + $x] = [Math]::Round(0.299 * $g[$i] + 0.587 * $g[$i + 1] + 0.114 * $g[$i + 2], 1) } }
        $fp[$s.name] = $f
        $lum = [WfcImage]::Luma((Join-Path $shotDir "$($s.name).jpg"), 4); $st = [WfcImage]::Stats($lum, 6.0)
        $rows.Add([pscustomobject][ordered]@{ shot = $s.name; tags = $s.tags; zone = $s.zone; cam = ("{0:F1},{1:F1},{2:F1}" -f $s.c[0], $s.c[1], $s.c[2]); mean_luma = [Math]::Round($st[0], 1); black = [Math]::Round([WfcImage]::BlackFraction($g, 3.0), 3); flat = [Math]::Round($st[2], 3) })
    }
    Remove-Item -Recurse -Force (Join-Path $OutDir ("chunk{0:D2}" -f [int]($c0 / $Chunk)))
}
# ---- characters: robot / vehicle with the chase camera at spread starts ----
$charDir = Join-Path $OutDir "characters"; New-Item -ItemType Directory -Force $charDir | Out-Null
foreach ($k in 0, 7, 14, 21, 28, 35, 42, 49, 56, 63, 70, 77) { foreach ($form in "robot", "vehicle") {
    $d = Join-Path $charDir ("{0}_s{1:D2}" -f $form, $k); $shot = Join-Path $d "shot.bmp"
    $e = @{ WFC_SMOKE_FRAMES = "90"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0"; WFC_START = "$k"; WFC_SHOT = $shot }
    if ($form -eq "vehicle") { $e.WFC_STARTVEHICLE = "1" }
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    New-Item -ItemType Directory -Force $d | Out-Null; $null = Invoke-WfcExe $Exe $d $e "run.log" 600
    if (Test-Path $shot) {
        $name = "char_{0}_s{1:D2}" -f $form, $k; $g = [WfcImage]::Rgb($shot, 8)
        Save-WfcJpeg $shot (Join-Path $shotDir "$name.jpg"); Remove-Item $shot
        $w = [int]$g[0]; $h = [int]$g[1]; $f = New-Object double[] (880)
        for ($y = 0; $y -lt 22; $y++) { for ($x = 0; $x -lt 40; $x++) { $i = 2 + 3 * ([int]($y * $h / 22) * $w + [int]($x * $w / 40)); $f[$y * 40 + $x] = [Math]::Round(0.299 * $g[$i] + 0.587 * $g[$i + 1] + 0.114 * $g[$i + 2], 1) } }
        $fp[$name] = $f
        $rows.Add([pscustomobject][ordered]@{ shot = $name; tags = "$form|lighting|shadows"; zone = ""; cam = "chase"; mean_luma = [Math]::Round(($f | Measure-Object -Average).Average, 1); black = [Math]::Round([WfcImage]::BlackFraction($g, 3.0), 3); flat = "" })
    }
} }
Write-WfcCsv $rows (Join-Path $OutDir "shots.csv")
}   # end capture
Save-Fp $fp (Join-Path $OutDir "fingerprints.json")
# ---- contact sheets per category ----
$res = New-WfcResults
foreach ($cat in "interior", "exterior", "tunnel", "ramp_stairs", "glass", "skyline", "moving_scenery", "particles", "pickup", "start", "robot", "vehicle") {
    $sel = @($rows | Where-Object { ($_.tags -split '\|') -contains $cat })
    if (-not $sel.Count) { Add-WfcResult $res "sweep.category.$cat" "INFO" 0 "no shots"; continue }
    $pick = if ($sel.Count -le 24) { $sel } else { @(0..23 | ForEach-Object { $sel[[int]($_ * ($sel.Count - 1) / 23)] }) }
    New-WfcSheet @($pick | ForEach-Object { @{ png = (Join-Path $shotDir "$($_.shot).jpg"); label = ("{0} {1} L{2}" -f $_.shot, $_.zone, $_.mean_luma); flag = ([double]$_.black -ge 0.4) } }) (Join-Path $OutDir "sheet_$cat.png") 6 320 180
    Save-WfcJpeg (Join-Path $OutDir "sheet_$cat.png") (Join-Path $OutDir "sheet_$cat.jpg"); Remove-Item (Join-Path $OutDir "sheet_$cat.png")
    $dark = @($sel | Where-Object { [double]$_.black -ge 0.4 })
    Add-WfcResult $res "sweep.category.$cat" "INFO" $sel.Count ("{0} shots; mean luma {1:F1}; {2} with >= 40% near-black cells{3}" -f $sel.Count, ($sel | Measure-Object mean_luma -Average).Average, $dark.Count, $(if ($dark.Count) { ": " + (($dark | Select-Object -First 6 | ForEach-Object { $_.shot }) -join ", ") } else { "" }))
}
$darkAll = @($rows | Where-Object { [double]$_.black -ge 0.6 } | Sort-Object { [double]$_.black } -Descending)
Add-WfcResult $res "sweep.near_black_views" $(if ($darkAll.Count) { "HUMAN" } else { "INFO" }) $darkAll.Count ("views >= 60% near-black (unlit / missing / void candidates): " + (($darkAll | Select-Object -First 12 | ForEach-Object { "{0} ({1:P0})" -f $_.shot, [double]$_.black }) -join ", "))
# ---- before/after ----
if ($Baseline -and (Test-Path (Join-Path $Baseline "fingerprints.json"))) {
    $old = Load-Fp (Join-Path $Baseline "fingerprints.json")
    $chg = @()
    foreach ($n in $fp.Keys) {
        $o = $old[$n]; if (-not $o) { continue }
        $s = 0.0; for ($i = 0; $i -lt 880; $i++) { $s += [Math]::Abs($fp[$n][$i] - $o[$i]) }
        $chg += [pscustomobject]@{ shot = $n; diff = [Math]::Round($s / 880, 2) }
    }
    $chg = @($chg | Sort-Object diff -Descending)
    Write-WfcCsv $chg (Join-Path $OutDir "vs_baseline.csv")
    $missing = @($old.Keys | Where-Object { -not $fp.ContainsKey($_) })
    $top = @($chg | Where-Object { $_.diff -ge 3 } | Select-Object -First 16)
    $tiles = @(); foreach ($c in $top) { $tiles += @{ png = (Join-Path (Join-Path $Baseline "shots") "$($c.shot).jpg"); label = "BEFORE $($c.shot)" }; $tiles += @{ png = (Join-Path $shotDir "$($c.shot).jpg"); label = "AFTER diff $($c.diff)"; flag = $true } }
    if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_changed.png") 4 400 225; Save-WfcJpeg (Join-Path $OutDir "sheet_changed.png") (Join-Path $OutDir "sheet_changed.jpg"); Remove-Item (Join-Path $OutDir "sheet_changed.png") }
    Add-WfcResult $res "sweep.vs_baseline" $(if ($top.Count) { "HUMAN" } else { "INFO" }) $top.Count ("{0} shots compared; {1} changed by >= 3 luma (sheet_changed.jpg: before/after); {2} baseline shots missing now" -f $chg.Count, @($chg | Where-Object { $_.diff -ge 3 }).Count, $missing.Count)
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"STREETS SWEEP: {0} shots -> {1}" -f $rows.Count, $OutDir
