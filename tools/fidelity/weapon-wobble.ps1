# WEAPON WOBBLE (P0-1, user playtest 2026-10-13: "sidestepping makes the gun wobbly / glitchy ... almost like an interlaced
# effect going left to right"; weapon only). Tier TARGETED. One exe per variant, direct-boot match, windowed, RT off, HD off.
# The display rate is made deterministic with WFC_RENDERHZ (0 or 1 sim steps per frame at 144 Hz, as on the user's monitor),
# the pawn strafes right (WFC_AUTOSTRAFE, mouse off) and WFC_SHOTEVERY captures consecutive frames; wobble.py measures each
# 64 px tile's frame-to-frame shift and reports the tiles whose motion is NOT steady (jitter = mean |second difference|,
# flips = share of frames whose step changes sign). Lockstep identity captures never see this (one step per frame, alpha 1).
#
# (-From / -Count: a wide window of frames after boot; frame timing varies per run, so wobble.py measures only the steady
#  strafe inside it - the longest run of moving frames. Streets TDM at 144 Hz: spawn after the 10 s countdown at ~1450-1600,
#  then about 115 frames of strafe before a wall. The frames are deleted after the measurement except every 20th.)
# Variants (the A/B switches the exe already has):
#   hz60          WFC_RENDERHZ=60: one step per frame, the control (no presentation interpolation in play)
#   default       WFC_RENDERHZ=144
#   nointerp      + WFC_NOINTERP=1 (the Pass 25b presentation interpolation off: everything hops on steps together)
#   weapondirect  + WFC_WEAPONDIRECT=1 (the M1 1a1fb1c1 weapon draw queue bypassed: the pre-M1 path)
#   fsr           + WFC_FSR=0.67 WFC_FSR3=1 (temporal upscaling: needs the gun's own motion vectors)
#   motionview    + WFC_MOTIONVIEW=1 (diagnostic images of |velocity|; not measured)
# Verdict per variant: the gun tiles relative to the body tiles (see below); the default variant is the user's case. A fix
# build should PASS default (and fsr).
#
#   .\tools\fidelity\weapon-wobble.ps1 -Exe <wfc_rebuild.exe> -OutDir <dir> [-Variants hz60,default,nointerp,weapondirect,fsr]
#       [-Map MP_IAC_Streets_Base_m] [-From 1300] [-Count 480] [-RatioTol 0.25] [-ProbeTiles "7,8;8,7;6,8"]
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$OutDir, [string[]]$Variants = @("hz60", "default", "nointerp", "weapondirect", "fsr"),
      [string]$Map = "MP_IAC_Streets_Base_m", [int]$From = 1300, [int]$Count = 480, [double]$RatioTol = 0.25, [string]$ProbeTiles = "", [string]$BodyTiles = "3,5;4,5;5,5;3,6;4,6;5,6;3,7;4,7;5,7")
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
if ($Variants.Count -eq 1 -and $Variants[0] -match ',') { $Variants = $Variants[0] -split ',' }   # (-File passes "a,b" as one string)
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$res = New-WfcResults; $rows = @{}
$extra = @{ hz60 = @{ WFC_RENDERHZ = "60" }; default = @{ WFC_RENDERHZ = "144" }; nointerp = @{ WFC_RENDERHZ = "144"; WFC_NOINTERP = "1" }
            weapondirect = @{ WFC_RENDERHZ = "144"; WFC_WEAPONDIRECT = "1" }; fsr = @{ WFC_RENDERHZ = "144"; WFC_FSR = "0.67"; WFC_FSR3 = "1" }
            motionview = @{ WFC_RENDERHZ = "144"; WFC_MOTIONVIEW = "1" } }
