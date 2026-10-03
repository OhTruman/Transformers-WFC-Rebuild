# Frame-by-frame camera trace through a transformation on the deterministic exe (wfc_rebuild_observe).
#
#   .\tools\fidelity\camera-trace.ps1 -Exe <observe.exe> -OutDir <dir> [-Moving] [-ToVehicle] [-From 55 -To 140]
#
# Per frame (lockstep, one 60 Hz step each, NO scripted camera input: WFC_NOMOUSE):
#   camera position + forward (audio listener pose = camera), pawn position, actor-relative camera offset,
#   form / collision form, both-meshes flag, arm, camera strategy (camS), view yaw, orbit distance/height
#   (camD/camH) from the exe's frame log, and a grabbed frame for a contact sheet.
# Flags: a JUMP = the camera's offset from the pawn changes by > -JumpM in one frame, or the camera
# moves > -JumpM in one frame while the pawn moved < 0.5 m. Output: trace.csv, sheet.png, summary.txt.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [switch]$Moving, [switch]$ToRobot,
      [int]$Press = 60, [int]$From = 56, [int]$To = 140, [double]$JumpM = 1.0, [string]$RenderData = "", [hashtable]$Env = @{})
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
if (Test-Path $OutDir) { Get-ChildItem $OutDir -File | Remove-Item }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path   # the exe runs with cwd = OutDir: every path passed to it must be absolute
$envs = @{ WFC_SMOKE_FRAMES = "$($To + 2)"; WFC_LOGEVERY = "1"; WFC_NOMOUSE = "1"; WFC_PRESSTRANSFORM = "$Press"
           WFC_GRAB = "${From}:${To}:1"; WFC_GRAB_DIR = $OutDir; WFC_AUDIOSPY = (Join-Path $OutDir "audiospy.txt") }
