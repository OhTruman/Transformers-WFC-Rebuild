# Milestone 04 deterministic captures of MP_IAC_Streets (observe exe: lockstep 60 Hz, frame grabber).
#
#   .\tools\fidelity\m04-captures.ps1 -Exe <wfc_rebuild_observe.exe> -RenderData <work\render> -OutDir <dir> [-Baseline <dir>] [-Only a,b]
#
# TARGET shots (authored objects): a fixed world camera (WFC_RENDERCAM) aimed at the authored position.
#   * Visibility mask: the same locked frame rendered with the target removed (product diagnostic
#     WFC_SKIPMAT "comp:<component>"); cells that change are the target's visible pixels (coverage).
#   * View search: a ring of cameras (12 m, then 30 m); the camera with the most coverage is kept.
#   * Motion: mean luma change of the target's pixels between t = 2 s and t = 5 s (lockstep). A 15 deg/s
#     dome turns 45 deg in 3 s; the SkyBeam Matinee track moves ~14 deg. Frozen geometry gives ~0.
#   * Authored-hidden targets are also rendered with WFC_SHOWHIDDEN: coverage there but not in normal play
#     proves the object is in view AND hidden.
# SPAWN shots: the player camera at authored starts (interior / dark / bright / others), an exterior
#   overview, and a vehicle traversal route (boost from the open-run start) sampled every second.
# Per image: mean luma, near-black and flat (untextured) cell fractions - presentation INFO, never truth.
# Output: captures.csv, report.json (fidelity schema), sheet_<group>.png; with -Baseline, every frame is
# compared with the baseline's image of the same name (CHANGED list for the gate).
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [string]$Baseline = "", [string[]]$Only = @(), [hashtable]$Env = @{})
$ErrorActionPreference = "Stop"
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes "a,b" as one string
. (Join-Path $PSScriptRoot "lib\Run.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$Exe = (Resolve-Path $Exe).Path
$slice = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets"
function UE2G($x, $y, $z) { return , @(([double]$x / 100), ([double]$z / 100), ([double]$y / 100)) }   # glTF = (X, Z, Y) / 100

# ---- authored targets (glTF metres) ----
# skip = WFC_SKIPMAT spec removing exactly the target; hidden = authored bHidden (control under WFC_SHOWHIDDEN).
$g = Get-Content -Raw "$slice\gameplay.json" | ConvertFrom-Json
$fx = Get-Content -Raw "$slice\map_fx.json" | ConvertFrom-Json
$au = Get-Content -Raw "$slice\audio.json" | ConvertFrom-Json
$targets = [ordered]@{}
$targets["mover_domes"] = @{ pos = (UE2G 12027 -49089 -69880); kind = "mover"; skip = "comp:StaticInterpActor_15810;comp:StaticInterpActor_7381;comp:StaticInterpActor_8114"; note = "3 PHYS_Rotating DecoSphereHalf01 domes, RotationRate Yaw 2730 (15 deg/s)" }
$targets["mover_skybeam_deco"] = @{ pos = (UE2G 9011.13 -42093.5 -66667.3); kind = "mover"; skip = "comp:StaticInterpActor_5249"; note = "SkyBeam Matinee SeqAct_Interp_3464 (looping 9 s): StaticInterpActor_5249 DecoSphereHalf01" }
$targets["mover_skybeam_cone"] = @{ pos = (UE2G -13760.87 -28160.5 -65168.3); kind = "mover"; skip = "comp:StaticInterpActor_10471"; note = "SkyBeam Matinee: StaticInterpActor_10471 LightBeam_Cone" }
$ob = @($g.mode_dependent_visibility | ForEach-Object { $_.targets } | Where-Object { $_.mesh -like "*ObjBases*" })
$i = 0
foreach ($o in ($ob | Sort-Object { $_.actor } -Unique)) {
    $i++; $targets["objective_base_$i"] = @{ pos = @($o.location_gltf); kind = "objective"; hidden = $true; skip = "comp:$($o.actor)"; note = "$($o.actor) $($o.mesh) authored bHidden (unhidden only in CTF/EXT)" }
}
$dp = @($g.objectives.TnDominationPoint)[0]
$targets["domination_totem"] = @{ pos = @($dp.location_gltf); kind = "totem"; skip = "comp:TnDominationPoint"; note = "$($dp.actor) energon totem (animated SkeletalMesh)" }
$em = @($fx.particle_components | Where-Object { $_.owner_class -eq "Emitter" })[0]
$targets["fx_steam"] = @{ pos = (UE2G $em.owner_props.Location.X $em.owner_props.Location.Y $em.owner_props.Location.Z); kind = "fx"; note = "$($em.owner.Split('.')[-1]) $($em.props.Template)" }
$pa = @($g.pickups | Where-Object { $_.class -eq "TnAmmoCratePickupFactory" })[0]; $ph = @($g.pickups | Where-Object { $_.class -eq "TnHealthPickupFactory" })[0]
$targets["pickup_ammo"] = @{ pos = @($pa.location_gltf); kind = "pickup"; cam = @{ d = 6.0; h = 3.0 }; note = "$($pa.actor): available crate mesh rotates (PickupRotationRate Yaw 10000 = 55 deg/s) + highlight beam FX" }
$targets["pickup_health"] = @{ pos = @($ph.location_gltf); kind = "pickup"; cam = @{ d = 6.0; h = 3.0 }; note = "$($ph.actor): no mesh; available state shown only by CustomPickupEffect HealthPickup_FX" }
$targets["destructible"] = @{ pos = (UE2G 896 89968 -352); kind = "destructible"; skip = "comp:TnStaticDestructibleActor"; note = "TnStaticDestructibleActor_14465 WallPanelSign (authored outside the play space)" }
$z = @($au.zones)[0]
$targets["zone_trigger"] = @{ pos = (UE2G $z.trigger_volume_location_ue[0] $z.trigger_volume_location_ue[1] $z.trigger_volume_location_ue[2]); kind = "zone"; note = "$($z.comment) ($($z.trigger_volume))" }
$targets["exterior_overview"] = @{ pos = @(230.0, -715.0, -450.0); kind = "overview"; note = "play-space centre from above"; cam = @{ d = 120.0; h = 70.0 } }

$spawnShots = [ordered]@{
    spawn_interior = @{ env = @{ WFC_SPAWN_INDEX = "0" }; note = "FFA start 0 (zone ENTER_DEC_ROOM_LOWER)" }
    spawn_dark     = @{ env = @{ WFC_SPAWN_INDEX = "3" }; note = "darkest probed FFA start (vehicle-visual)" }
    spawn_bright   = @{ env = @{ WFC_SPAWN_INDEX = "18" }; note = "brightest probed FFA start / open run" }
    spawn_9        = @{ env = @{ WFC_SPAWN_INDEX = "9" }; note = "FFA start 9" }
    spawn_14       = @{ env = @{ WFC_SPAWN_INDEX = "14" }; note = "FFA start 14" }
    vehicle_route  = @{ env = @{ WFC_SPAWN_INDEX = "18"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.3" }; note = "boost route from start 18, frame every 1 s"; grab = "60:600:60"; frames = 602 }
}

$res = New-WfcResults
$rows = New-Object System.Collections.Generic.List[object]
$tiles = @{}
function Grab($name, $envs, $grab, $frames) {
    $dir = Join-Path $OutDir $name
    if (Test-Path $dir) { Get-ChildItem $dir -File | Remove-Item }
    New-Item -ItemType Directory -Force $dir | Out-Null
    $e = @{ WFC_SMOKE_FRAMES = "$frames"; WFC_GRAB = $grab; WFC_GRAB_DIR = $dir; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0" } + $Env + $envs
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $e "run.log" 900
    $pngs = @(Get-ChildItem $dir -Filter "f*.bmp" | Sort-Object Name | ForEach-Object { Convert-WfcBmp $_.FullName })
    return @{ rc = $rc; pngs = $pngs; dir = $dir }
}
function Group-Of($n) { if ($n -like "mover*") { "movers" } elseif ($n -like "spawn*" -or $n -like "vehicle*" -or $n -like "exterior*") { "spaces" } else { "objects" } }
function Add-Tile($n, $png, $label, $flag = $false) { $gr = Group-Of $n; if (-not $tiles[$gr]) { $tiles[$gr] = @() }; $script:tiles[$gr] += @{ png = $png; label = $label; flag = $flag } }
# Camera on a ring around p (azimuth v of n, distance d, height h), aimed at p: "x,y,z,yaw,pitch".
function CamAt($p, $v, $n, $d, $h) {
    $az = 2 * [Math]::PI * $v / $n
    $c = @(($p[0] + $d * [Math]::Sin($az)), ($p[1] + $h), ($p[2] + $d * [Math]::Cos($az)))
    $dv = @(($p[0] - $c[0]), ($p[1] - $c[1]), ($p[2] - $c[2]))
    $yaw = [Math]::Atan2(-$dv[0], -$dv[2]); $pitch = [Math]::Atan2($dv[1], [Math]::Sqrt($dv[0] * $dv[0] + $dv[2] * $dv[2]))
    return "{0:F3},{1:F3},{2:F3},{3:F4},{4:F4}" -f $c[0], $c[1], $c[2], $yaw, $pitch
}
# Target pixel mask from (with, skip) renders of the same locked frame: returns (lumaWith, lumaSkip, coverage fraction).
function Coverage($withPng, $skipPng) { $a = [WfcImage]::Luma($withPng, 8); $b = [WfcImage]::Luma($skipPng, 8); return , @($a, $b, [WfcImage]::Diff($a, $b, 4.0)[1]) }
# Mean |t5 - t2| over the mask cells (|with - skip| > 4): (motion, cells).
function MaskedMotion($a2, $a5, $mA, $mB) {
    $s = 0.0; $n = 0
    for ($k = 2; $k -lt [Math]::Min($a2.Length, $mA.Length); $k++) { if ([Math]::Abs($mA[$k] - $mB[$k]) -gt 4.0) { $s += [Math]::Abs($a5[$k] - $a2[$k]); $n++ } }
    return , @($(if ($n) { $s / $n } else { 0.0 }), $n)
}
$rings = @(@{ d = 12.0; h = 3.0; n = 6 }, @{ d = 30.0; h = 10.0; n = 6 })

foreach ($tn in $targets.Keys) {
    if ($Only.Count -and $Only -notcontains $tn) { continue }
    $t = $targets[$tn]; $p = $t.pos
    $hidEnv = if ($t.hidden) { @{ WFC_SHOWHIDDEN = "1" } } else { @{} }
    # ---- 1. view search (coverage with the target shown) ----
    $best = $null
    if ($t.skip) {
        foreach ($ring in $rings) {
            for ($v = 0; $v -lt $ring.n; $v++) {
                $cam = CamAt $p $v $ring.n $ring.d $ring.h
                $nm = "_scan_{0}" -f $tn
                $a = Grab $nm (@{ WFC_RENDERCAM = $cam } + $hidEnv) "120:120:1" 122
                $b = Grab "${nm}_skip" (@{ WFC_RENDERCAM = $cam; WFC_SKIPMAT = $t.skip } + $hidEnv) "120:120:1" 122
                if ($a.pngs.Count -and $b.pngs.Count) {
                    $cv = Coverage $a.pngs[0] $b.pngs[0]
                    $blk = [WfcImage]::Stats($cv[0], 6.0)[1]; $ok = $cv[2] -ge 0.005 -and $cv[2] -le 0.6 -and $blk -lt 0.7   # target in view, not filling it, not inside geometry
                    $score = if ($ok) { $cv[2] } else { $cv[2] * 0.001 }
                    if (-not $best -or $score -gt $best.score) { $best = @{ cam = $cam; cov = $cv[2]; score = $score; ok = $ok } }
                }
                Remove-Item -Recurse -Force $a.dir, $b.dir
            }
            if ($best -and $best.ok -and $best.cov -ge 0.01) { break }
        }
    }
    $cam = if ($best -and $best.cov -gt 0) { $best.cam } elseif ($t.cam) { CamAt $p 0 6 $t.cam.d $t.cam.h } else { CamAt $p 0 6 $rings[0].d $rings[0].h }
    # ---- 2. measurement at the chosen camera ----
    $r = Grab $tn @{ WFC_RENDERCAM = $cam } "120:300:180" 302
    if ($r.pngs.Count -lt 2) { Add-WfcResult $res "m04_capture.$tn.ran" "SKIP" $r.pngs.Count "grabs missing (exit $($r.rc))"; continue }
    $a2 = [WfcImage]::Luma($r.pngs[0], 8); $a5 = [WfcImage]::Luma($r.pngs[1], 8)
    $cov = -1.0; $motion = -1.0; $cells = 0; $covShown = -1.0; $fpChange = -1.0
    if ($t.skip) {
        $sk = Grab "${tn}_skip" @{ WFC_RENDERCAM = $cam; WFC_SKIPMAT = $t.skip } "120:300:180" 302
        $cv = Coverage $r.pngs[0] $sk.pngs[0]; $cov = $cv[2]
        $mm = MaskedMotion $a2 $a5 $cv[0] $cv[1]; $motion = $mm[0]; $cells = $mm[1]
        # Geometric motion: the target's footprint (|with - skip| > 4) at t=2 s vs t=5 s. An animated material
        # changes luma inside a fixed footprint (IoU ~1); a moving object shifts the footprint (IoU < 1).
        if ($sk.pngs.Count -ge 2) {
            $s2 = [WfcImage]::Luma($sk.pngs[0], 8); $s5 = [WfcImage]::Luma($sk.pngs[1], 8); $inter = 0; $union = 0
            for ($k = 2; $k -lt [Math]::Min($a2.Length, $s5.Length); $k++) {
                $m2 = [Math]::Abs($a2[$k] - $s2[$k]) -gt 4.0; $m5 = [Math]::Abs($a5[$k] - $s5[$k]) -gt 4.0
                if ($m2 -and $m5) { $inter++ }; if ($m2 -or $m5) { $union++ }
            }
            $fpChange = if ($union) { 1.0 - $inter / $union } else { 0.0 }
        }
        if ($t.hidden) {
            $sh = Grab "${tn}_shown" @{ WFC_RENDERCAM = $cam; WFC_SHOWHIDDEN = "1" } "120:120:1" 122
            $shs = Grab "${tn}_shown_skip" @{ WFC_RENDERCAM = $cam; WFC_SHOWHIDDEN = "1"; WFC_SKIPMAT = $t.skip } "120:120:1" 122
            $covShown = (Coverage $sh.pngs[0] $shs.pngs[0])[2]
            Add-Tile $tn $sh.pngs[0] "$tn WFC_SHOWHIDDEN control: coverage $([Math]::Round($covShown,3))"
        }
    } else {
        $motion = [WfcImage]::RegionDiff($a2, $a5, 0.35, 0.3, 0.65, 0.7, 6.0)[0]
    }
    $st5 = [WfcImage]::Stats($a5, 6.0)
    $rows.Add([pscustomobject][ordered]@{ capture = $tn; kind = $t.kind; camera = $cam; coverage = [Math]::Round($cov, 4); coverage_shown = [Math]::Round($covShown, 4)
                                          motion = [Math]::Round($motion, 3); footprint_change = [Math]::Round($fpChange, 3); mask_cells = $cells; mean_luma = [Math]::Round($st5[0], 1); black = [Math]::Round($st5[1], 3); flat = [Math]::Round($st5[2], 3); note = $t.note })
    Add-Tile $tn $r.pngs[0] "$tn t=2s coverage $([Math]::Round($cov,3))"
    Add-Tile $tn $r.pngs[1] "$tn t=5s luma change $([Math]::Round($motion,2)), footprint change $([Math]::Round($fpChange,3))" ($fpChange -gt 0.1)
    Add-WfcResult $res "m04_capture.$tn.coverage" "INFO" $cov ("target fraction of the frame (skip mask) at camera {0}{1}; {2}" -f $cam, $(if ($t.hidden) { "; with WFC_SHOWHIDDEN $([Math]::Round($covShown,4))" } else { "" }), $t.note)
    Add-WfcResult $res "m04_capture.$tn.motion" "INFO" $motion ("mean luma change of the target's pixels t=2 s -> 5 s ({0} cells; centre crop when no mask)" -f $cells)
    if ($t.skip) { Add-WfcResult $res "m04_capture.$tn.footprint_change" "INFO" $fpChange "1 - IoU of the target's pixel footprint at t=2 s and t=5 s (geometric motion; ~0 = static, animated materials do not move it)" }
    if ($t.hidden) { Add-WfcResult $res "m04_capture.$tn.coverage_shown" "INFO" $covShown "coverage of the same camera with WFC_SHOWHIDDEN (in-view control)" }
}
foreach ($sn in $spawnShots.Keys) {
    if ($Only.Count -and $Only -notcontains $sn) { continue }
    $s = $spawnShots[$sn]
    $grab = if ($s.grab) { $s.grab } else { "120:300:180" }; $fr = if ($s.frames) { $s.frames } else { 302 }
    $r = Grab $sn $s.env $grab $fr
    $k = 0
    foreach ($png in $r.pngs) {
        $st = [WfcImage]::Stats([WfcImage]::Luma($png, 8), 6.0)
        $rows.Add([pscustomobject][ordered]@{ capture = "$sn#$k"; kind = "space"; camera = "player"; coverage = ""; coverage_shown = ""; motion = ""; mask_cells = ""
                                              mean_luma = [Math]::Round($st[0], 1); black = [Math]::Round($st[1], 3); flat = [Math]::Round($st[2], 3); note = $s.note })
        Add-Tile $sn $png ("{0} #{1} luma {2:F0} black {3:F2} flat {4:F2}" -f $sn, $k, $st[0], $st[1], $st[2])
        $k++
    }
    Add-WfcResult $res "m04_capture.$sn.frames" $(if ($r.pngs.Count) { "INFO" } else { "SKIP" }) $r.pngs.Count ("{0}; exit {1}" -f $s.note, $r.rc)
}
Write-WfcCsv $rows (Join-Path $OutDir "captures.csv")
foreach ($gr in $tiles.Keys) { New-WfcSheet $tiles[$gr] (Join-Path $OutDir "sheet_$gr.png") 4 480 270 }
# ---- comparison with a previous run (same capture names) ----
if ($Baseline -and (Test-Path $Baseline)) {
    foreach ($png in Get-ChildItem $OutDir -Recurse -Filter "f*.png" | Where-Object { $_.Directory.Name -notlike "*_skip" }) {
        $rel = $png.FullName.Substring($OutDir.Length + 1)
        $old = Join-Path $Baseline $rel
        if (-not (Test-Path $old)) { continue }
        $d = [WfcImage]::Diff([WfcImage]::Luma($old, 8), [WfcImage]::Luma($png.FullName, 8), 16.0)
        Add-WfcResult $res ("m04_capture.vs_baseline." + ($rel -replace '[\\/]', '.')) "INFO" ([Math]::Round($d[0], 2)) ("mean luma change vs baseline {0:F1}; {1:P0} of cells changed{2}" -f $d[0], $d[1], $(if ($d[0] -gt 3) { " - CHANGED, review" } else { "" }))
    }
}
$null = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M04 CAPTURES: {0} rows -> {1}" -f $rows.Count, $OutDir
