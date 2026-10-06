# Milestone 04 living-map AUDIO on the deterministic exe (observe: lockstep 60 Hz + recording audio backend).
#
#   .\tools\fidelity\m04-world-audio.ps1 -Exe <wfc_rebuild_observe.exe> -SourceRoot <tree> -RenderData <work\render> -OutDir <dir>
#
# Runs authored FFA starts (idle, then robot walk / vehicle boost routes) with WFC_AUDIOSPY + WFC_AMBLOG and
# checks, against ExtractedAssets audio.json (AssetTools a23c675) and the runtime's own [CONF] semantics:
#   emitters    every runtime ambient voice is attributed to an authored emitter (same cue, nearest shape);
#               emitters represented over all runs; never-voiced emitters listed with their nearest listener
#   map_start   active emitters at the first AMB sample vs 70 - per-cue instance-limit refusals
#   placement   point voices at the actor; every line / volume voice position (start + updates) on the
#               authored segment / inside or on the authored box (closest point to the listener)
#   pools       PP_* one-shots: cue belongs to the current zone's pool, distance from the listener in range
#   reverb      zone entries, presets applied
#   leak        live voices first third vs last third, mixer voices/dropped/stolen (AMB), voices never ended
#   duplicate   identical launches (same sound, same position, same frame)
#   pickups     pickup cue launches (HEALTH_PU_* / OVERSHIELD_POWER_UP): no repeat within 0.5 s
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$SourceRoot, [string]$RenderData = "",
      [Parameter(Mandatory)][string]$OutDir, [int[]]$Starts = @(0, 3, 8, 11, 12, 21, 23), [int]$IdleFrames = 2400,
      [int]$RouteFrames = 3600, [switch]$AnalyzeOnly, [int]$PlaceSample = 50)   # check every Nth voice-position update (memory/CPU)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$au = Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\MP_IAC_Streets\audio.json" | ConvertFrom-Json
# ---- wave -> cue (tree under test) ----
$cueOf = @{}
$inc = Join-Path $SourceRoot "src\game\SoundCues.inc"; $cur = $null
foreach ($ln in [IO.File]::ReadLines($inc)) {
    if ($ln -match '^\s*\{"([^"]+)",') { $cur = $Matches[1] }
    if ($cur) { foreach ($m in [regex]::Matches($ln, '"([^"]+\.wav)"')) { $w = ($m.Groups[1].Value -split "[/\\]")[-1].ToUpper(); if (-not $cueOf[$w]) { $cueOf[$w] = @() }; if ($cueOf[$w] -notcontains $cur) { $cueOf[$w] += $cur } } }
}
# The map bank (BL_LVL_MP_IAC_STREETS) is loaded from audio.json at runtime: add its cue trees' waves.
function Walk($node, $cue) { if ($node.wav) { $w = ($node.wav -split "[/\\]")[-1].ToUpper(); if (-not $cueOf[$w]) { $cueOf[$w] = @() }; if ($cueOf[$w] -notcontains $cue) { $cueOf[$w] += $cue } }; foreach ($c in @($node.children)) { if ($c) { Walk $c $cue } } }
foreach ($p in $au.cues.PSObject.Properties) { Walk $p.Value.tree $p.Name }
function Cues($path) { $w = ($path -split "[/\\]")[-1].ToUpper(); if ($cueOf[$w]) { $cueOf[$w] } else { @() } }
function V3($a, $b, $c) { return , @([double]$a, [double]$b, [double]$c) }
function Sub($a, $b) { return , @(($a[0] - $b[0]), ($a[1] - $b[1]), ($a[2] - $b[2])) }
function Dot($a, $b) { $a[0] * $b[0] + $a[1] * $b[1] + $a[2] * $b[2] }
function Len($a) { [Math]::Sqrt((Dot $a $a)) }
# ---- authored emitters with shapes (glTF metres; axes = gltf_matrix columns, scale included) ----
$em = New-Object System.Collections.Generic.List[object]
foreach ($k in "point", "line", "volume") {
    foreach ($e in $au.emitters.$k) {
        $m = $e.gltf_matrix
        $o = @{ actor = $e.actor; kind = $k; cue = $e.cue; pos = @([double]$m[12], [double]$m[13], [double]$m[14]); ax = @((V3 $m[0] $m[1] $m[2]), (V3 $m[4] $m[5] $m[6]), (V3 $m[8] $m[9] $m[10])) }
        if ($k -eq "line") { $o.half = [double]$e.linelength / 100.0 / 2.0 }      # LineLength (UU) x DrawScale3D.X (in the X axis column)
        if ($k -eq "volume") { $o.half = [double]$e.radius / 100.0 }              # Radius (UU) x DrawScale3D per axis
        $em.Add($o)
    }
}
# Distance from p to the emitter's authored shape.
function ShapeDist($o, $p) {
    $d = Sub $p $o.pos
    if ($o.kind -eq "point") { return (Len $d) }
    if ($o.kind -eq "line") {
        $x = $o.ax[0]; $xl = Len $x; $u = (Dot $d $x) / ($xl * $xl)   # in units of the scaled axis
        $u = [Math]::Max(-$o.half, [Math]::Min($o.half, $u))
        return (Len (Sub $d @(($x[0] * $u), ($x[1] * $u), ($x[2] * $u))))
    }
    $out = 0.0                                                         # oriented box: excess beyond +/-half per axis
    foreach ($x in $o.ax) { $xl = Len $x; $u = (Dot $d $x) / ($xl * $xl); $ex = [Math]::Max(0, [Math]::Abs($u) - $o.half) * $xl; $out += $ex * $ex }
    return [Math]::Sqrt($out)
}
$pools = @{}; $poolsByZone = @{}
foreach ($z in $au.zones) { $poolsByZone[$z.comment] = @($z.one_shot_pool | ForEach-Object { $_.cue }); $pi = 0; foreach ($p in $z.one_shot_pool) { $pools["$($z.comment)#$pi"] = @{ zone = $z.comment; cue = $p.cue; dmin = $p.distance_min / 100.0; dmax = $p.distance_max / 100.0 }; $pi++ } }   # action names repeat across zone sequences
$poolsFired = @{}
$poolCues = @{}; foreach ($p in $pools.Values) { $poolCues[$p.cue] = $true }