if ($Moving) { $envs.WFC_AUTOWALK = "1" }
if ($ToRobot) { $envs.WFC_STARTVEHICLE = "1" }
if ($RenderData) { $envs.WFC_RENDER_DATA = $RenderData }
foreach ($k in $Env.Keys) { $envs[$k] = $Env[$k] }
$rc = Invoke-WfcExe $Exe $OutDir $envs
$log = @{}; foreach ($f in (Read-WfcFrames (Join-Path $OutDir "wfc.log"))) { $log[[int]$f.frame] = $f }
$cam = @{}; $lastH = $null
foreach ($ln in [IO.File]::ReadLines((Join-Path $OutDir "audiospy.txt"))) {
    if ($ln.StartsWith("H ")) { $lastH = $ln.Split(" ") }
    elseif ($ln.StartsWith("F ") -and $lastH) { $cam[[int]$ln.Split(" ")[2]] = $lastH }
}
$rows = New-Object System.Collections.Generic.List[object]
$prev = $null
for ($fr = $From; $fr -le $To; $fr++) {
    $L = $log[$fr]; $H = $cam[$fr]
    if (-not $L -or -not $H) { continue }
    $c = @([double]$H[2], [double]$H[3], [double]$H[4]); $fw = @([double]$H[5], [double]$H[6], [double]$H[7])
    $rel = @(($c[0] - $L.x), ($c[1] - $L.y), ($c[2] - $L.z))
    $relJump = 0; $camStep = 0; $pawnStep = 0; $yawStep = 0
    if ($prev) {
        $relJump = [Math]::Sqrt([Math]::Pow($rel[0] - $prev.rx, 2) + [Math]::Pow($rel[1] - $prev.ry, 2) + [Math]::Pow($rel[2] - $prev.rz, 2))
        $camStep = [Math]::Sqrt([Math]::Pow($c[0] - $prev.cx, 2) + [Math]::Pow($c[1] - $prev.cy, 2) + [Math]::Pow($c[2] - $prev.cz, 2))
        $pawnStep = [Math]::Sqrt([Math]::Pow($L.x - $prev.px, 2) + [Math]::Pow($L.y - $prev.py, 2) + [Math]::Pow($L.z - $prev.pz, 2))
        $yawStep = [Math]::Abs([Math]::IEEERemainder([Math]::Atan2(-$fw[0], -$fw[2]) - $prev.cyaw, 2 * [Math]::PI)) * 57.29578
    }
    $flag = @()
    if ($relJump -gt $JumpM) { $flag += "camera_offset_jump" }
    if ($camStep -gt $JumpM -and $pawnStep -lt 0.5) { $flag += "camera_cut" }
    $row = [pscustomobject][ordered]@{
        frame = $fr; elapsed = [Math]::Round(($fr - $Press + 1) / 60.0, 4); form = $L.form; move_form = $L.moveForm; anim = $L.anim; anim_t = $L.t
        both = $L.both; arm = $L.arm; hand = $L.hand; camS = $L.camS; vyaw = $L.vyaw; camD = $L.camD; camH = $L.camH
        pawn_x = $L.x; pawn_y = $L.y; pawn_z = $L.z; cam_x = $c[0]; cam_y = $c[1]; cam_z = $c[2]
        cam_fwd_x = $fw[0]; cam_fwd_y = $fw[1]; cam_fwd_z = $fw[2]; cam_yaw_deg = [Math]::Round([Math]::Atan2(-$fw[0], -$fw[2]) * 57.29578, 3)
        rel_x = [Math]::Round($rel[0], 3); rel_y = [Math]::Round($rel[1], 3); rel_z = [Math]::Round($rel[2], 3)
        offset_jump_m = [Math]::Round($relJump, 3); cam_step_m = [Math]::Round($camStep, 3); pawn_step_m = [Math]::Round($pawnStep, 3)
        cam_yaw_step_deg = [Math]::Round($yawStep, 3); flags = ($flag -join ";")
        rx = $rel[0]; ry = $rel[1]; rz = $rel[2]; cx = $c[0]; cy = $c[1]; cz = $c[2]; px = $L.x; py = $L.y; pz = $L.z; cyaw = [Math]::Atan2(-$fw[0], -$fw[2]) }
    $rows.Add($row); $prev = $row
}
Write-WfcCsv ($rows | Select-Object * -ExcludeProperty rx, ry, rz, cx, cy, cz, px, py, pz, cyaw) (Join-Path $OutDir "trace.csv")
# Contact sheet: every 3rd frame plus every flagged frame and its neighbours.
$flagged = @($rows | Where-Object { $_.flags })
$want = @{}; foreach ($r in $rows) { if (($r.frame - $From) % 3 -eq 0) { $want[$r.frame] = $true } }
foreach ($r in $flagged) { foreach ($k in ($r.frame - 2)..($r.frame + 2)) { $want[$k] = $true } }
$tiles = @()
foreach ($r in $rows) {
    $bmp = Join-Path $OutDir ("f{0:D5}.bmp" -f $r.frame)
    if (-not (Test-Path $bmp)) { continue }
    if ($want[$r.frame]) {
        $png = Convert-WfcBmp $bmp
        $tiles += @{ png = $png; label = ("f{0} e={1:F3} {2}/{3} camS={4} both={5} jump={6:F2} {7}" -f $r.frame, $r.elapsed, $r.form, $r.move_form, $r.camS, $r.both, $r.offset_jump_m, $r.flags); flag = [bool]$r.flags }
    } else { Remove-Item -LiteralPath $bmp }
}
New-WfcSheet $tiles (Join-Path $OutDir "sheet.png") 5 384 216
$sum = @("exit $rc; frames $($rows.Count); flagged $($flagged.Count)")
foreach ($r in $flagged) { $sum += ("frame {0} e={1:F3}s form {2} move {3} camS {4} both {5}: offset jump {6:F3} m, camera step {7:F3} m, pawn step {8:F3} m, yaw step {9:F2} deg" -f $r.frame, $r.elapsed, $r.form, $r.move_form, $r.camS, $r.both, $r.offset_jump_m, $r.cam_step_m, $r.pawn_step_m, $r.cam_yaw_step_deg) }
$sum | Set-Content (Join-Path $OutDir "summary.txt")
$sum
