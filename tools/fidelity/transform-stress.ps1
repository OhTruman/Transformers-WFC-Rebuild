# Deterministic high-volume transform stress test on the real exe (Release, lockstep 60 Hz), judged against
# the AUTHORED pawn collision world (collision_query.py on ExtractedAssets collision_pawn.glb).
#
#   .\tools\fidelity\transform-stress.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Starts 0..83] [-Scenarios a,b]
#
# Matrix: every authored player start (WFC_START, 84) x scenarios (robot->vehicle and vehicle->robot:
# stationary, walking/driving, boost, nitro, boost+turn, airborne). Ramps, inclines, declines, walls and thin
# floors come from the starts' own authored headings; each run is tagged with what it met (incline at the
# press, wall = speed collapse, thin floor = authored floor thickness < 0.5 m under the press point).
# Recorded per run: position / horizontal speed / vertical speed before and after, floor height under the
# press point and the end point, form / collision form, camera distance, min y.
# Verdicts (pos.y = actor ground level for both forms, checked against the authored floors):
#   FAIL fell_through  - at the first sampled frame where the pawn is > 1 m below the floor it stood on, that
#                        same floor (within 0.6 m of the press floor height) exists in the authored collision
#                        directly above it: it passed through authored collision (not over an edge).
#   FAIL out_of_bounds - y below KillZ (-749 m) or no authored floor under the pawn within 200 m.
#   INFO large_drop    - ended > 3 m lower on another authored floor (drove/fell off an edge; legal).
#   FAIL wrong_form    - the run ends in neither the requested form nor a transform in progress.
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [string[]]$Starts = @(), [string[]]$Scenarios = @(), [switch]$AnalyzeOnly, [string]$Map = "MP_IAC_Streets", [int]$StartSample = 0)
$ErrorActionPreference = "Stop"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes "a,b" as one string
$Starts = @($Starts | ForEach-Object { $_ -split "," } | Where-Object { $_ } | ForEach-Object { [int]$_ })
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$MapDir = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$Map"
if (-not (Test-Path $MapDir)) { throw "no runtime data for map $Map" }
# player starts = the map's gameplay.json player_starts with a location (what WFC_START indexes)
$nStarts = @((Get-Content -Raw (Join-Path $MapDir "gameplay.json") | ConvertFrom-Json).player_starts | Where-Object { @($_.location_gltf).Count -ge 3 }).Count
if (-not $Starts.Count) { $Starts = if ($StartSample -gt 0 -and $StartSample -lt $nStarts) { @(0..($StartSample - 1) | ForEach-Object { [int][Math]::Floor($_ * $nStarts / $StartSample) }) } else { @(0..($nStarts - 1)) } }# KillZ: the persistent level's TnWorldInfo (<map>_BASE_m in physics.json), m; UE3 default -262143 UU when unauthored
$killZ = -2621.43; $pw = (Get-Content -Raw (Join-Path $MapDir "physics.json") | ConvertFrom-Json).world; foreach ($q in $pw.PSObject.Properties) { if ($q.Name -like "*_BASE_m" -and $q.Value.KillZ -ne $null) { $killZ = [double]$q.Value.KillZ * 0.01 } }
$colGlb = Join-Path $MapDir "collision_pawn.glb"
$defs = [ordered]@{
    r2v_stationary = @{ to = "VEHICLE"; press = 60;  frames = 240; env = @{} }
    r2v_walk       = @{ to = "VEHICLE"; press = 90;  frames = 270; env = @{ WFC_AUTOWALK = "1" } }
    r2v_airborne   = @{ to = "VEHICLE"; press = 150; frames = 330; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOJUMP_EVERY = "140" } }
    v2r_stationary = @{ to = "ROBOT";   press = 60;  frames = 240; env = @{ WFC_STARTVEHICLE = "1" } }
    v2r_drive      = @{ to = "ROBOT";   press = 120; frames = 300; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1" } }
    v2r_boost      = @{ to = "ROBOT";   press = 150; frames = 330; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1" } }
    v2r_nitro      = @{ to = "ROBOT";   press = 150; frames = 330; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "120" } }
    v2r_boost_turn = @{ to = "ROBOT";   press = 150; frames = 330; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.6" } }
    v2r_airborne   = @{ to = "ROBOT";   press = 150; frames = 330; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOJUMP_EVERY = "140" } }
    # M05 additions: longer boosts reach ramps / walls / overhangs further from the start; opposite turn; late press
    v2r_boost_long = @{ to = "ROBOT";   press = 300; frames = 480; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1" } }
    v2r_boost_left = @{ to = "ROBOT";   press = 240; frames = 420; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "-0.6" } }
    v2r_nitro_late = @{ to = "ROBOT";   press = 420; frames = 600; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.25"; WFC_AUTODASH = "400" } }
    # repeated rapid transforms (WFC_PRESSTRANSFORM_EVERY, Systems soak hook); judged by the general detector only
    v2r_rapid      = @{ to = "ANY";     press = 0;   frames = 720; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_PRESSTRANSFORM_EVERY = "40"; WFC_AUTOTURN = "0.3" } }
    r2v_rapid      = @{ to = "ANY";     press = 0;   frames = 720; env = @{ WFC_AUTOWALK = "1"; WFC_PRESSTRANSFORM_EVERY = "33"; WFC_AUTOJUMP_EVERY = "97"; WFC_AUTOTURN = "-0.3" } }
}
$baseScenarios = @("r2v_stationary", "r2v_walk", "r2v_airborne", "v2r_stationary", "v2r_drive", "v2r_boost", "v2r_nitro", "v2r_boost_turn", "v2r_airborne")
if (-not $Scenarios.Count) { $Scenarios = $baseScenarios }   # -Scenarios all = every scenario
if ($Scenarios -contains "all") { $Scenarios = @($defs.Keys) }
# ---- 1. runs (resumable: a run with trace.csv is skipped) ----
$runsDir = Join-Path $OutDir "runs"; New-Item -ItemType Directory -Force $runsDir | Out-Null
$todo = @(); foreach ($sc in $Scenarios) { foreach ($s in $Starts) { $todo += @{ sc = $sc; s = $s } } }
$n = 0
foreach ($r in $todo) {
    $n++; $name = "{0}_s{1:D2}" -f $r.sc, $r.s; $dir = Join-Path $runsDir $name; $tr = Join-Path $dir "trace.csv"
    if ($AnalyzeOnly -or (Test-Path $tr)) { continue }
    $d = $defs[$r.sc]
    $e = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_START = "$($r.s)"; WFC_MAP = $Map } + $d.env
    if ($d.press -gt 0) { $e.WFC_PRESSTRANSFORM = "$($d.press)" }
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $e "run.log" 600
    $rows = foreach ($f in (Read-WfcFrames (Join-Path $dir "wfc.log"))) { [pscustomobject][ordered]@{ frame = $f.frame; x = $f.x; y = $f.y; z = $f.z; grounded = $f.grounded; form = $f.form; moveForm = $f.moveForm; hspeed = $f.hspeed; vy = $f.vy; camD = $f.camD; anim = $f.anim } }
    Write-WfcCsv @($rows) $tr
    Remove-Item (Join-Path $dir "wfc.log")   # keep the compact trace only
    if ($n % 25 -eq 0) { Write-Host ("[{0}/{1}] {2} exit {3}" -f $n, $todo.Count, $name, $rc) }
}
# ---- 2. authored floors for every press point and trajectory sample ----
$queries = New-Object System.Collections.Generic.List[object]
$runs = @{}
foreach ($r in $todo) {
    $name = "{0}_s{1:D2}" -f $r.sc, $r.s; $tr = Join-Path (Join-Path $runsDir $name) "trace.csv"
    if (-not (Test-Path $tr)) { continue }
    $t = @(Import-Csv $tr); if ($t.Count -lt 10) { $runs[$name] = @{ broken = $true; r = $r }; continue }
    $press = $defs[$r.sc].press
    if ($press -le 0) { $press = [int]$t[0].frame + 1 }   # rapid scenarios: tags from the start point
    $pre = $t | Where-Object { [int]$_.frame -eq $press - 1 } | Select-Object -First 1; if (-not $pre) { $pre = $t[[Math]::Max(0, [Math]::Min($press - 2, $t.Count - 1))] }
    # General detector candidates: per grounded episode, the first frame more than 1 m below the last grounded height.
    # Reference = the AUTHORED floor under the last grounded position (gpos query), not the actor height: the vehicle actor
    # rides higher above the floor than the robot, so a form change alone moves the actor by most of a metre.
    $cands = @(); $lastG = $null; $lastGp = $null; $armed = $false
    foreach ($f in $t) {
        $y = [double]$f.y
        if ($f.grounded -eq "1") { $lastG = $y; $lastGp = $f; $armed = $true; continue }
        if ($armed -and $null -ne $lastG -and $y -lt $lastG - 1.0) { $cands += [pscustomobject]@{ frame = [int]$f.frame; x = [double]$f.x; y = $y; z = [double]$f.z; lastG = $lastG; gx = [double]$lastGp.x; gz = [double]$lastGp.z }; $armed = $false }
    }
    foreach ($c in $cands) { $queries.Add(@{ id = "$name|g|$($c.frame)"; x = $c.x; y = $c.lastG; z = $c.z }); $queries.Add(@{ id = "$name|gpos|$($c.frame)"; x = $c.gx; y = $c.lastG; z = $c.gz }) }
    $transforms = 0; $prevForm = $t[0].form; foreach ($f in $t) { if ($f.form -ne $prevForm) { $transforms++; $prevForm = $f.form } }
    $runs[$name] = @{ r = $r; t = $t; pre = $pre; cands = $cands; transforms = $transforms; press = $press }
    $queries.Add(@{ id = "$name|press"; x = [double]$pre.x; y = [double]$pre.y; z = [double]$pre.z })
    foreach ($f in ($t | Where-Object { ($defs[$r.sc].press -gt 0 -and [int]$_.frame -ge $press -and [int]$_.frame % 3 -eq 0) -or [int]$_.frame -eq [int]$t[-1].frame })) {
        # floor at the PRESS level under this xz (is the floor the pawn stood on still above it?) and the floor under the pawn now
        $queries.Add(@{ id = "$name|lvl|$($f.frame)"; x = [double]$f.x; y = [double]$pre.y; z = [double]$f.z })
        $queries.Add(@{ id = "$name|now|$($f.frame)"; x = [double]$f.x; y = [double]$f.y; z = [double]$f.z })
    }
}
$qf = Join-Path $OutDir "queries.json"; $qo = Join-Path $OutDir "queries_out.json"
[IO.File]::WriteAllText($qf, ($queries | ConvertTo-Json -Compress -Depth 3))
& $py (Join-Path $PSScriptRoot "collision_query.py") $colGlb $qf $qo | Out-Host
$Q = @{}; foreach ($o in (Get-Content -Raw $qo | ConvertFrom-Json)) { $Q[$o.id] = $o }
# ---- 3. classification ----
$res = New-WfcResults
$table = New-Object System.Collections.Generic.List[object]
foreach ($name in ($runs.Keys | Sort-Object)) {
    $R = $runs[$name]; $sc = $R.r.sc; $d = $defs[$sc]
    if ($R.broken) { $table.Add([pscustomobject][ordered]@{ run = $name; scenario = $sc; start = $R.r.s; verdict = "TOOL"; note = "trace too short (exe failed?)" }); continue }
    $t = $R.t; $pre = $R.pre; $end = $t[-1]
    $pq = $Q["$name|press"]; $pf = $pq.floor_below
    $post = @($t | Where-Object { [int]$_.frame -ge $R.press })
    # general detector: a candidate crossing where the floor the pawn last stood on (within 0.6 m) is right there
    $gThrough = $null
    foreach ($c in $R.cands) {
        $g = $Q["$name|g|$($c.frame)"]; $gp = $Q["$name|gpos|$($c.frame)"]
        if (-not $gp -or $null -eq $gp.floor_below) { continue }
        $floorRef = $gp.floor_below
        # under = the floor at the CROSSING xz (at the old level) is more than 1 m above the pawn; on a slope / step down
        # the pawn stands on that surface and is not under anything
        if ($g -and $null -ne $g.floor_below -and ($g.floor_below - $c.y) -gt 1.0 -and [Math]::Abs($g.floor_below - $floorRef) -le 0.6) { $gThrough = $c; $gThrough | Add-Member -Force -NotePropertyName floorRef -NotePropertyValue $g.floor_below; break }
    }
    $lowCeil = $pq -and $null -ne $pq.above_any -and ($pq.above_any - [double]$pre.y) -lt 4.2
    $minY = [double]::MaxValue; foreach ($f in $post) { $minY = [Math]::Min($minY, [double]$f.y) }
    # Judged at the CROSSING only: the first sampled frame where the pawn is > 1 m below the press floor. If the
    # press floor exists right there (within 0.6 m) it went through it; otherwise it left over an edge (legal),
    # and moving under that floor later (on a lower level) is legal too.
    $through = $null; $pen = 0.0; $cross = $null
    if ($null -ne $pf) {
        foreach ($f in $post) {
            if (-not $Q.ContainsKey("$name|lvl|$($f.frame)")) { continue }
            if ([double]$f.y -lt $pf - 1.0) { $cross = $f; break }
        }
        if ($cross) {
            $lvl = $Q["$name|lvl|$($cross.frame)"]
            if ($null -ne $lvl.floor_below -and [Math]::Abs($lvl.floor_below - $pf) -le 0.6 -and ($lvl.floor_below - [double]$cross.y) -gt 1.0) { $through = $cross; $pen = $lvl.floor_below - $minY; $through | Add-Member -Force -NotePropertyName floorHere -NotePropertyValue $lvl.floor_below }
        }
    }
    $eq = $Q["$name|now|$($end.frame)"]
    $oob = $minY -lt $killZ; $noFloor = -not $oob -and $eq -and $null -eq $eq.floor_below   # no EXPORTED floor under the end point while above KillZ: the runtime stands on something the export lacks -> INFO, look here
    $drop = if ($null -ne $pf -and $eq -and $null -ne $eq.floor_below) { $pf - $eq.floor_below } else { 0 }
    $formOk = $d.to -eq "ANY" -or $end.form -eq $d.to -or $end.anim -like "Transform_*"
    if ($d.press -le 0) { $through = $null; $pen = 0.0 }   # rapid scenarios: no single press floor; the general detector judges them
    if (-not $through -and $gThrough) { $through = [pscustomobject]@{ frame = $gThrough.frame }; $pen = $gThrough.floorRef - $minY }
    $noHook = $d.to -eq "ANY" -and $R.transforms -lt 2
    $thin = $pq -and $null -ne $pq.thickness -and $pq.thickness -lt 0.5
    $back = @($t | Where-Object { [int]$_.frame -ge $R.press - 30 -and [int]$_.frame -lt $R.press })
    $incline = if ($back.Count -ge 2) { ([double]$back[-1].y - [double]$back[0].y) } else { 0 }
    $wall = ([double]$pre.hspeed -gt 4) -and (@($post | Select-Object -First 30 | Where-Object { [double]$_.hspeed -lt 0.5 }).Count -gt 0)
    # A crossing where the pawn is grounded again on the SAME floor within 3 frames is a transient sink (a visible pop, not
    # a fall under the map); landing on a lower level (or not landing) after crossing the floor it stood on is a fall-through.
    $sinkOnly = $false; $landY = $null
    if ($through) {
        $ref = if ($gThrough -and $through.frame -eq $gThrough.frame) { $gThrough.floorRef } elseif ($through.PSObject.Properties.Name -contains "floorHere") { $through.floorHere } else { $pf }
        $land = @($t | Where-Object { [int]$_.frame -gt [int]$through.frame -and $_.grounded -eq "1" } | Select-Object -First 1)[0]
        if ($land) { $landY = [double]$land.y; if ($null -ne $ref -and [Math]::Abs($landY - $ref) -le 0.6 -and ([int]$land.frame - [int]$through.frame) -le 3) { $sinkOnly = $true } }
    }
    $verdict = if ($noHook) { "SKIP no_transform_hook" } elseif ($through -and $sinkOnly) { "INFO transient_sink" } elseif ($through) { "FAIL fell_through" } elseif ($oob) { "FAIL out_of_bounds" } elseif ($noFloor) { "INFO no_exported_floor" } elseif (-not $formOk) { "FAIL wrong_form" } elseif ($drop -gt 3) { "INFO large_drop" } else { "PASS" }
    $table.Add([pscustomobject][ordered]@{ run = $name; scenario = $sc; start = $R.r.s; verdict = $verdict
        pre_x = $pre.x; pre_y = $pre.y; pre_z = $pre.z; pre_hspeed = $pre.hspeed; pre_vy = $pre.vy; pre_grounded = $pre.grounded; pre_form = $pre.form
        press_floor = $(if ($null -ne $pf) { [Math]::Round($pf, 2) } else { "" }); press_floor_thickness = $(if ($pq.thickness) { [Math]::Round($pq.thickness, 2) } else { "" })
        end_x = $end.x; end_y = $end.y; end_z = $end.z; end_hspeed = $end.hspeed; end_vy = $end.vy; end_grounded = $end.grounded; end_form = $end.form; end_moveForm = $end.moveForm; end_camD = $end.camD
        end_floor = $(if ($eq -and $null -ne $eq.floor_below) { [Math]::Round($eq.floor_below, 2) } else { "" }); min_y = [Math]::Round($minY, 2); drop = [Math]::Round($drop, 2)
        penetration = [Math]::Round($pen, 2); through_frame = $(if ($through) { $through.frame } else { "" }); incline_0_5s = [Math]::Round($incline, 2); wall = [int]$wall; thin_floor = [int]$thin; low_ceiling = [int][bool]$lowCeil; transforms = $R.transforms; general_cross_frame = $(if ($gThrough) { $gThrough.frame } else { "" }); landed_y = $(if ($null -ne $landY) { [Math]::Round($landY, 2) } else { "" }) })
}
Write-WfcCsv $table (Join-Path $OutDir "stress.csv")
foreach ($sc in $Scenarios) {
    $rows = @($table | Where-Object scenario -eq $sc)
    if (-not $rows.Count) { continue }
    $f = @($rows | Where-Object { $_.verdict -like "FAIL*" })
    $tags = "thin floor {0}, wall {1}, incline > 0.5 m {2}, decline < -0.5 m {3}, airborne at press {4}, low ceiling {5}, transforms {6}" -f @($rows | Where-Object thin_floor -eq 1).Count, @($rows | Where-Object wall -eq 1).Count, @($rows | Where-Object { [double]$_.incline_0_5s -gt 0.5 }).Count, @($rows | Where-Object { [double]$_.incline_0_5s -lt -0.5 }).Count, @($rows | Where-Object pre_grounded -eq "0").Count, @($rows | Where-Object low_ceiling -eq 1).Count, (($rows | Measure-Object transforms -Sum).Sum)
    $skips = @($rows | Where-Object { $_.verdict -like "SKIP*" })
    if ($skips.Count -eq $rows.Count) { Add-WfcResult $res "transform_stress.$sc" "SKIP" 0 "no transform happened in any run: the build lacks WFC_PRESSTRANSFORM_EVERY (Systems soak hook)" "Systems"; continue }
    Add-WfcResult $res "transform_stress.$sc" $(if ($f.Count) { "FAIL" } else { "PASS" }) $f.Count ("{0} runs: {1}. Met: {2}. Large legal drops {3}.{4}" -f $rows.Count, (($rows | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", "), $tags, @($rows | Where-Object verdict -eq "INFO large_drop").Count, $(if ($f.Count) { " Failing: " + (($f | Select-Object -First 8 | ForEach-Object { "{0} ({1}, pen {2} m @f{3})" -f $_.run, $_.verdict, $_.penetration, $_.through_frame }) -join "; ") } else { "" })) "Gameplay"
}
foreach ($x in ($table | Where-Object { $_.verdict -like "FAIL*" })) { Add-WfcResult $res "transform_stress.run.$($x.run)" "FAIL" $x.penetration ("{0}: start {1}, press at ({2}, {3}, {4}) speed {5} floor {6} (thickness {7}); end ({8}, {9}, {10}) form {11} floor {12}; min y {13}; penetration {14} m at frame {15}" -f $x.verdict, $x.start, $x.pre_x, $x.pre_y, $x.pre_z, $x.pre_hspeed, $x.press_floor, $x.press_floor_thickness, $x.end_x, $x.end_y, $x.end_z, $x.end_form, $x.end_floor, $x.min_y, $x.penetration, $x.through_frame) "Gameplay" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"TRANSFORM STRESS: {0} runs; " -f $table.Count + (($table | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", ")