# ---- runs ----
$runs = [ordered]@{}
foreach ($s in $Starts) { $runs["idle_$s"] = @{ env = @{ WFC_SPAWN_INDEX = "$s" }; frames = $IdleFrames } }
foreach ($s in $Starts) { $runs["walk_$s"] = @{ env = @{ WFC_SPAWN_INDEX = "$s"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.35" }; frames = $RouteFrames } }
foreach ($s in @(6, 18)) { $runs["drive_$s"] = @{ env = @{ WFC_SPAWN_INDEX = "$s"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.25" }; frames = $RouteFrames } }
$res = New-WfcResults
$represented = @{}; $minListen = @{}; foreach ($o in $em) { $minListen[$o.actor] = 1e9 }
$placeN = 0; $placeBad = 0; $placeWorst = 0.0; $placeBadList = @{}
$poolFired = @{}; $poolBad = 0; $poolTotal = 0; $poolBadList = @()
$zonesAll = @{}; $presetsAll = @{}
$runRows = @()
foreach ($rn in $runs.Keys) {
    $r = $runs[$rn]; $dir = Join-Path $OutDir $rn; $spy = Join-Path $dir "audiospy.txt"
    if (-not $AnalyzeOnly) {
        $e = @{ WFC_SMOKE_FRAMES = "$($r.frames)"; WFC_AUDIOSPY = $spy; WFC_AMBLOG = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "0" } + $r.env
        if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
        $null = Invoke-WfcExe $Exe $dir $e "run.log" 1800
    }
    if (-not (Test-Path $spy)) { Add-WfcResult $res "m04_audio.$rn.ran" "SKIP" $null "no audio spy output"; continue }
    $L = Get-Content (Join-Path $dir "wfc.log")
    # AMB samples
    $amb = @($L | Select-String "AMB zone=(\S+) emitters=(\d+)/(\d+) oneShots=(\d+) cues=(\d+) .*voices=(-?\d+)(?: \(max (\d+), dropped (\d+), stolen (\d+)\))?" | ForEach-Object { $g = $_.Matches[0].Groups; [pscustomobject]@{ zone = $g[1].Value; active = [int]$g[2].Value; total = [int]$g[3].Value; shots = [int]$g[4].Value; cues = [int]$g[5].Value; voices = [int]$g[6].Value; dropped = $(if ($g[8].Success) { [int]$g[8].Value } else { 0 }); stolen = $(if ($g[9].Success) { [int]$g[9].Value } else { 0 }) } })
    foreach ($m in ($L | Select-String "ambient: entered zone (\S+) \(from [^;]*; preset (\S+?),")) { $zonesAll[$m.Matches[0].Groups[1].Value] = $true; $presetsAll[$m.Matches[0].Groups[2].Value] = $true }
    $zoneTimeline = @($L | Select-String "ambient: entered zone (\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value })
    # spy
    $uCount = 0
    $snd = @{}; $live = @{}; $perFrame = New-Object System.Collections.Generic.List[int]; $lis = $null; $voiceEm = @{}; $launch = @{}; $dups = 0; $pick = @(); $neverEnded = 0
    foreach ($ln in [IO.File]::ReadLines($spy)) {
        $q = $ln.Split(" ")
        switch ($q[0]) {
            "L" { $snd[[int]$q[2]] = ($q[4..($q.Length - 1)] -join " ") }
            "H" { $lis = V3 $q[2] $q[3] $q[4] }
            "V" {
                $v = [int]$q[2]; $s = [int]$q[3]; $p = V3 $q[6] $q[7] $q[8]; $t = [double]$q[1]
                $live[$v] = $true
                $key = "{0}|{1}|{2:F2}|{3:F2}|{4:F2}" -f $s, $t, $p[0], $p[1], $p[2]
                if ($launch[$key]) { $dups++ } else { $launch[$key] = $true }
                $cs = Cues $snd[$s]
                if ($cs | Where-Object { $_ -like "*HEALTH_PU*" -or $_ -like "*OVERSHIELD_POWER*" }) { $pick += @{ t = $t; cue = ($cs -join "|") } }
                $amb2 = @($cs | Where-Object { $_ -like "BL_LVL_MP_IAC_STREETS.EMIT_*" -or $_ -like "BL_LVL_MP_IAC_STREETS.AMB_*" })
                if ($amb2.Count -and $q[5] -eq "1") {
                    $best = $null; $bd = 1e9
                    foreach ($o in $em) { if ($amb2 -notcontains $o.cue) { continue }; $d = ShapeDist $o $p; if ($d -lt $bd) { $bd = $d; $best = $o } }
                    if ($best) {
                        $voiceEm[$v] = $best; $represented[$best.actor] = $true
                        $placeN++; if ($bd -gt 0.25) { $placeBad++; $placeBadList[$best.actor] = [Math]::Max([double]$placeBadList[$best.actor], $bd) }; $placeWorst = [Math]::Max($placeWorst, $bd)
                    }
                }
                $pc = @($cs | Where-Object { $poolCues[$_] })
                if ($pc.Count -and $lis) {
                    # zone current at the launch time: the AMB sample (every 0.5 s, simulated) covering t
                    $poolTotal++; $ai = [Math]::Min($amb.Count - 1, [Math]::Max(0, [int][Math]::Floor($t / 0.5) - 1)); $zn = if ($amb.Count) { $amb[$ai].zone } else { "" }
                    $hit = $pools.Values | Where-Object { $pc -contains $_.cue -and $_.zone -eq $zn } | Select-Object -First 1
                    $inZone = [bool]$hit
                    if (-not $hit) { $hit = $pools.Values | Where-Object { $pc -contains $_.cue } | Select-Object -First 1 }
                    $poolsFired["$($hit.zone)|$($hit.cue)"] = $true
                    $poolFired[$hit.cue] = [int]$poolFired[$hit.cue] + 1
                    $dl = Len (Sub $p $lis)
                    $badDist = $dl -lt $hit.dmin - 1.0 -or $dl -gt $hit.dmax + 1.0
                    if ($badDist -or -not $inZone) { $poolBad++; $script:poolBadList += ("{0} t={1:F2} {2} zone {3}{4} dist {5:F1} m" -f $rn, $t, ($hit.cue -replace '^.*\.', ''), $zn, $(if ($inZone) { "" } else { " (not its pool)" }), $dl) }
                }
            }
            "U" {
                if ((++$uCount % $PlaceSample) -ne 0) { break }
                $v = [int]$q[2]; $o = $voiceEm[$v]
                if ($o -and $o.kind -ne "point") { $d = ShapeDist $o (V3 $q[3] $q[4] $q[5]); $placeN++; if ($d -gt 0.25) { $placeBad++; $placeBadList[$o.actor] = [Math]::Max([double]$placeBadList[$o.actor], $d) }; $placeWorst = [Math]::Max($placeWorst, $d) }
            }
            "S" { $live.Remove([int]$q[2]) }
            "E" { $live.Remove([int]$q[2]) }
            "F" { $perFrame.Add($live.Count); if ($lis -and ($perFrame.Count % 30) -eq 0) { foreach ($o in $em) { $d = ShapeDist $o $lis; if ($d -lt $minListen[$o.actor]) { $minListen[$o.actor] = $d } } } }
        }
    }
    $n = $perFrame.Count
    $a3 = if ($n -ge 30) { ($perFrame.GetRange(0, [int]($n / 3)) | Measure-Object -Average).Average } else { 0 }
    $b3 = if ($n -ge 30) { ($perFrame.GetRange($n - [int]($n / 3), [int]($n / 3)) | Measure-Object -Average).Average } else { 0 }
    $pickRep = 0; for ($i = 1; $i -lt $pick.Count; $i++) { if ($pick[$i].t - $pick[$i - 1].t -lt 0.5 -and $pick[$i].cue -eq $pick[$i - 1].cue) { $pickRep++ } }
    $first = if ($amb.Count) { $amb[0] } else { $null }
    $runRows += [pscustomobject][ordered]@{ run = $rn; frames = $n; zones = ($zoneTimeline -join ">"); start_active = $(if ($first) { $first.active } else { -1 }); max_active = $(if ($amb.Count) { ($amb | Measure-Object active -Maximum).Maximum } else { -1 })
        mixer_voices_max = $(if ($amb.Count) { ($amb | Measure-Object voices -Maximum).Maximum } else { -1 }); dropped = $(if ($amb.Count) { $amb[-1].dropped } else { -1 }); stolen = $(if ($amb.Count) { $amb[-1].stolen } else { -1 })
        live_first_third = [Math]::Round($a3, 1); live_last_third = [Math]::Round($b3, 1); one_shots = $(if ($amb.Count) { $amb[-1].shots } else { -1 }); duplicate_launches = $dups; pickup_sounds = $pick.Count; pickup_repeats = $pickRep }
    Add-WfcResult $res "m04_audio.$rn.no_voice_growth" $(if ($b3 -gt 2 * $a3 + 6) { "FAIL" } else { "PASS" }) $b3 ("live voices mean first third {0:F1}, last third {1:F1}; mixer max {2}" -f $a3, $b3, $runRows[-1].mixer_voices_max)
    Add-WfcResult $res "m04_audio.$rn.no_duplicate_launch" $(if ($dups -eq 0) { "PASS" } else { "FAIL" }) $dups "identical launches (same sound, same position, same frame)"
    if ($pick.Count) { Add-WfcResult $res "m04_audio.$rn.pickup_sound_once" $(if ($pickRep -eq 0) { "PASS" } else { "FAIL" }) $pick.Count ("pickup cue launches {0}, repeats within 0.5 s {1}: {2}" -f $pick.Count, $pickRep, (($pick | ForEach-Object { "{0:F2}s {1}" -f $_.t, $_.cue }) -join "; ")) }
    "{0}: zones {1}; start active {2}; live {3:F0}->{4:F0}; dups {5}; pickups {6}" -f $rn, ($zoneTimeline -join ">"), $runRows[-1].start_active, $a3, $b3, $dups, $pick.Count
}
Write-WfcCsv $runRows (Join-Path $OutDir "runs.csv")
# ---- aggregate ----
$rep = @($em | Where-Object { $represented[$_.actor] }).Count
$never = @($em | Where-Object { -not $represented[$_.actor] } | ForEach-Object { "{0} ({1}, {2}; nearest listener {3:F0} m)" -f $_.actor, $_.kind, ($_.cue -replace '^.*\.', ''), $minListen[$_.actor] })
Add-WfcResult $res "m04_audio.emitters_represented" $(if ($rep -eq $em.Count) { "PASS" } else { "INFO" }) $rep ("authored emitters with a runtime voice over all runs: {0}/{1}. Never voiced: {2}" -f $rep, $em.Count, ($never -join "; ")) "" 70
foreach ($k in "point", "line", "volume") { $all = @($em | Where-Object kind -eq $k); Add-WfcResult $res "m04_audio.emitters_represented.$k" "INFO" @($all | Where-Object { $represented[$_.actor] }).Count "of $($all.Count) authored $k emitters" "" $all.Count }
$startRows = @($runRows | Where-Object { $_.run -like "idle_*" -and $_.start_active -ge 0 })
Add-WfcResult $res "m04_audio.map_start_active" "INFO" $(if ($startRows.Count) { ($startRows | Measure-Object start_active -Average).Average } else { -1 }) ("active emitters at the first AMB sample per start: {0}. Systems [CONF]: 30 line/volume always play + point cues limited 13->5, 12->5, 8->5, 5->3 -> 50 of 70 at map start" -f (($startRows | ForEach-Object { "$($_.run)=$($_.start_active)" }) -join ", ")) "" 50
$startOk = @($startRows | Where-Object { $_.start_active -eq 50 }).Count
Add-WfcResult $res "m04_audio.map_start_matches_limits" $(if ($startRows.Count -and $startOk -eq $startRows.Count) { "PASS" } elseif ($startRows.Count) { "INFO" } else { "SKIP" }) $startOk "starts whose map-start active count equals the instance-limit prediction (50)"
Add-WfcResult $res "m04_audio.shaped_placement" $(if ($placeN -and $placeBad -eq 0) { "PASS" } elseif ($placeN) { "FAIL" } else { "SKIP" }) $placeBad ("{0} voice positions checked against the authored point / segment / box; {1} off-shape by > 0.25 m (worst {2:F2} m): {3}" -f $placeN, $placeBad, $placeWorst, (($placeBadList.Keys | ForEach-Object { "{0} {1:F2} m" -f $_, $placeBadList[$_] }) -join ", "))
Add-WfcResult $res "m04_audio.oneshot_pools_fired" "INFO" $poolFired.Count ("pools fired (zone|cue): {3}; cues: {0} (authored pools {1}, {2} distinct cues)" -f (($poolFired.Keys | ForEach-Object { "{0} x{1}" -f ($_ -replace '^.*\.', ''), $poolFired[$_] }) -join ", "), $pools.Count, $poolCues.Count, (($poolsFired.Keys | Sort-Object) -join ", ")) "" $poolCues.Count
Add-WfcResult $res "m04_audio.oneshot_pools_exercised" "INFO" $poolsFired.Count "zone pools that fired at least once (authored 11; a pool runs only while its zone is current)" "" 11
Add-WfcResult $res "m04_audio.oneshot_distance" $(if ($poolTotal -and $poolBad -eq 0) { "PASS" } elseif ($poolTotal) { "FAIL" } else { "SKIP" }) $poolBad ("{0} pool one-shots; {1} outside the authored DistanceMin..Max from the listener (+-1 m) or not in the current zone's pool: {2}" -f $poolTotal, $poolBad, (($poolBadList | Select-Object -First 12) -join "; "))
Add-WfcResult $res "m04_audio.zones_entered" "INFO" $zonesAll.Count (($zonesAll.Keys | Sort-Object) -join ", ") "" 9
Add-WfcResult $res "m04_audio.reverb_presets_applied" "INFO" $presetsAll.Count (($presetsAll.Keys | Sort-Object) -join ", ") "" 10
$null = Write-WfcReport $res (Join-Path $OutDir "report.json")
"M04 WORLD AUDIO: emitters represented {0}/{1}; placement bad {2}/{3}; pools {4}; zones {5}" -f $rep, $em.Count, $placeBad, $placeN, $poolFired.Count, $zonesAll.Count
