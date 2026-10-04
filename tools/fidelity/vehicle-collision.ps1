# Vehicle collision stress on the real exe, judged against the AUTHORED pawn collision world (independent of the
# product's own probes): does the truck pass through walls, low blocks, props, narrow gaps or map boundaries?
#
#   .\tools\fidelity\vehicle-collision.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Starts 0..83] [-Scenarios a,b]
#
# Every authored player start x scenarios (drive, boost, boost turning left / right, nitro, long boost with turn
# changes, reverse, strafe). Each run logs the pawn every frame (WFC_LOGEVERY=1, lockstep 60 Hz); collision_sweep.py
# sweeps every frame-to-frame step at 0.8 / 1.2 / 1.6 m above the actor's ground level (above legal 0.5 m kerb climbs,
# within the hull: Gameplay's truck hull spans root -0.35 .. +1.85 m) against wall-like authored collision faces
# (|normal.y| < 0.7). collision_query.py checks the end point is still inside the map.
#   FAIL passed_through - a step crossed an authored wall face (the collision actor is named)
#   FAIL out_of_bounds  - below KillZ or no authored floor under the end point
#   INFO stuck          - moving input but < 0.5 m travelled in the last 3 s (wedged; Gameplay CHAOS reports these too)
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir,
      [string[]]$Starts = @(), [string[]]$Scenarios = @(), [switch]$AnalyzeOnly, [double[]]$Heights = @(0.8, 1.2, 1.6))
