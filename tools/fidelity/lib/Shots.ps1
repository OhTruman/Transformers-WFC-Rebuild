# Shared helpers for multi-view capture through the product's WFC_SHOTLIST diagnostic
# (Application.cpp: lines "<name> x,y,z,tx,ty,tz" in glTF metres; each camera held 8 frames and captured to
# WFC_SHOTDIR/<name>.bmp; one map load for many views). Dot-source after lib\Run.ps1.
#
# Memory/disk: every BMP is reduced to an RGB grid (8 px cells, ~170 KB in memory) right after the run and
# deleted; only the shots named in -Keep are written as JPEG (half size). Grids are returned in a hashtable.
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "ImageStats.cs") -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Drawing

$script:WfcSlice = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets"
$script:WfcContent = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice"
$script:WfcFovX = 80.0; $script:WfcW = 1280; $script:WfcH = 720; $script:WfcCell = 8

function UeToGltf($x, $y, $z) { return , @(([double]$x / 100), ([double]$z / 100), ([double]$y / 100)) }   # glTF = (X, Z, Y) / 100
# Column-major 4x4 (glTF / props.json gltf_matrix) times point.
function MulPoint($m, $p) { return , @(($m[0] * $p[0] + $m[4] * $p[1] + $m[8] * $p[2] + $m[12]), ($m[1] * $p[0] + $m[5] * $p[1] + $m[9] * $p[2] + $m[13]), ($m[2] * $p[0] + $m[6] * $p[1] + $m[10] * $p[2] + $m[14])) }
# Camera on a horizontal ring around target t: azimuth az (rad), horizontal distance d, height dh above t.
function RingCam($t, $az, $d, $dh) { return , @(($t[0] + $d * [Math]::Sin($az)), ($t[1] + $dh), ($t[2] + $d * [Math]::Cos($az))) }
function ShotLine($name, $c, $t) { "{0} {1:F3},{2:F3},{3:F3},{4:F3},{5:F3},{6:F3}" -f $name, $c[0], $c[1], $c[2], $t[0], $t[1], $t[2] }

# Project world point p for a camera at c looking at t -> grid coordinates (x, y) or $null when behind.
function ProjectGrid($c, $t, $p) {
    $f = @(($t[0] - $c[0]), ($t[1] - $c[1]), ($t[2] - $c[2])); $fl = [Math]::Sqrt($f[0] * $f[0] + $f[1] * $f[1] + $f[2] * $f[2]); $f = @(($f[0] / $fl), ($f[1] / $fl), ($f[2] / $fl))
    $r = @((-$f[2]), 0.0, $f[0]); $rl = [Math]::Sqrt($r[0] * $r[0] + $r[2] * $r[2]); if ($rl -lt 1e-6) { return $null }; $r = @(($r[0] / $rl), 0.0, ($r[2] / $rl))   # f x up
    $u = @(($r[1] * $f[2] - $r[2] * $f[1]), ($r[2] * $f[0] - $r[0] * $f[2]), ($r[0] * $f[1] - $r[1] * $f[0]))
    $d = @(($p[0] - $c[0]), ($p[1] - $c[1]), ($p[2] - $c[2]))
    $z = $d[0] * $f[0] + $d[1] * $f[1] + $d[2] * $f[2]; if ($z -lt 0.05) { return $null }
    $tx = [Math]::Tan($script:WfcFovX * [Math]::PI / 360.0); $ty = $tx * $script:WfcH / $script:WfcW
    $nx = ($d[0] * $r[0] + $d[1] * $r[1] + $d[2] * $r[2]) / $z / $tx; $ny = ($d[0] * $u[0] + $d[1] * $u[1] + $d[2] * $u[2]) / $z / $ty
    return , @((($nx + 1) / 2 * $script:WfcW / $script:WfcCell), ((1 - $ny) / 2 * $script:WfcH / $script:WfcCell))
}
# Convex hull (monotone chain) of 2D points -> flat [x0,y0,x1,y1,...].
function Hull2D($pts) {
    $p = @($pts | Sort-Object { $_[0] }, { $_[1] })
    if ($p.Count -lt 3) { return @() }
    $cross = { param($o, $a, $b) ($a[0] - $o[0]) * ($b[1] - $o[1]) - ($a[1] - $o[1]) * ($b[0] - $o[0]) }
    $lower = New-Object System.Collections.Generic.List[object]; foreach ($q in $p) { while ($lower.Count -ge 2 -and (& $cross $lower[$lower.Count - 2] $lower[$lower.Count - 1] $q) -le 0) { $lower.RemoveAt($lower.Count - 1) }; $lower.Add($q) }
    $upper = New-Object System.Collections.Generic.List[object]; for ($i = $p.Count - 1; $i -ge 0; $i--) { $q = $p[$i]; while ($upper.Count -ge 2 -and (& $cross $upper[$upper.Count - 2] $upper[$upper.Count - 1] $q) -le 0) { $upper.RemoveAt($upper.Count - 1) }; $upper.Add($q) }
    $h = @(); for ($i = 0; $i -lt $lower.Count - 1; $i++) { $h += , $lower[$i] }; for ($i = 0; $i -lt $upper.Count - 1; $i++) { $h += , $upper[$i] }   # ", " keeps pairs
    $flat = @(); foreach ($q in $h) { $flat += [double]$q[0]; $flat += [double]$q[1] }
    return , $flat
}

