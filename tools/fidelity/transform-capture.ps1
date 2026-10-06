# Transformation visual analyzer on the REAL exe (deterministic lockstep build).
#
#   .\tools\fidelity\transform-capture.ps1                    # all cases -> work\fidelity\transform\
#   .\tools\fidelity\transform-capture.ps1 -Cases r2v_side
#
# Runs wfc_rebuild_observe (unmodified product sources + lockstep clock + frame grabber + audio spy,
# see measure/): exactly one 60 Hz step per frame, so frame N is always the same moment. For each
# case it presses Transform at frame 60, grabs every -Stride-th frame through the fold and records
# per frame: elapsed, played clip + time, form, collision form, weapon usable, root position,
# camera position (listener pose), debug-overlay state, and image continuity against the previous
# grab. Flags:
#   pose/pixel freeze   no visible change between grabs while transforming (fixed camera)
#   visual pop          one grab-to-grab change > 3x the fold median (sudden appearance)
#   mesh switch         form changes (the rebuild's single-mesh handoff) vs authored windows
#   debug geometry      DebugFlags overlay / WFC_DEBUGCAM beacon drawn (exact, from the exe)
#   camera pop          camera moves > 1 m relative to the pawn root in one frame
# Authored overlap windows [CONFIRMED]: R->V 0.396-0.880 s, V->R 0.098-0.663 s.
# Output per case: frames.csv, sheet.png (labelled; orange label = flagged), report.json; plus a
# combined report.json for the merge gate. The harness suite transform_analyzer covers the
# simulation-side fields (normalized clip times, weapon visible, exact pose deltas).
param([string[]]$Cases = @("r2v_side", "v2r_side", "r2v_chase_moving", "v2r_chase_moving"),
      [string]$Exe = "", [string]$OutDir = "", [int]$Stride = 2, [int]$SheetEvery = 3)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_observe.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\transform" }
if (-not (Test-Path $Exe)) { throw "measurement build missing: $Exe (cmake -DWFC_BUILD_MEASURE=ON; target wfc_rebuild_observe)" }
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue

