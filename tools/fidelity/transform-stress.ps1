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
      [string[]]$Starts = @(), [string[]]$Scenarios = @(), [switch]$AnalyzeOnly)
$ErrorActionPreference = "Stop"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })   # -File passes "a,b" as one string
$Starts = @($Starts | ForEach-Object { $_ -split "," } | Where-Object { $_ } | ForEach-Object { [int]$_ }); if (-not $Starts.Count) { $Starts = @(0..83) }
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$colGlb = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets\collision_pawn.glb"
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
}
if (-not $Scenarios.Count) { $Scenarios = @($defs.Keys) }
# ---- 1. runs (resumable: a run with trace.csv is skipped) ----
$runsDir = Join-Path $OutDir "runs"; New-Item -ItemType Directory -Force $runsDir | Out-Null
$todo = @(); foreach ($sc in $Scenarios) { foreach ($s in $Starts) { $todo += @{ sc = $sc; s = $s } } }
$n = 0
foreach ($r in $todo) {
    $n++; $name = "{0}_s{1:D2}" -f $r.sc, $r.s; $dir = Join-Path $runsDir $name; $tr = Join-Path $dir "trace.csv"
    if ($AnalyzeOnly -or (Test-Path $tr)) { continue }
    $d = $defs[$r.sc]
    $e = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_START = "$($r.s)"; WFC_PRESSTRANSFORM = "$($d.press)" } + $d.env
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
    $pre = $t | Where-Object { [int]$_.frame -eq $press - 1 } | Select-Object -First 1; if (-not $pre) { $pre = $t[[Math]::Min($press - 2, $t.Count - 1)] }
    $runs[$name] = @{ r = $r; t = $t; pre = $pre }
    $queries.Add(@{ id = "$name|press"; x = [double]$pre.x; y = [double]$pre.y; z = [double]$pre.z })
    foreach ($f in ($t | Where-Object { [int]$_.frame -ge $press -and ([int]$_.frame % 3 -eq 0 -or [int]$_.frame -eq [int]$t[-1].frame) })) {
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
    $post = @($t | Where-Object { [int]$_.frame -ge $d.press })
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
            if ($null -ne $lvl.floor_below -and [Math]::Abs($lvl.floor_below - $pf) -le 0.6) { $through = $cross; $pen = $lvl.floor_below - $minY }
        }
    }
    $eq = $Q["$name|now|$($end.frame)"]
    $oob = $minY -lt -749 -or ($eq -and $null -eq $eq.floor_below)
    $drop = if ($null -ne $pf -and $eq -and $null -ne $eq.floor_below) { $pf - $eq.floor_below } else { 0 }
    $formOk = $end.form -eq $d.to -or $end.anim -like "Transform_*"
    $thin = $pq -and $null -ne $pq.thickness -and $pq.thickness -lt 0.5
    $back = @($t | Where-Object { [int]$_.frame -ge $d.press - 30 -and [int]$_.frame -lt $d.press })
    $incline = if ($back.Count -ge 2) { ([double]$back[-1].y - [double]$back[0].y) } else { 0 }
    $wall = ([double]$pre.hspeed -gt 4) -and (@($post | Select-Object -First 30 | Where-Object { [double]$_.hspeed -lt 0.5 }).Count -gt 0)
    $verdict = if ($through) { "FAIL fell_through" } elseif ($oob) { "FAIL out_of_bounds" } elseif (-not $formOk) { "FAIL wrong_form" } elseif ($drop -gt 3) { "INFO large_drop" } else { "PASS" }
    $table.Add([pscustomobject][ordered]@{ run = $name; scenario = $sc; start = $R.r.s; verdict = $verdict
        pre_x = $pre.x; pre_y = $pre.y; pre_z = $pre.z; pre_hspeed = $pre.hspeed; pre_vy = $pre.vy; pre_grounded = $pre.grounded; pre_form = $pre.form
        press_floor = $(if ($null -ne $pf) { [Math]::Round($pf, 2) } else { "" }); press_floor_thickness = $(if ($pq.thickness) { [Math]::Round($pq.thickness, 2) } else { "" })
        end_x = $end.x; end_y = $end.y; end_z = $end.z; end_hspeed = $end.hspeed; end_vy = $end.vy; end_grounded = $end.grounded; end_form = $end.form; end_moveForm = $end.moveForm; end_camD = $end.camD
        end_floor = $(if ($eq -and $null -ne $eq.floor_below) { [Math]::Round($eq.floor_below, 2) } else { "" }); min_y = [Math]::Round($minY, 2); drop = [Math]::Round($drop, 2)
        penetration = [Math]::Round($pen, 2); through_frame = $(if ($through) { $through.frame } else { "" }); incline_0_5s = [Math]::Round($incline, 2); wall = [int]$wall; thin_floor = [int]$thin })
}
Write-WfcCsv $table (Join-Path $OutDir "stress.csv")
foreach ($sc in $Scenarios) {
    $rows = @($table | Where-Object scenario -eq $sc)
    if (-not $rows.Count) { continue }
    $f = @($rows | Where-Object { $_.verdict -like "FAIL*" })
    $tags = "thin floor {0}, wall {1}, incline > 0.5 m {2}, decline < -0.5 m {3}, airborne at press {4}" -f @($rows | Where-Object thin_floor -eq 1).Count, @($rows | Where-Object wall -eq 1).Count, @($rows | Where-Object { [double]$_.incline_0_5s -gt 0.5 }).Count, @($rows | Where-Object { [double]$_.incline_0_5s -lt -0.5 }).Count, @($rows | Where-Object pre_grounded -eq "0").Count
    Add-WfcResult $res "transform_stress.$sc" $(if ($f.Count) { "FAIL" } else { "PASS" }) $f.Count ("{0} runs: {1}. Met: {2}. Large legal drops {3}.{4}" -f $rows.Count, (($rows | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", "), $tags, @($rows | Where-Object verdict -eq "INFO large_drop").Count, $(if ($f.Count) { " Failing: " + (($f | Select-Object -First 8 | ForEach-Object { "{0} ({1}, pen {2} m @f{3})" -f $_.run, $_.verdict, $_.penetration, $_.through_frame }) -join "; ") } else { "" })) "Gameplay"
}
foreach ($x in ($table | Where-Object { $_.verdict -like "FAIL*" })) { Add-WfcResult $res "transform_stress.run.$($x.run)" "FAIL" $x.penetration ("{0}: start {1}, press at ({2}, {3}, {4}) speed {5} floor {6} (thickness {7}); end ({8}, {9}, {10}) form {11} floor {12}; min y {13}; penetration {14} m at frame {15}" -f $x.verdict, $x.start, $x.pre_x, $x.pre_y, $x.pre_z, $x.pre_hspeed, $x.press_floor, $x.press_floor_thickness, $x.end_x, $x.end_y, $x.end_z, $x.end_form, $x.end_floor, $x.min_y, $x.penetration, $x.through_frame) "Gameplay" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"TRANSFORM STRESS: {0} runs; " -f $table.Count + (($table | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", ")