# glTF primitive bounds per material name (cached per mesh file): @{ <material> = @(@(min), @(max)), ... }
$script:WfcPrimCache = @{}
function MeshPrimBounds($gltfRel) {
    if ($script:WfcPrimCache.ContainsKey($gltfRel)) { return $script:WfcPrimCache[$gltfRel] }
    $out = @{}
    $f = Join-Path $script:WfcContent $gltfRel; if (-not (Test-Path $f)) { $f = Join-Path (Split-Path $script:WfcContent) $gltfRel }
    if ((Test-Path $f) -and $f.EndsWith(".gltf")) {
        $g = Get-Content -Raw $f | ConvertFrom-Json
        $pi = 0
        foreach ($m in $g.meshes) { foreach ($pr in $m.primitives) {
            $mat = if ($null -ne $pr.material) { $g.materials[$pr.material].name } else { "" }
            $a = $g.accessors[$pr.attributes.POSITION]
            $out["#$pi"] = @(@([double]$a.min[0], [double]$a.min[1], [double]$a.min[2]), @([double]$a.max[0], [double]$a.max[1], [double]$a.max[2])); $pi++   # by section index
            if ($out.ContainsKey($mat)) { $o = $out[$mat]; for ($k = 0; $k -lt 3; $k++) { $o[0][$k] = [Math]::Min($o[0][$k], $a.min[$k]); $o[1][$k] = [Math]::Max($o[1][$k], $a.max[$k]) } }
            else { $out[$mat] = @(@([double]$a.min[0], [double]$a.min[1], [double]$a.min[2]), @([double]$a.max[0], [double]$a.max[1], [double]$a.max[2])) }
        } }
    }
    $script:WfcPrimCache[$gltfRel] = $out
    return $out
}
# World-space corners (8) of a placed prop's primitive with material $matShort (short material name).
function PropCorners($prop, $matShort) {
    if (-not $prop.gltf) { return @() }
    $b = MeshPrimBounds $prop.gltf
    $key = $null
    $ov = @($prop.material_overrides)                  # override i replaces section i
    for ($i = 0; $i -lt $ov.Count; $i++) { if ($ov[$i] -is [string] -and $ov[$i] -like "*$matShort*" -and $b.ContainsKey("#$i")) { $key = "#$i"; break } }
    if (-not $key) { $key = @($b.Keys | Where-Object { $_ -notlike "#*" -and ($_ -eq $matShort -or $matShort -like "*$_*" -or $_ -like "*$matShort*") })[0] }
    if (-not $key) { return @() }
    $mn = $b[$key][0]; $mx = $b[$key][1]; $cs = @()
    foreach ($x in $mn[0], $mx[0]) { foreach ($y in $mn[1], $mx[1]) { foreach ($z in $mn[2], $mx[2]) { $cs += , (MulPoint $prop.gltf_matrix @($x, $y, $z)) } } }
    return , $cs
}
# Points on a placed prop primitive's surface: an n x n grid over its two largest bounding-box axes at the
# middle of the thinnest (a flat glass / sheet primitive), world space.
function PropSurfaceSamples($prop, $matShort, [int]$n = 24) {
    $cs = PropCorners $prop $matShort; if (-not $cs.Count) { return @() }
    $b = MeshPrimBounds $prop.gltf
    $key = $null; $ov = @($prop.material_overrides)
    for ($i = 0; $i -lt $ov.Count; $i++) { if ($ov[$i] -is [string] -and $ov[$i] -like "*$matShort*" -and $b.ContainsKey("#$i")) { $key = "#$i"; break } }
    if (-not $key) { $key = @($b.Keys | Where-Object { $_ -notlike "#*" -and ($_ -eq $matShort -or $matShort -like "*$_*" -or $_ -like "*$matShort*") })[0] }
    $mn = $b[$key][0]; $mx = $b[$key][1]
    $ext = @(($mx[0] - $mn[0]), ($mx[1] - $mn[1]), ($mx[2] - $mn[2])); $thin = [Array]::IndexOf($ext, ($ext | Measure-Object -Minimum).Minimum)
    $ax = @(0, 1, 2 | Where-Object { $_ -ne $thin })
    $out = @()
    for ($i = 0; $i -lt $n; $i++) { for ($j = 0; $j -lt $n; $j++) {
        $p = @((($mn[0] + $mx[0]) / 2), (($mn[1] + $mx[1]) / 2), (($mn[2] + $mx[2]) / 2))
        $p[$ax[0]] = $mn[$ax[0]] + ($i + 0.5) / $n * $ext[$ax[0]]; $p[$ax[1]] = $mn[$ax[1]] + ($j + 0.5) / $n * $ext[$ax[1]]
        $out += , (MulPoint $prop.gltf_matrix $p)
    } }
    return , $out
}
# Grid cell indices (y*w+x) covered by the projections of world points (in front of the camera, on screen).
function ProjectCells($c, $t, $pts) {
    $w = [int]($script:WfcW / $script:WfcCell); $h = [int]($script:WfcH / $script:WfcCell); $set = @{}
    foreach ($p in $pts) { $q = ProjectGrid $c $t $p; if (-not $q) { continue }; $x = [int][Math]::Floor($q[0]); $y = [int][Math]::Floor($q[1]); if ($x -ge 0 -and $x -lt $w -and $y -ge 0 -and $y -lt $h) { $set[$y * $w + $x] = $true } }
    return , ([int[]]@($set.Keys))
}
function Centroid($pts) { $s = @(0.0, 0.0, 0.0); foreach ($p in $pts) { $s[0] += $p[0]; $s[1] += $p[1]; $s[2] += $p[2] }; return , @(($s[0] / $pts.Count), ($s[1] / $pts.Count), ($s[2] / $pts.Count)) }

