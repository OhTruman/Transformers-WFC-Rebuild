# Standardized screenshots for human A/B review (real wfc_rebuild.exe, fixed cameras).
#
#   .\tools\fidelity\stills.ps1                         # all sets -> work\fidelity\stills\
#   .\tools\fidelity\stills.ps1 -Sets transform_r2v
#
# Sets:
#   transform_r2v / transform_v2r  contact sheet around the fold: transform pressed at frame P, one run per
#                                  capture offset (P+k); each tile is labelled from that frame's log line
#                                  (form, clip, clip time, fine aim...). Fixed side camera (WFC_RENDERCAM).
#   vehicle_materials              vehicle at front/side/rear/3-4 angles, WFC path vs legacy path, + robot.
# The exe integrates wall-clock time, so the clip time at a given frame differs between machines; the label
# carries the actual state. Output: <set>.png contact sheet + the individual PNGs.
param([string[]]$Sets = @("transform_r2v", "transform_v2r", "vehicle_materials"), [string]$Exe = "", [string]$OutDir = "")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\stills" }
Add-Type -AssemblyName System.Drawing

# Authored FFA spawn used by the exe (World::respawnPlayer) and its facing.
$spawn = @(363.5, -724.48, -341.8); $yaw = 1.01
$fwd = @(-[Math]::Sin($yaw), 0, -[Math]::Cos($yaw)); $right = @(-$fwd[2], 0, $fwd[0])
function Cam([double[]]$offset, [double]$lookUp = 2.0) {
    # camera at spawn + offset (in fwd/right/up), looking at spawn + lookUp
    $c = @(($spawn[0] + $fwd[0] * $offset[0] + $right[0] * $offset[1]), ($spawn[1] + $offset[2]), ($spawn[2] + $fwd[2] * $offset[0] + $right[2] * $offset[1]))
    $d = @(($spawn[0] - $c[0]), ($spawn[1] + $lookUp - $c[1]), ($spawn[2] - $c[2]))
    $len = [Math]::Sqrt($d[0] * $d[0] + $d[1] * $d[1] + $d[2] * $d[2])
    $cy = [Math]::Atan2(-$d[0], -$d[2]); $cp = [Math]::Asin($d[1] / $len)
    return ("{0:F3},{1:F3},{2:F3},{3:F4},{4:F4}" -f $c[0], $c[1], $c[2], $cy, $cp)
}
function Shot($dir, $name, [hashtable]$envs, [int]$frames) {
    $bmp = Join-Path $dir "$name.bmp"; $log = Join-Path $dir "$name.log"
    $all = @{ WFC_SMOKE_FRAMES = "$frames"; WFC_SHOT = $bmp; WFC_LOGEVERY = "1" } + $envs
    $saved = @{}; foreach ($k in $all.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, [string]$all[$k], "Process") }
    try { $p = Start-Process -FilePath $Exe -WorkingDirectory $dir -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err" }
    finally { foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") } }
    $label = "$name"
    $last = Get-Content $log | Select-String "\] frame $frames " | Select-Object -Last 1
    if ($last -and $last.Line -match "form=(\S+) anim=(\S+) t=([\d.]+).* moveForm=(\S+)") { $label = "$name  form=$($Matches[1]) move=$($Matches[4]) $($Matches[2]) t=$($Matches[3])" }
    if (-not (Test-Path $bmp)) { return @{ png = $null; label = "$label (no shot, exit $($p.ExitCode))" } }
    $png = Join-Path $dir "$name.png"
    $img = [System.Drawing.Image]::FromFile($bmp); try { $img.Save($png, [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
    Remove-Item $bmp
    return @{ png = $png; label = $label }
}
function Sheet($tiles, $path, [int]$cols = 4) {
    $w = 480; $h = 270; $lh = 18
    $rows = [Math]::Ceiling($tiles.Count / $cols)
    $bmp = New-Object System.Drawing.Bitmap ($cols * $w), ($rows * ($h + $lh))
    $g = [System.Drawing.Graphics]::FromImage($bmp); $g.Clear([System.Drawing.Color]::Black)
    $font = New-Object System.Drawing.Font "Consolas", 8
    for ($i = 0; $i -lt $tiles.Count; $i++) {
        $x = ($i % $cols) * $w; $y = [Math]::Floor($i / $cols) * ($h + $lh)
        if ($tiles[$i].png) { $im = [System.Drawing.Image]::FromFile($tiles[$i].png); $g.DrawImage($im, $x, $y + $lh, $w, $h); $im.Dispose() }
        $g.DrawString($tiles[$i].label, $font, [System.Drawing.Brushes]::White, $x + 2, $y + 2)
    }
    $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
$side = Cam @(0, 12, 3) 2.0
foreach ($set in $Sets) {
    $dir = Join-Path $OutDir $set; New-Item -ItemType Directory -Force $dir | Out-Null
    $tiles = @()
    switch ($set) {
        "transform_r2v" {
            foreach ($k in 0, 20, 45, 70, 100, 140, 180, 230, 290, 360, 440, 540) {
                $tiles += Shot $dir ("r2v_p{0:D3}" -f $k) @{ WFC_PRESSTRANSFORM = "60"; WFC_RENDERCAM = $side } (60 + $k)
            }
        }
        "transform_v2r" {
            foreach ($k in 0, 15, 30, 50, 75, 100, 130, 170, 220, 280, 350, 440) {
                $tiles += Shot $dir ("v2r_p{0:D3}" -f $k) @{ WFC_STARTVEHICLE = "1"; WFC_PRESSTRANSFORM = "60"; WFC_RENDERCAM = $side } (60 + $k)
            }
        }
        "vehicle_materials" {
            $angles = [ordered]@{ front = @(9, 0, 2.5); side = @(0, 9, 2.0); rear = @(-9, 0, 2.5); threequarter = @(6.5, 6.5, 5.0) }
            foreach ($a in $angles.Keys) {
                $tiles += Shot $dir "veh_wfc_$a" @{ WFC_STARTVEHICLE = "1"; WFC_RENDERCAM = (Cam $angles[$a] 1.5) } 90
                $tiles += Shot $dir "veh_legacy_$a" @{ WFC_STARTVEHICLE = "1"; WFC_RENDERCAM = (Cam $angles[$a] 1.5); WFC_LEGACYRENDER = "1" } 90
            }
            foreach ($a in "front", "side") { $tiles += Shot $dir "robot_wfc_$a" @{ WFC_RENDERCAM = (Cam $angles[$a] 2.5) } 90 }
        }
    }
    Sheet $tiles (Join-Path $OutDir "$set.png")
    "$set -> $(Join-Path $OutDir "$set.png") ($($tiles.Count) tiles)"
}