# Side camera on the authored FFA spawn (World::respawnPlayer), same framing as stills.ps1.
$spawn = @(363.5, -724.48, -341.8); $yaw = 1.01
$fwd = @(-[Math]::Sin($yaw), 0, -[Math]::Cos($yaw)); $right = @(-$fwd[2], 0, $fwd[0])
function Cam([double[]]$o, [double]$lookUp) {
    $c = @(($spawn[0] + $fwd[0] * $o[0] + $right[0] * $o[1]), ($spawn[1] + $o[2]), ($spawn[2] + $fwd[2] * $o[0] + $right[2] * $o[1]))
    $d = @(($spawn[0] - $c[0]), ($spawn[1] + $lookUp - $c[1]), ($spawn[2] - $c[2]))
    $len = [Math]::Sqrt($d[0] * $d[0] + $d[1] * $d[1] + $d[2] * $d[2])
    "{0:F3},{1:F3},{2:F3},{3:F4},{4:F4}" -f $c[0], $c[1], $c[2], [Math]::Atan2(-$d[0], -$d[2]), [Math]::Asin($d[1] / $len)
}
$side = Cam @(0, 12, 3) 2.0
$press = 60
$defs = [ordered]@{
    r2v_side         = @{ r2v = $true;  frames = 230; env = @{ WFC_RENDERCAM = $side } }
    v2r_side         = @{ r2v = $false; frames = 180; env = @{ WFC_RENDERCAM = $side; WFC_STARTVEHICLE = "1" } }
    r2v_chase_moving = @{ r2v = $true;  frames = 230; env = @{ WFC_AUTOWALK = "1" } }
    v2r_chase_moving = @{ r2v = $false; frames = 180; env = @{ WFC_AUTOWALK = "1"; WFC_STARTVEHICLE = "1" } }
}
$all = New-WfcResults
foreach ($name in $Cases) {
    $d = $defs[$name]; $dir = Join-Path $OutDir $name
    if (Test-Path $dir) { Get-ChildItem $dir -File | Remove-Item }
    New-Item -ItemType Directory -Force $dir | Out-Null
    # Native notify times (M03 native RE: R->V 0.3958 / 0.8796, V->R 0.0984 / 0.6634 s).
    $w0 = if ($d.r2v) { 0.3958 } else { 0.0984 }; $w1 = if ($d.r2v) { 0.8796 } else { 0.6634 }
    $envs = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOGEVERY = "1"; WFC_PRESSTRANSFORM = "$press"; WFC_NOMOUSE = "1"
               WFC_GRAB = "$($press - 4):$($d.frames - 1):$Stride"; WFC_GRAB_DIR = $dir; WFC_AUDIOSPY = (Join-Path $dir "audiospy.txt")
               WFC_DEBUGSTATE = (Join-Path $dir "debugstate.txt") } + $d.env
    $rc = Invoke-WfcExe $Exe $dir $envs
    # ---- per-frame data ----
    $log = @{}; foreach ($f in (Read-WfcFrames (Join-Path $dir "wfc.log"))) { $log[[int]$f.frame] = $f }
    $cam = @{}; $lastH = $null
    foreach ($ln in [IO.File]::ReadLines((Join-Path $dir "audiospy.txt"))) {
        if ($ln.StartsWith("H ")) { $lastH = $ln.Split(" ") }
        elseif ($ln.StartsWith("F ") -and $lastH) { $cam[[int]$ln.Split(" ")[2]] = @([double]$lastH[2], [double]$lastH[3], [double]$lastH[4]) }
    }
    $dbg = @{}; foreach ($ln in [IO.File]::ReadLines((Join-Path $dir "debugstate.txt"))) { $q = $ln.Split(" "); $dbg[[int]$q[0]] = ([int]$q[1] -or [int]$q[2]) }
    $rows = New-Object System.Collections.Generic.List[object]
    $prevLuma = $null; $prev = $null
    foreach ($bmp in (Get-ChildItem $dir -Filter "f*.bmp" | Sort-Object Name)) {
        $fr = [int]$bmp.BaseName.Substring(1)
        $L = $log[$fr]; if (-not $L) { continue }
        $luma = [WfcImage]::Luma($bmp.FullName, 8)
        $diff = if ($prevLuma) { [WfcImage]::Diff($prevLuma, $luma, 12.0) } else { @(0, 0) }
        $c = $cam[$fr]
        $root = @($L.x, $L.y, $L.z)
        $camRel = if ($c) { @(($c[0] - $root[0]), ($c[1] - $root[1]), ($c[2] - $root[2])) } else { $null }
        $camJump = 0
        if ($prev -and $camRel -and $prev.camRel) {
            $camJump = [Math]::Sqrt([Math]::Pow($camRel[0] - $prev.camRel[0], 2) + [Math]::Pow($camRel[1] - $prev.camRel[1], 2) + [Math]::Pow($camRel[2] - $prev.camRel[2], 2)) / $Stride
        }
        $e = ($fr - $press + 1) / 60.0   # frame 60 runs the press step = 1 step of fold (harness convention)
        $rows.Add([pscustomobject][ordered]@{
            frame = $fr; elapsed = [Math]::Round($e, 4); form = $L.form; collision_form = $L.moveForm; clip = $L.anim; clip_t = $L.t
            weapon_usable = $L.wpn; root_x = $L.x; root_y = $L.y; root_z = $L.z; yaw = $L.yaw; hspeed = $L.hspeed
            cam_x = $(if ($c) { $c[0] } else { "" }); cam_y = $(if ($c) { $c[1] } else { "" }); cam_z = $(if ($c) { $c[2] } else { "" })
            debug_overlay = [int][bool]$dbg[$fr]; img_diff = [Math]::Round($diff[0], 3); img_changed = [Math]::Round($diff[1], 4)
            cam_jump_per_frame = [Math]::Round($camJump, 4); cam_dist = $L.camD; flags = ""; png = ""; camRel = $camRel })
        $prevLuma = $luma; $prev = $rows[$rows.Count - 1]
    }
    # ---- flags ----
    $fold = @($rows | Where-Object { $_.elapsed -gt 0 -and $_.clip -like "Transform_*" })
    $med = if ($fold.Count) { ($fold.img_diff | Sort-Object)[[int]($fold.Count / 2)] } else { 0 }
    $switch = $null; $pops = @(); $freeze = 0; $dbgFrames = 0; $camPops = 0; $colPops = 0; $prevD = $null
    for ($i = 0; $i -lt $rows.Count; $i++) {
        $r = $rows[$i]; $fl = @()
        if ($i -gt 0 -and $r.form -ne $rows[$i - 1].form -and -not $switch) { $switch = $r; $fl += "mesh_switch" }
        if ($r.clip -like "Transform_*" -and $r.elapsed -gt 0) {
            if ($r.img_diff -gt [Math]::Max(3 * $med, 4.0)) { $pops += $r; $fl += "visual_pop" }
            if ($name -like "*_side" -and $r.img_diff -lt 0.05) { $freeze++; $fl += "no_visible_change" }
        }
        if ($r.debug_overlay) { $dbgFrames++; $fl += "debug_geometry" }
        # A jump the product's own collided orbit distance (camD = |cameraPos - actor|) accounts for is the
        # third-person camera-collision pull-in / release (PlayerController::cameraPos segmentHit, [PROV]):
        # it also happens without any transformation (camera-trace.ps1 controls) - KNOWN, not a pop.
        if ($r.cam_jump_per_frame -gt 1.0) {
            $dd = if ($null -ne $prevD -and "$($r.cam_dist)" -ne "") { [Math]::Abs([double]$r.cam_dist - [double]$prevD) } else { 0 }
            if ($dd -gt 0.8 * $r.cam_jump_per_frame) { $colPops++; $fl += "camera_collision_pull" } else { $camPops++; $fl += "camera_pop" }
        }
        if ("$($r.cam_dist)" -ne "") { $prevD = $r.cam_dist }
        if ($r.weapon_usable -eq 1 -and $r.form -ne "ROBOT") { $fl += "usable_while_vehicle_mesh" }
        $r.flags = $fl -join ";"
    }
    # ---- contact sheet (every SheetEvery-th grab + all flagged) ----
    $tiles = @()
    for ($i = 0; $i -lt $rows.Count; $i++) {
        $r = $rows[$i]
        $bmp = Join-Path $dir ("f{0:D5}.bmp" -f $r.frame)
        if (($i % $SheetEvery) -eq 0 -or $r.flags) {
            $r.png = Convert-WfcBmp $bmp
            $tiles += @{ png = $r.png; label = ("f{0} e={1:F2}s {2} {3} t={4:F2} {5}" -f $r.frame, $r.elapsed, $r.form, $r.clip, $r.clip_t, $r.flags); flag = [bool]$r.flags }
        } elseif (Test-Path $bmp) { Remove-Item -LiteralPath $bmp }
    }
    New-WfcSheet $tiles (Join-Path $dir "sheet.png") 5 384 216
    Write-WfcCsv ($rows | Select-Object * -ExcludeProperty camRel) (Join-Path $dir "frames.csv")
    # ---- results ----
    $id = "transform_capture.$name"
    Add-WfcResult $all "$id.ran" $(if ($rc -eq 0 -and $rows.Count -gt 10) { "PASS" } else { "FAIL" }) $rows.Count "grabbed frames (exit $rc)"
    Add-WfcResult $all "$id.no_debug_geometry" $(if ($dbgFrames -eq 0) { "PASS" } else { "FAIL" }) $dbgFrames "frames drawn with the debug overlay / beacon (exact DebugFlags record)"
    Add-WfcResult $all "$id.no_camera_pop" $(if ($camPops -eq 0) { "PASS" } else { "FAIL" }) $camPops "frames where the camera jumps > 1 m relative to the pawn root in one frame, NOT explained by camera collision"
    Add-WfcResult $all "$id.camera_collision_snaps" $(if ($colPops -eq 0) { "PASS" } else { "KNOWN" }) $colPops "frames where the collided orbit distance snaps > 1 m in one frame (camera-collision pull-in/release; unsmoothed in the rebuild, original TnThirdPersoncollisionCameraBehavior / TnAvoidClippingCameraBehavior response not recovered - [PROV]). Location-dependent; also occurs without a transform (camera-trace controls)" "Gameplay"
    if ($switch) {
        # Grabs are every -Stride frames: the switch is only located to within Stride/60 s (+1.5 steps as in
        # transform_analyzer). The frame-exact check is the harness's transform_analyzer / transform_timeline.
        $tol = ($Stride + 1.5) / 60.0
        $st = if ($switch.elapsed -ge $w0 - $tol -and $switch.elapsed -le $w1 + $tol) { "INFO" } else { "KNOWN" }
        Add-WfcResult $all "$id.mesh_switch_time" $st $switch.elapsed ("logical form switch at {0:F3} s (native overlap window {1}-{2} s, sampling tolerance {3:F3} s; mesh overlap itself is judged frame-exactly by transform_timeline.*.both_meshes_visible_in_fold)" -f $switch.elapsed, $w0, $w1, $tol) "Gameplay" $null "s"
        $sw = $rows | Where-Object frame -eq $switch.frame
        Add-WfcResult $all "$id.switch_visual_change" "INFO" $sw.img_diff ("image change at the switch frame vs fold median {0:F2} (ratio {1:F1}x)" -f $med, $(if ($med -gt 0) { $sw.img_diff / $med } else { 0 }))
    }
    if ($name -like "*_side") {
        Add-WfcResult $all "$id.visual_pops" "INFO" $pops.Count ("grabs with a change > 3x the fold median: " + (($pops | ForEach-Object { "f$($_.frame)@$($_.elapsed)s" }) -join ", "))
        Add-WfcResult $all "$id.frozen_grabs" "INFO" $freeze "fold grabs with no visible change vs the previous grab (fixed camera; authored holds included)"
    }
    "{0}: {1} grabs, switch {2}, pops {3}, debug {4}, campops {5}" -f $name, $rows.Count, $(if ($switch) { "$($switch.elapsed)s" } else { "none" }), $pops.Count, $dbgFrames, $camPops
}
$sum = Write-WfcReport $all (Join-Path $OutDir "report.json")
"TRANSFORM CAPTURE: {0} pass, {1} FAIL, {2} known, {3} info" -f $sum.pass, $sum.fail, $sum.known, $sum.info