# Run the product over a shot list. $Shots = ordered list of @{ name; c; t } ; returns @{ rc; grids = @{name = float[]} }.
# $Keep = names to keep as JPEG (half size) in $Dir; $Overlay = scriptblock(name, bmpPath) called before deletion.
function Invoke-ShotList([string]$Exe, [string]$Dir, $Shots, [hashtable]$Env = @{}, [string]$RenderData = "", $Keep = @(), [scriptblock]$Overlay = $null) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $Dir = (Resolve-Path $Dir).Path                     # the exe runs with cwd = $Dir: WFC_SHOTDIR must be absolute
    Get-ChildItem $Dir -Filter *.bmp -ErrorAction SilentlyContinue | Remove-Item
    $list = Join-Path $Dir "shots.txt"
    ($Shots | ForEach-Object { ShotLine $_.name $_.c $_.t }) | Set-Content -Encoding ASCII $list
    $e = @{ WFC_SHOTLIST = $list; WFC_SHOTDIR = $Dir; WFC_SMOKE_FRAMES = "$(8 * $Shots.Count + 24)"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0" } + $Env
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $Dir $e "run.log" 3600
    $grids = @{}
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
    $ep = New-Object System.Drawing.Imaging.EncoderParameters 1; $ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 80L
    foreach ($s in $Shots) {
        $bmp = Join-Path $Dir "$($s.name).bmp"
        if (-not (Test-Path $bmp)) { continue }
        $grids[$s.name] = [WfcImage]::Rgb($bmp, $script:WfcCell)
        if ($Overlay) { & $Overlay $s.name $bmp | Out-Host }   # callback output must not join the return value
        if ($Keep -contains $s.name) {
            $img = [System.Drawing.Image]::FromFile($bmp); $bm = New-Object System.Drawing.Bitmap $img, ([int]($img.Width / 2)), ([int]($img.Height / 2)); $img.Dispose()
            $bm.Save((Join-Path $Dir "$($s.name).jpg"), $codec, $ep); $bm.Dispose()
        }
        Remove-Item -LiteralPath $bmp
    }
    return @{ rc = $rc; grids = $grids }
}
# Half-size JPEG of any image (overlays).
function Save-WfcJpeg([string]$Src, [string]$Dst) {
    $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object MimeType -eq 'image/jpeg'
    $ep = New-Object System.Drawing.Imaging.EncoderParameters 1; $ep.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 80L
    $img = [System.Drawing.Image]::FromFile($Src); $bm = New-Object System.Drawing.Bitmap $img, ([int]($img.Width / 2)), ([int]($img.Height / 2)); $img.Dispose()
    $bm.Save($Dst, $codec, $ep); $bm.Dispose()
}
