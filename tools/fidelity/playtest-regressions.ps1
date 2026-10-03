# Human-playtest regression suite: reproducible measurements for reported Streets visual problems.
# MEASURES what the runtime does; it does not decide the correct implementation. Flags are HUMAN (= HUMAN
# CHECK REQUIRED) with the image to look at; nothing here is a FAIL by itself.
#
#   .\tools\fidelity\playtest-regressions.ps1 -Exe <Release wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Baseline <dir>]
#
# Reported observations -> scenario:
#   1/2/3 smoke veil at distance / changes up close / red-purple smoke  -> "sheets": the 66 authored translucent
#         sheet props (FogSheet_DepthBiased Blue/RED/Purple, BckSillouhetteSmoke) and "particles": the map FX
#         (8 Steam_Sm_FX + one of each pickup template). Distance series 60/30/15/6/2 m from 4 azimuths; A/B
#         against the same shots with the effect removed (WFC_SKIPMAT=<material> / WFC_NOMAPFX) gives the
#         effect's screen coverage, signed RGB contribution (hue) and strength per distance.
#   4     static-looking particles -> the effect's own change between two samples 1.07 s apart (masked).
#   5/6   glass / transparent floor depth order -> 8 glass-floor props (IAC_Glass2_light, IAC_Glass,
#         IAC_Decagon_GlassPan2, IAC_Glass2_dense_Dark) from 4 azimuths x 3 elevations (above, grazing, below);
#         the glass primitive's projected polygon (shrunk 15%) vs where the glass actually contributes:
#         "holes" = something drawn over / instead of the glass inside its own outline.
#   7     black geometry behind ramps -> authored navigation edges with slope (ramps/stairs): views along and
#         across; near-black cell fraction.
#   8     boost -> robot under the map -> transform-stress.ps1 (Phase 3).
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [string]$Baseline = "", [string[]]$Families = @("sheets", "particles", "glass", "ramps"))
$ErrorActionPreference = "Stop"
$Families = @($Families | ForEach-Object { $_ -split "," } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$props = (Get-Content -Raw "$WfcSlice\props.json" | ConvertFrom-Json).props
$res = New-WfcResults
$tiles = @{}
function Tile($group, $png, $label, $flag = $false) { if (-not $tiles[$group]) { $script:tiles[$group] = @() }; $script:tiles[$group] += @{ png = $png; label = $label; flag = $flag } }
function Pick($list, $n) { $l = @($list); if ($l.Count -le $n) { return $l }; $out = @(); for ($i = 0; $i -lt $n; $i++) { $out += $l[[int][Math]::Floor($i * ($l.Count - 1) / [Math]::Max(1, $n - 1))] }; return $out }
function Hue($r, $g, $b) {
    $mx = [Math]::Max($r, [Math]::Max($g, $b)); if ($mx -lt 2) { return "neutral" }
    if ($r -gt 1.3 * $g -and $b -gt 1.15 * $g) { return "purple/magenta" }
    if ($r -gt 1.3 * [Math]::Max($g, $b)) { return "red" }
    if ($b -gt 1.2 * [Math]::Max($r, $g)) { return "blue" }
    if ([Math]::Max($r, [Math]::Max($g, $b)) - [Math]::Min($r, [Math]::Min($g, $b)) -lt 0.2 * $mx) { return "grey/white" }
    return "mixed"
}
$dists = @(60, 30, 15, 6, 2)
$azs = @(0, 1, 2, 3) | ForEach-Object { $_ * [Math]::PI / 2 + 0.3 }

# ---------- 1-4: sheets and particles (distance series + motion) ----------
function EffectFamily($fam, $targets, $skipEnv) {
    $shots = New-Object System.Collections.Generic.List[object]
    foreach ($t in $targets) {
        foreach ($d in $dists) { for ($ai = 0; $ai -lt 4; $ai++) { $shots.Add(@{ name = ("{0}_d{1:D2}_a{2}" -f $t.id, $d, $ai); c = (RingCam $t.pos $azs[$ai] $d ([Math]::Min(2.0, $d * 0.3))); t = $t.pos; tid = $t.id; d = $d; a = $ai }) } }
        # motion: same camera at 15 m, 8 consecutive shots (8 frames each) -> first and last are 56 frames = 0.93 s apart
        for ($k = 0; $k -lt 8; $k++) { $shots.Add(@{ name = ("{0}_m{1}" -f $t.id, $k); c = (RingCam $t.pos $azs[0] 15 2.0); t = $t.pos; tid = $t.id; d = -1; a = 0 }) }
    }
    $dir = Join-Path $OutDir $fam
    Write-Host "[$fam] $($shots.Count) shots x 2 runs"
    $B = Invoke-ShotList $Exe (Join-Path $dir "without") $shots $skipEnv $RenderData
    $keep = @($shots | Where-Object { $_.d -in 60, 15, 2 -and $_.a -eq 0 } | ForEach-Object { $_.name })
    $A = Invoke-ShotList $Exe (Join-Path $dir "with") $shots @{} $RenderData -Keep $keep
    $rows = @()
    foreach ($t in $targets) {
        $best = @{}
        foreach ($d in $dists) {
            $bestA = $null
            for ($ai = 0; $ai -lt 4; $ai++) {
                $n = "{0}_d{1:D2}_a{2}" -f $t.id, $d, $ai
                if (-not $A.grids[$n] -or -not $B.grids[$n]) { continue }
                $m = [WfcImage]::MaskStats($A.grids[$n], $B.grids[$n], 3.0)
                if (-not $bestA -or $m[0] -gt $bestA.cov) { $bestA = @{ cov = $m[0]; r = $m[1]; g = $m[2]; b = $m[3]; l = $m[4]; mx = $m[5]; a = $ai } }
            }
            if ($bestA) { $best[$d] = $bestA; $rows += [pscustomobject][ordered]@{ family = $fam; target = $t.id; material = $t.mat; distance_m = $d; best_azimuth = $bestA.a; coverage = [Math]::Round($bestA.cov, 4); dR = [Math]::Round($bestA.r, 1); dG = [Math]::Round($bestA.g, 1); dB = [Math]::Round($bestA.b, 1); mean_dL = [Math]::Round($bestA.l, 1); max_dL = [Math]::Round($bestA.mx, 1); hue = (Hue $bestA.r $bestA.g $bestA.b) } }
        }
        $m0 = "{0}_m0" -f $t.id; $m7 = "{0}_m7" -f $t.id
        $motion = if ($A.grids[$m0] -and $A.grids[$m7]) { [WfcImage]::MaskMotion($A.grids[$m0], $B.grids[$m0], $A.grids[$m7], $B.grids[$m7], 3.0) } else { -1 }
        $covM = if ($A.grids[$m0]) { ([WfcImage]::MaskStats($A.grids[$m0], $B.grids[$m0], 3.0))[0] } else { 0 }
        $c60 = if ($best[60]) { $best[60].cov } else { 0 }; $c30 = if ($best[30]) { $best[30].cov } else { 0 }
        $c6 = if ($best[6]) { $best[6].cov } else { 0 }; $c2 = if ($best[2]) { $best[2].cov } else { 0 }
        $id = "playtest.$fam.$($t.id)"
        $series = ($dists | ForEach-Object { if ($best[$_]) { "{0}m {1:P0}/{2}" -f $_, $best[$_].cov, (Hue $best[$_].r $best[$_].g $best[$_].b) } }) -join ", "
        Add-WfcResult $res "$id.distance_series" "INFO" $c60 ("coverage/hue by distance (best of 4 azimuths): {0}; {1}" -f $series, $t.note)
        $veil = [Math]::Max($c60, $c30)
        Add-WfcResult $res "$id.far_veil" $(if ($veil -ge 0.35) { "HUMAN" } else { "INFO" }) $veil ("largest screen coverage at 30-60 m {0:P0} (>= 35% flagged: a distant effect filling a third of the view). Look at {1}_d60_a0.jpg / _d30" -f $veil, $t.id)
        if ($c6 -gt 0.03) { $ratio = $c2 / $c6; Add-WfcResult $res "$id.close_change" $(if ($ratio -lt 0.25 -or $ratio -gt 4) { "HUMAN" } else { "INFO" }) $ratio ("coverage at 2 m / at 6 m = {0:F2} ({1:P0} -> {2:P0}); < 0.25 or > 4 flagged as a drastic close-range change" -f $ratio, $c6, $c2) }
        $hueAll = @($dists | Where-Object { $best[$_] -and $best[$_].cov -gt 0.01 } | ForEach-Object { Hue $best[$_].r $best[$_].g $best[$_].b } | Select-Object -Unique)
        Add-WfcResult $res "$id.hue" $(if ($hueAll | Where-Object { $_ -in "red", "purple/magenta" }) { "HUMAN" } else { "INFO" }) $null ("effect colour contribution: {0} (material {1}); red/purple flagged for the reported red/purple smoke" -f ($hueAll -join ", "), $t.mat)
        if ($covM -gt 0.005) { Add-WfcResult $res "$id.motion" $(if ($motion -lt 0.5) { "HUMAN" } else { "INFO" }) $motion ("change of the effect's own contribution over 0.93 s at 15 m: {0:F2} luma (coverage {1:P1}); < 0.5 flagged static-looking" -f $motion, $covM) }
        else { Add-WfcResult $res "$id.motion" "INFO" -1 ("effect not visible from the 15 m motion camera (coverage {0:P1})" -f $covM) }
        $j = Join-Path (Join-Path $dir "with") ("{0}_d15_a0.jpg" -f $t.id); if (Test-Path $j) { Tile $fam $j ("{0} 15m cov {1:P0} {2}" -f $t.id, $(if ($best[15]) { $best[15].cov } else { 0 }), ($hueAll -join "/")) ($veil -ge 0.35) }
        $j = Join-Path (Join-Path $dir "with") ("{0}_d60_a0.jpg" -f $t.id); if (Test-Path $j) { Tile $fam $j ("{0} 60m cov {1:P0}" -f $t.id, $c60) ($c60 -ge 0.35) }
        $j = Join-Path (Join-Path $dir "with") ("{0}_d02_a0.jpg" -f $t.id); if (Test-Path $j) { Tile $fam $j ("{0} 2m cov {1:P0}" -f $t.id, $c2) }
    }
    Write-WfcCsv $rows (Join-Path $dir "series.csv")
}
if ($Families -contains "sheets") {
    $sheetMats = @("FogSheet_DepthBiased_Blue_MATINST", "FogSheet_DepthBiased_RED_MATINST", "FogSheet_DepthBiased_Purple_MATINST", "BckSillouhetteSmoke_MATINST")
    $targets = @()
    foreach ($m in $sheetMats) {
        $inst = @($props | Where-Object { (@($_.section_materials) + @($_.material_overrides | Where-Object { $_ -is [string] })) -match [regex]::Escape($m) } | Sort-Object { $_.gltf_matrix[12] })
        $k = 0
        foreach ($p in (Pick $inst 3)) {
            $cs = PropCorners $p $m; if (-not $cs.Count) { continue }
            $targets += @{ id = ("{0}_{1}" -f ($m -replace '_MATINST$', '' -replace '^FogSheet_DepthBiased_', 'Fog'), $k); pos = (Centroid $cs); mat = $m; note = "$($p.actor) ($($inst.Count) instances of $m)" }; $k++
        }
    }
    EffectFamily "sheets" $targets @{ WFC_SKIPMAT = ($sheetMats -join ";") }
}
if ($Families -contains "particles") {
    $fx = (Get-Content -Raw (Join-Path $RenderData "MP_IAC_Streets\map_fx_runtime.json") | ConvertFrom-Json).instances
    $targets = @(); $k = 0
    foreach ($i in ($fx | Where-Object { $_.template -like "*Steam*" })) { $m = $i.ue_matrix[3]; $p = UeToGltf $m[0] $m[1] $m[2]; $targets += @{ id = "Steam_$k"; pos = @($p[0], ($p[1] + 1.0), $p[2]); mat = $i.template; note = "$($i.owner) $($i.template)" }; $k++ }
    foreach ($tpl in "Pickup_FX", "HealthPickup_FX", "OvershieldPickup_FX") {
        $i = @($fx | Where-Object { $_.template -like "*.$tpl" })[0]; if (-not $i) { continue }
        $m = $i.ue_matrix[3]; $p = UeToGltf $m[0] $m[1] $m[2]; $targets += @{ id = $tpl; pos = @($p[0], ($p[1] + 0.8), $p[2]); mat = $i.template; note = "$($i.owner) $($i.template) (active state: authored / Gameplay)" }
    }
    EffectFamily "particles" $targets @{ WFC_NOMAPFX = "1" }
}
# ---------- 5/6: glass floors ----------
if ($Families -contains "glass") {
    $glassMats = @("IAC_Glass2_light_MATINST", "IAC_Glass_MATINST", "IAC_Decagon_GlassPan2_MATINST", "IAC_Glass2_dense_Dark_MATINST")
    $gt = @()
    foreach ($m in $glassMats) {
        $seen = @{}
        $inst = @($props | Where-Object { (@($_.section_materials) + @($_.material_overrides | Where-Object { $_ -is [string] })) -match [regex]::Escape($m) } | Sort-Object { $_.gltf_matrix[12] } | Where-Object { $k2 = "{0:F1},{1:F1},{2:F1}" -f $_.gltf_matrix[12], $_.gltf_matrix[13], $_.gltf_matrix[14]; if ($seen[$k2]) { $false } else { $seen[$k2] = $true; $true } })   # one per placement
        $k = 0
        foreach ($p in (Pick $inst 2)) {
            $sp = PropSurfaceSamples $p $m 24
            if ($sp.Count) { $gt += @{ id = ("{0}_{1}" -f ($m -replace '_MATINST$', ''), $k); samples = $sp; pos = (Centroid $sp); mat = $m; note = "$($p.actor) $($p.mesh.Split('.')[-1]) ($($inst.Count) placements)" }; $k++ }
        }
    }
    $shots = New-Object System.Collections.Generic.List[object]
    # player-like cameras: standing on the glass (eye 2.5 m), a few metres off, grazing, and from underneath
    $elev = @(@{ n = "aboveclose"; dh = 2.5; d = 3.0 }, @{ n = "above"; dh = 4.0; d = 7.0 }, @{ n = "grazing"; dh = 0.8; d = 10.0 }, @{ n = "below"; dh = -2.5; d = 4.0 })
    foreach ($t in $gt) { for ($a = 0; $a -lt 4; $a++) { foreach ($e in $elev) { $shots.Add(@{ name = ("{0}_{1}_a{2}" -f $t.id, $e.n, $a); c = (RingCam $t.pos $azs[$a] $e.d $e.dh); t = $t.pos; tid = $t.id; e = $e.n; a = $a }) } } }
    $dir = Join-Path $OutDir "glass"
    Write-Host "[glass] $($gt.Count) props, $($shots.Count) shots x 2 runs"
    $B = Invoke-ShotList $Exe (Join-Path $dir "without") $shots @{ WFC_SKIPMAT = ($glassMats -join ";") } $RenderData
    # The glass's own surface, sampled 24 x 24 and projected: the cells where it should be seen (no occlusion test).
    $cellsOf = @{}; foreach ($s in $shots) { $t = $gt | Where-Object id -eq $s.tid; $cellsOf[$s.name] = ProjectCells $s.c $s.t $t.samples }
    $script:glassRows = @(); $glassDir = Join-Path $dir "with"
    $A = Invoke-ShotList $Exe $glassDir $shots @{} $RenderData -Overlay { param($n, $bmp)
        if (-not $B.grids[$n] -or $cellsOf[$n].Count -lt 40) { return }
        $ga = [WfcImage]::Rgb($bmp, 8)
        $pc = [WfcImage]::CellCoverage($ga, $B.grids[$n], $cellsOf[$n], 2.0)
        $png = Join-Path $glassDir "overlay_$n.png"; [WfcImage]::OverlayCells($bmp, $ga, $B.grids[$n], 8, $cellsOf[$n], 2.0, $png); Save-WfcJpeg $png ($png -replace '\.png$', '.jpg'); Remove-Item $png
        $script:glassRows += [pscustomobject][ordered]@{ shot = $n; surface_cells = $pc[0]; glass_contributes = [Math]::Round($pc[1], 3); holes = [Math]::Round($pc[2], 3) }
    }
    $glassRows2 = $script:glassRows
    foreach ($t in $gt) {
        $mine = @($glassRows2 | Where-Object { $_.shot -like "$($t.id)_*" })
        if (-not $mine.Count) { Add-WfcResult $res "playtest.glass.$($t.id)" "INFO" -1 "glass surface not in front of any camera ($($t.note))"; continue }
        $by = @{}; foreach ($e in $elev) { $g = @($mine | Where-Object { $_.shot -like "*_$($e.n)_a*" }); if ($g.Count) { $by[$e.n] = ($g | Measure-Object glass_contributes -Average).Average } }
        $abv = @("aboveclose", "above" | Where-Object { $by.ContainsKey($_) } | ForEach-Object { $by[$_] }); $abvMean = if ($abv.Count) { ($abv | Measure-Object -Average).Average } else { -1 }
        $blw = if ($by.ContainsKey("below")) { $by["below"] } else { -1 }
        $worst = $mine | Sort-Object holes -Descending | Select-Object -First 1
        $oneSided = $abvMean -ge 0 -and $blw -ge 0 -and [Math]::Abs($abvMean - $blw) -ge 0.5
        $flag = $worst.holes -ge 0.25 -or $oneSided
        Add-WfcResult $res "playtest.glass.$($t.id)" $(if ($flag) { "HUMAN" } else { "INFO" }) $worst.holes ("{0} views. Glass contributes over its own surface (mean): {1}. Worst view {2}: {3:P0} of the surface shows no glass (occluded, drawn over, or the glass not drawn from that side) - overlay_{2}.jpg (red = glass contribution, blue = surface without it).{4} {5}" -f $mine.Count, (($elev | Where-Object { $by.ContainsKey($_.n) } | ForEach-Object { "{0} {1:P0}" -f $_.n, $by[$_.n] }) -join ", "), $worst.shot, $worst.holes, $(if ($oneSided) { " Seen from one side only (above vs below differ by >= 50 points); material two_sided=false." } else { "" }), $t.note)
        $j = Join-Path $glassDir "overlay_$($worst.shot).jpg"; if (Test-Path $j) { Tile "glass" $j ("{0} holes {1:P0}" -f $worst.shot, $worst.holes) $flag }
        $j = Join-Path $glassDir ("overlay_{0}_below_a0.jpg" -f $t.id); if (Test-Path $j) { Tile "glass" $j ("{0} below" -f $t.id) }
    }
    Write-WfcCsv $glassRows2 (Join-Path $dir "glass.csv")
}

# ---------- 7: ramps / stairs ----------
if ($Families -contains "ramps") {
    $nav = Get-Content -Raw "$WfcSlice\navigation.json" | ConvertFrom-Json
    $loc = @{}; foreach ($n in $nav.nodes) { $loc[$n.node] = $n.location_gltf }
    $ramps = @()
    foreach ($e in $nav.edges) {
        $a = $loc[$e.start.Split('.')[-1]]; $b = $loc[$e.end.Split('.')[-1]]; if (-not $a -or -not $b) { continue }
        $h = [Math]::Sqrt([Math]::Pow($b[0] - $a[0], 2) + [Math]::Pow($b[2] - $a[2], 2)); if ($h -lt 6) { continue }
        $s = ($b[1] - $a[1]) / $h
        if ($s -gt 0.12) { $ramps += @{ lo = $a; hi = $b; slope = $s; id = $e.spec } }
    }
    $sel = Pick ($ramps | Sort-Object { $_.lo[0] }) 14
    $shots = New-Object System.Collections.Generic.List[object]
    foreach ($r in $sel) {
        $lo = @($r.lo[0], ($r.lo[1] + 2.5), $r.lo[2]); $hi = @($r.hi[0], ($r.hi[1] + 2.5), $r.hi[2]); $mid = @((($lo[0] + $hi[0]) / 2), (($lo[1] + $hi[1]) / 2), (($lo[2] + $hi[2]) / 2))
        $dx = $hi[0] - $lo[0]; $dz = $hi[2] - $lo[2]; $l = [Math]::Sqrt($dx * $dx + $dz * $dz); $px = -$dz / $l * 10; $pz = $dx / $l * 10
        $shots.Add(@{ name = "$($r.id)_up"; c = $lo; t = $hi }); $shots.Add(@{ name = "$($r.id)_down"; c = $hi; t = $lo })
        $shots.Add(@{ name = "$($r.id)_side"; c = @(($mid[0] + $px), ($mid[1] + 3), ($mid[2] + $pz)); t = $mid })
    }
    $dir = Join-Path $OutDir "ramps"
    Write-Host "[ramps] $($shots.Count) shots"
    $A = Invoke-ShotList $Exe $dir $shots @{} $RenderData -Keep @($shots | ForEach-Object { $_.name })
    $rows = @()
    foreach ($s in $shots) {
        if (-not $A.grids[$s.name]) { continue }
        $bf = [WfcImage]::BlackFraction($A.grids[$s.name], 3.0)
        $rows += [pscustomobject][ordered]@{ shot = $s.name; black_cells = [Math]::Round($bf, 3) }
    }
    foreach ($r in ($rows | Sort-Object black_cells -Descending | Select-Object -First 8)) {
        Add-WfcResult $res "playtest.ramps.$($r.shot)" $(if ($r.black_cells -ge 0.15) { "HUMAN" } else { "INFO" }) $r.black_cells ("near-black (luma < 3) fraction of the view along/across an authored ramp (navigation edge slope > 0.12); >= 15% flagged - $($r.shot).jpg")
        Tile "ramps" (Join-Path $dir "$($r.shot).jpg") ("{0} black {1:P0}" -f $r.shot, $r.black_cells) ($r.black_cells -ge 0.15)
    }
    Add-WfcResult $res "playtest.ramps.views" "INFO" $rows.Count ("{0} ramp views over {1} authored ramp edges (of {2} with slope > 0.12)" -f $rows.Count, $sel.Count, $ramps.Count)
    Write-WfcCsv $rows (Join-Path $dir "ramps.csv")
}
foreach ($g in $tiles.Keys) { New-WfcSheet $tiles[$g] (Join-Path $OutDir "sheet_$g.png") 4 480 270; Save-WfcJpeg (Join-Path $OutDir "sheet_$g.png") (Join-Path $OutDir "sheet_$g.jpg"); Remove-Item (Join-Path $OutDir "sheet_$g.png") }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"PLAYTEST REGRESSIONS: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ") + " -> $OutDir"