$ErrorActionPreference = "Stop"
$Scenarios = @($Scenarios | ForEach-Object { $_ -split "," } | Where-Object { $_ })
$Starts = @($Starts | ForEach-Object { $_ -split "," } | Where-Object { $_ } | ForEach-Object { [int]$_ }); if (-not $Starts.Count) { $Starts = @(0..83) }
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$colGlb = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets\collision_pawn.glb"
$V = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1" }
$defs = [ordered]@{
    drive        = @{ frames = 600; env = $V }
    boost        = @{ frames = 600; env = $V + @{ WFC_AUTOBOOST = "1" } }
    boost_right  = @{ frames = 600; env = $V + @{ WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.5" } }
    boost_left   = @{ frames = 600; env = $V + @{ WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "-0.5" } }
    nitro        = @{ frames = 600; env = $V + @{ WFC_AUTOBOOST = "1"; WFC_AUTODASH = "90"; WFC_AUTODASH2 = "330" } }
    boost_cycle  = @{ frames = 900; env = $V + @{ WFC_AUTOBOOST_CYCLE = "45"; WFC_AUTOTURN = "0.2" } }
    reverse      = @{ frames = 480; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOBACK = "1"; WFC_AUTOTURN = "0.3" } }
    strafe_boost = @{ frames = 600; env = $V + @{ WFC_AUTOBOOST = "1"; WFC_AUTOSTRAFE = "1" } }
}
if (-not $Scenarios.Count) { $Scenarios = @($defs.Keys) }
$runsDir = Join-Path $OutDir "runs"; New-Item -ItemType Directory -Force $runsDir | Out-Null
$todo = @(); foreach ($sc in $Scenarios) { foreach ($s in $Starts) { $todo += @{ sc = $sc; s = $s } } }
$n = 0
foreach ($r in $todo) {
    $n++; $name = "{0}_s{1:D2}" -f $r.sc, $r.s; $dir = Join-Path $runsDir $name; $tr = Join-Path $dir "trace.csv"
    if ($AnalyzeOnly -or (Test-Path $tr)) { continue }
    $d = $defs[$r.sc]
    $e = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_START = "$($r.s)" } + $d.env
    if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
    $rc = Invoke-WfcExe $Exe $dir $e "run.log" 600
    $rows = foreach ($f in (Read-WfcFrames (Join-Path $dir "wfc.log"))) { [pscustomobject][ordered]@{ frame = $f.frame; x = $f.x; y = $f.y; z = $f.z; grounded = $f.grounded; form = $f.form; hspeed = $f.hspeed } }
    Write-WfcCsv @($rows) $tr
    Remove-Item (Join-Path $dir "wfc.log")
    if ($n % 25 -eq 0) { Write-Host ("[{0}/{1}] {2} exit {3}" -f $n, $todo.Count, $name, $rc) }
}
# ---- sweep + end-point floors ----
$paths = New-Object System.Collections.Generic.List[object]; $queries = New-Object System.Collections.Generic.List[object]; $runs = @{}
foreach ($r in $todo) {
    $name = "{0}_s{1:D2}" -f $r.sc, $r.s; $tr = Join-Path (Join-Path $runsDir $name) "trace.csv"
    if (-not (Test-Path $tr)) { continue }
    $t = @(Import-Csv $tr); if ($t.Count -lt 10) { $runs[$name] = @{ broken = $true; r = $r }; continue }
    $veh = @($t | Where-Object form -eq "VEHICLE")
    $pts = New-Object System.Collections.Generic.List[object]; $lastKey = ""
    foreach ($f in $veh) { $k = "$($f.x),$($f.y),$($f.z)"; if ($k -ne $lastKey) { $pts.Add(@([int]$f.frame, [double]$f.x, [double]$f.y, [double]$f.z)); $lastKey = $k } }
    $paths.Add(@{ id = $name; heights = $Heights; pts = $pts.ToArray() })
    $queries.Add(@{ id = "$name|end"; x = [double]$t[-1].x; y = [double]$t[-1].y; z = [double]$t[-1].z })
    $runs[$name] = @{ r = $r; t = $t; vehFrames = $veh.Count }
}
$pf = Join-Path $OutDir "paths.json"; $po = Join-Path $OutDir "sweep_out.json"; $qf = Join-Path $OutDir "queries.json"; $qo = Join-Path $OutDir "queries_out.json"
[IO.File]::WriteAllText($pf, ($paths | ConvertTo-Json -Compress -Depth 5)); [IO.File]::WriteAllText($qf, ($queries | ConvertTo-Json -Compress -Depth 3))
Push-Location $PSScriptRoot
try { & $py "collision_sweep.py" $colGlb $pf $po | Out-Host; & $py "collision_query.py" $colGlb $qf $qo | Out-Host } finally { Pop-Location }
$S = Get-Content -Raw $po | ConvertFrom-Json; $Q = @{}; foreach ($o in (Get-Content -Raw $qo | ConvertFrom-Json)) { $Q[$o.id] = $o }
$X = @{}; foreach ($c in $S.crossings) { if (-not $X.ContainsKey($c.id)) { $X[$c.id] = @() }; $X[$c.id] += $c }
# ---- classification ----
$res = New-WfcResults; $table = New-Object System.Collections.Generic.List[object]
foreach ($name in ($runs.Keys | Sort-Object)) {
    $R = $runs[$name]; $sc = $R.r.sc
    if ($R.broken) { $table.Add([pscustomobject][ordered]@{ run = $name; scenario = $sc; start = $R.r.s; verdict = "TOOL" }); continue }
    $t = $R.t; $end = $t[-1]; $eq = $Q["$name|end"]
    $cr = @($X[$name] | Where-Object { $_ })
    $minY = ($t | ForEach-Object { [double]$_.y } | Measure-Object -Minimum).Minimum
    $oob = $minY -lt -749 -or ($eq -and $null -eq $eq.floor_below)
    $tail = @($t | Where-Object { [int]$_.frame -gt [int]$end.frame - 180 })
    $travel = if ($tail.Count -ge 2) { [Math]::Sqrt([Math]::Pow([double]$tail[-1].x - [double]$tail[0].x, 2) + [Math]::Pow([double]$tail[-1].z - [double]$tail[0].z, 2)) } else { 0 }
    $dist = 0.0; for ($k = 1; $k -lt $t.Count; $k++) { $dist += [Math]::Sqrt([Math]::Pow([double]$t[$k].x - [double]$t[$k - 1].x, 2) + [Math]::Pow([double]$t[$k].z - [double]$t[$k - 1].z, 2)) }
    $maxSp = ($t | ForEach-Object { [double]$_.hspeed } | Measure-Object -Maximum).Maximum
    $verdict = if ($cr.Count) { "FAIL passed_through" } elseif ($oob) { "FAIL out_of_bounds" } elseif ($travel -lt 0.5) { "INFO stuck" } else { "PASS" }
    $first = if ($cr.Count) { $cr[0] } else { $null }
    $table.Add([pscustomobject][ordered]@{ run = $name; scenario = $sc; start = $R.r.s; verdict = $verdict; distance_m = [Math]::Round($dist, 1); max_speed = $maxSp
        crossings = $cr.Count; first_frame = $(if ($first) { $first.frame }); first_h = $(if ($first) { $first.h }); actor = $(if ($first) { $first.node }); at = $(if ($first) { "{0},{1},{2}" -f $first.x, $first.y, $first.z })
        actors = (($cr | ForEach-Object { $_.node } | Select-Object -Unique) -join ";"); end_x = $end.x; end_y = $end.y; end_z = $end.z; min_y = [Math]::Round($minY, 2); last3s_m = [Math]::Round($travel, 2) })
}
Write-WfcCsv $table (Join-Path $OutDir "vehicle_collision.csv")
foreach ($sc in $Scenarios) {
    $rows = @($table | Where-Object scenario -eq $sc); if (-not $rows.Count) { continue }
    $f = @($rows | Where-Object { $_.verdict -like "FAIL*" })
    $km = [Math]::Round((($rows | Measure-Object distance_m -Sum).Sum) / 1000, 2)
    Add-WfcResult $res "vehicle_collision.$sc" $(if ($f.Count) { "FAIL" } else { "PASS" }) $f.Count ("{0} runs, {1} km driven: {2}.{3}" -f $rows.Count, $km, (($rows | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", "), $(if ($f.Count) { " Failing: " + (($f | Select-Object -First 8 | ForEach-Object { "{0} through {1} at ({2}) h {3} m @f{4}" -f $_.run, $_.actor, $_.at, $_.first_h, $_.first_frame }) -join "; ") } else { "" })) "Gameplay"
}
$byActor = @($table | Where-Object { $_.verdict -eq "FAIL passed_through" } | ForEach-Object { $_.actors -split ";" } | Group-Object | Sort-Object Count -Descending)
foreach ($g in $byActor) { Add-WfcResult $res "vehicle_collision.actor.$($g.Name)" "FAIL" $g.Count ("{0} runs passed through authored collision actor {1}" -f $g.Count, $g.Name) "Gameplay" }
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"VEHICLE COLLISION: {0} runs; " -f $table.Count + (($table | Group-Object verdict | ForEach-Object { "$($_.Name) $($_.Count)" }) -join ", ") + "; actors: " + (($byActor | Select-Object -First 6 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", ")