foreach ($v in $Variants) {
    if (-not $extra.ContainsKey($v)) { "unknown variant $v"; continue }
    $d = Join-Path $OutDir $v; $fr = Join-Path $d "frames"; New-Item -ItemType Directory -Force $fr | Out-Null
    $cf0 = $From; $cf1 = $From + $Count - 1; $js0 = Join-Path $d "wobble.json"
    if ($v -eq "hz60") { $cf0 = [int][Math]::Ceiling($From / 2.4); $cf1 = $cf0 + [int]($Count / 2.4) - 1 }   # the same match moment at one step per frame
    if (-not (Test-Path $js0)) {
        (Get-BotProfile 0 0 1 -Width 1920 -Height 1080) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
        $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = "${Map}?GameModeTag=TDM"; WFC_RT_MODE = "0"; WFC_NOMOUSE = "1"; WFC_SKIPINTRO = "1"; WFC_AUTOSTRAFE = "1"
                WFC_SHOTEVERY = ($fr -replace '\\', '/') + ",$cf0,$cf1"; WFC_SMOKE_FRAMES = "$($cf1 + 10)" }
        foreach ($k in $extra[$v].Keys) { $e[$k] = $extra[$v][$k] }
        $code = Invoke-WfcExe $Exe $d $e "run.log" 900
        "{0:HH:mm:ss} {1}: exit {2}, {3} frames" -f (Get-Date), $v, $code, @(Get-ChildItem $fr -Filter *.bmp).Count
    }
    if ($v -eq "motionview") { continue }
    $js = Join-Path $d "wobble.json"
    if (-not (Test-Path $js)) { $env:PYTHONDONTWRITEBYTECODE = "1"; & $py (Join-Path $PSScriptRoot "wobble.py") $fr $js | ForEach-Object { "  $v $_" } }
    if (Test-Path $js) { $rows[$v] = Get-Content $js -Raw | ConvertFrom-Json }
    if (@(Get-ChildItem $fr -Filter *.bmp).Count -gt 60) { Get-ChildItem $fr -Filter *.bmp | Where-Object { [int]($_.BaseName.Substring(1)) % 20 -ne 0 } | Remove-Item -Force }   # (~2.7 MB each)
    if (Test-Path (Join-Path $d "CONTAMINATED.txt")) { $rows[$v] | Add-Member -Force NoteProperty contaminated $true }
}
# The gun's tiles (the probe): -ProbeTiles "x,y;x,y" (64 px tile coordinates), else the 3 worst tiles of this run's
# "default" variant; the body's tiles (-BodyTiles, default the Streets spawn's torso). The character is at the same screen
# place in every variant (same spawn, same strafe). Verdict (2026-10-13, after-run on a44aa6db): GUN RELATIVE TO BODY in the
# same run, as Rendering measures it - at 144 Hz every camera-locked character tile has more tile jitter than at 60 Hz
# (interpolated piecewise motion), so an absolute comparison with the 60 Hz control fails a correct gun. PASS when the gun /
# body jitter ratio is within the 60 Hz control's ratio + -RatioTol AND the gun's sign-flip rate is <= the body's + 0.1
# (the wobble's signature: the gun alone alternates left / right).
function TilesOf([string]$spec) { return @($spec -split ';' | Where-Object { $_ } | ForEach-Object { $xy = $_ -split ','; ,@([int]$xy[0], [int]$xy[1]) }) }
$probe = @()
if ($ProbeTiles) { $probe = TilesOf $ProbeTiles }
elseif ($rows["default"] -and $rows["default"].top) { $probe = @($rows["default"].top | Select-Object -First 3 | ForEach-Object { ,@([int]$_.tile[0], [int]$_.tile[1]) }) }
$body = TilesOf $BodyTiles
function AvgOf($r, [string]$grid, $tiles) { if (-not $r -or -not $r.$grid -or -not $tiles.Count) { return $null }; $v = @($tiles | ForEach-Object { [double]$r.$grid[$_[1]][$_[0]] }); return [Math]::Round(($v | Measure-Object -Average).Average, 3) }
$ctl = $rows["hz60"]; $cj = AvgOf $ctl "grid_jitter" $probe; $cb = AvgOf $ctl "grid_jitter" $body
$cRatio = if ($cj -ne $null -and $cb) { [Math]::Round($cj / $cb, 2) } else { $null }
foreach ($v in $rows.Keys) {
    $r = $rows[$v]
    if ($r.status -eq "UNKNOWN" -or $null -eq $r.max_tile_jitter_px) { Add-WfcResult $res "wobble.$v" "UNKNOWN" $null ("no measurement: {0}" -f $r.note) "Experimental"; continue }
    $pj = AvgOf $r "grid_jitter" $probe; $pf = AvgOf $r "grid_flips" $probe; $bj = AvgOf $r "grid_jitter" $body; $bf = AvgOf $r "grid_flips" $body
    $ratio = if ($bj) { [Math]::Round($pj / $bj, 2) } else { $null }
    $note = "gun tiles ({0}) jitter {1} px, flips {2}; body tiles jitter {3} px, flips {4}; gun / body {5}; steady {6}; background |dx| {7} px/frame" -f (($probe | ForEach-Object { $_ -join "," }) -join " "), $pj, $pf, $bj, $bf, $ratio, ($r.steady -join "-"), $r.median_abs_dx_px
    if ($r.contaminated) { $note += " (other lanes' processes ran: untimed, noted only)" }
    if ($v -eq "hz60") { Add-WfcResult $res "wobble.$v" "INFO" $ratio "control: $note" "Experimental"; continue }
    $st = if ($null -eq $cRatio -or $null -eq $ratio) { "UNKNOWN" } elseif ($ratio -le $cRatio + $RatioTol -and $pf -le $bf + 0.1) { "PASS" } else { "FAIL" }
    Add-WfcResult $res "wobble.$v" $st $ratio ("{0}; 60 Hz control gun / body {1} + tol {2}" -f $note, $cRatio, $RatioTol) "Rendering" $(if ($null -ne $cRatio) { $cRatio + $RatioTol } else { $null }) "ratio"
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"WEAPON WOBBLE: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
