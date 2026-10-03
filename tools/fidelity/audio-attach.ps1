# Audio spatial-attachment harness (no audio implementation change).
#
#   .\tools\fidelity\audio-attach.ps1                       # all scenarios -> work\fidelity\audio\
#   .\tools\fidelity\audio-attach.ps1 -Scenarios boost,nitro
#   .\tools\fidelity\audio-attach.ps1 -AnalyzeOnly          # re-analyze existing recordings
#
# Runs wfc_rebuild_observe (unmodified product sources; lockstep clock + Win32Audio replaced by the
# recorder in measure/SpyAudio.cpp, which also models isPlaying() from wave length / pitch). Every
# sound instance is joined with the owner (pawn) pose of the same frame from the exe's frame log:
#   cue (wave -> cue name from the tree's own src/game/SoundCues.inc), source position over time,
#   owner position, attachment (fixed playAt / updated voice / never-updated voice), owner-local
#   offset (socket estimate), loop, attenuation (min/max/rolloff or ref/max), listener distance,
#   SmartPan pan, modelled gain, lifetime.
# A pawn-owned sound (starts within 6 m of the pawn) STOPS FOLLOWING its owner when, while it plays,
# its distance to the pawn grows by > 2 m (> 3 m for position-updated voices) while the pawn moved > 2 m.
# IMPT_* cues are anchored at the hit point by design (world-owned).
# Presence checks: footsteps, transform cue, landing, fine-aim start/end, vehicle loops.
# Output: <scenario>/instances.csv, report.json (wfc_fidelity schema), offending_cues.txt.
param([string[]]$Scenarios = @("stationary_transform", "moving_transform", "robot_footsteps", "jump_land", "hover_move", "boost",
                               "dash", "nitro", "vehicle_jump", "firing_moving", "fine_aim", "sustained_fire", "vehicle_exit_loops"),
      [string]$Exe = "", [string]$OutDir = "", [string]$SourceRoot = "", [switch]$AnalyzeOnly)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_observe.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\audio" }
if (-not $SourceRoot) { $SourceRoot = $root }
if (-not $AnalyzeOnly -and -not (Test-Path $Exe)) { throw "measurement build missing: $Exe (cmake -DWFC_BUILD_MEASURE=ON; target wfc_rebuild_observe)" }
$defs = [ordered]@{
    stationary_transform = @{ frames = 260; env = @{ WFC_PRESSTRANSFORM = "60" } }
    moving_transform     = @{ frames = 320; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.6"; WFC_PRESSTRANSFORM = "60" } }
    robot_footsteps      = @{ frames = 420; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.6" } }
    jump_land            = @{ frames = 300; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOJUMP = "1" } }
    hover_move           = @{ frames = 420; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.6" } }
    boost                = @{ frames = 420; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.4" } }
    dash                 = @{ frames = 420; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTODASH = "120"; WFC_AUTODASH2 = "300"; WFC_AUTOTURN = "0.4" } }
    nitro                = @{ frames = 480; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "160"; WFC_AUTOTURN = "0.4" } }
    vehicle_jump         = @{ frames = 300; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOJUMP = "1" } }
    firing_moving        = @{ frames = 360; env = @{ WFC_AUTOWALK = "1"; WFC_AUTOFIRE = "1"; WFC_AUTOTURN = "0.6" } }
    fine_aim             = @{ frames = 260; env = @{ WFC_FINEAIM_ON = "60"; WFC_FINEAIM_OFF = "180" } }
    # 30 s of held fire (several magazines + auto-reloads): voice lifetime / accumulation.
    sustained_fire       = @{ frames = 1800; env = @{ WFC_AUTOFIRE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.3" } }
    # Boosting vehicle transforms to robot at frame 240 (4 s), then 4 s on foot: vehicle loops must end.
    vehicle_exit_loops   = @{ frames = 480; press = 240; env = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_PRESSTRANSFORM = "240" } }
}
# ---- wave -> cue names from the tree under test ----
$cueOf = @{}
$inc = Join-Path $SourceRoot "src\game\SoundCues.inc"
if (Test-Path $inc) {
    $cur = $null
    foreach ($ln in [IO.File]::ReadLines($inc)) {
        if ($ln -match '^\s*\{"([^"]+)",') { $cur = $Matches[1] }
        if ($cur) { foreach ($m in [regex]::Matches($ln, '"([^"]+\.wav)"')) { $w = ($m.Groups[1].Value -split "[/\\]")[-1].ToUpper(); if (-not $cueOf[$w]) { $cueOf[$w] = @() }; if ($cueOf[$w] -notcontains $cur) { $cueOf[$w] += $cur } } }
    }
}
function CueName($path) { $w = ($path -split "[/\\]")[-1].ToUpper(); if ($cueOf[$w]) { ($cueOf[$w] -join "|") } else { "(no cue) $w" } }
function V3($a, $b, $c) { return , @([double]$a, [double]$b, [double]$c) }
function Dist($a, $b) { [Math]::Sqrt(($a[0] - $b[0]) * ($a[0] - $b[0]) + ($a[1] - $b[1]) * ($a[1] - $b[1]) + ($a[2] - $b[2]) * ($a[2] - $b[2])) }

$all = New-WfcResults
$offending = New-Object System.Collections.Generic.List[string]
$played = @{}    # scenario -> set of cue names heard
foreach ($name in $Scenarios) {
    $d = $defs[$name]; $dir = Join-Path $OutDir $name
    $spy = Join-Path $dir "audiospy.txt"
    if (-not $AnalyzeOnly) {
        $envs = @{ WFC_SMOKE_FRAMES = "$($d.frames)"; WFC_LOGEVERY = "1"; WFC_NOMOUSE = "1"; WFC_AUDIOSPY = $spy } + $d.env
        $rc = Invoke-WfcExe $Exe $dir $envs
    } else { $rc = 0 }
    # ---- owner pose per frame ----
    $own = @{}; foreach ($f in (Read-WfcFrames (Join-Path $dir "wfc.log"))) { $own[[int]$f.frame] = $f }
    # ---- spy ----
    $snd = @{}; $inst = [ordered]@{}; $lis = @{}; $frame = 1; $lastH = $null; $nA = 0
    foreach ($ln in [IO.File]::ReadLines($spy)) {
        $q = $ln.Split(" ")
        switch ($q[0]) {
            "L" { $snd[[int]$q[2]] = @{ dur = [double]$q[3]; path = ($q[4..($q.Length - 1)] -join " ") } }
            "H" { $lastH = $q }
            "F" { if ($lastH) { $lis[[int]$q[2]] = $lastH }; $frame = [int]$q[2] + 1 }
            "A" { $nA++; $inst["A$nA"] = @{ kind = "fixed playAt"; f0 = $frame; s = [int]$q[2]; p0 = (V3 $q[3] $q[4] $q[5]); vol = [double]$q[6]; minD = [double]$q[7]; maxD = [double]$q[8]; roll = 1.0; loop = 0; pos = 1; upd = (New-Object System.Collections.Generic.List[object]); f1 = $null; pan2 = 2.0; pan3 = 4.0 } }
            "V" { $inst["V$($q[2])"] = @{ kind = "voice"; f0 = $frame; s = [int]$q[3]; pos = [int]$q[4]; loop = [int]$q[5]; p0 = (V3 $q[6] $q[7] $q[8]); minD = [double]$q[9]; maxD = [double]$q[10]
                                           vol = $(if ($q.Length -gt 11) { [double]$q[11] } else { 1 }); roll = $(if ($q.Length -gt 13) { [double]$q[13] } else { 1 })
                                           pan2 = $(if ($q.Length -gt 14) { [double]$q[14] } else { 2 }); pan3 = $(if ($q.Length -gt 15) { [double]$q[15] } else { 4 })
                                           upd = (New-Object System.Collections.Generic.List[object]); f1 = $null } }
            "U" { $v = $inst["V$($q[2])"]; if ($v) { $v.upd.Add(@{ f = $frame; p = (V3 $q[3] $q[4] $q[5]); vol = [double]$q[6] }) } }
            { $_ -eq "S" -or $_ -eq "E" } { $v = $inst["V$($q[2])"]; if ($v -and -not $v.f1) { $v.f1 = $frame } }
        }
    }
    $lastFrame = $frame
    $rows = New-Object System.Collections.Generic.List[object]
    $played[$name] = @{}
    foreach ($k in $inst.Keys) {
        $x = $inst[$k]
        $info = $snd[$x.s]; $path = if ($info) { $info.path } else { "sound$($x.s)" }; $dur = if ($info) { $info.dur } else { -1 }
        $cue = CueName $path
        foreach ($c in ($cue -split "\|")) { $played[$name][$c] = $true }
        if (-not $x.pos) { continue }
        $f0 = $x.f0
        $f1 = if ($x.f1) { $x.f1 } elseif ($x.loop) { $lastFrame } else { [Math]::Min($lastFrame, $f0 + [int][Math]::Ceiling([Math]::Max($dur, 0) * 60)) }
        $o0 = $own[$f0]; if (-not $o0) { continue }
        $own0 = (V3 $o0.x $o0.y $o0.z)
        $d0 = Dist $x.p0 $own0
        $yaw = [double]$o0.yaw
        # owner-local offset (x right, z forward in the pawn frame; yaw 0 faces -Z)
        $dx = $x.p0[0] - $own0[0]; $dz = $x.p0[2] - $own0[2]
        $fx = -[Math]::Sin($yaw); $fz = -[Math]::Cos($yaw)
        $localF = $dx * $fx + $dz * $fz; $localR = $dx * (-$fz) + $dz * $fx; $localU = $x.p0[1] - $own0[1]
        # follow test over the lifetime
        $maxGrow = 0; $maxMove = 0; $lostAt = $null
        # Updated (socket-attached) voices may swing ~1-2 m relative to the root (muzzle on a turning,
        # running robot): 3 m tolerance; fixed / never-updated sounds: 2 m.
        $thr = if ($x.upd.Count -gt 0) { 3.0 } else { 2.0 }
        $ui = 0; $src = $x.p0
        for ($f = $f0; $f -le $f1; $f++) {
            while ($ui -lt $x.upd.Count -and $x.upd[$ui].f -le $f) { $src = $x.upd[$ui].p; $ui++ }
            $o = $own[$f]; if (-not $o) { continue }
            $op = (V3 $o.x $o.y $o.z)
            $mv = Dist $op $own0
            $grow = (Dist $src $op) - $d0
            if ($mv -gt $maxMove) { $maxMove = $mv }
            if ($grow -gt $maxGrow) { $maxGrow = $grow }
            if (-not $lostAt -and $grow -gt $thr -and $mv -gt 2.0) { $lostAt = $f }
        }
        # Impacts (IMPT_*) are anchored at the hit point by design: world-owned even when the hit is near.
        $owner = if ($cue -match "^IMPT") { "world-impact" } elseif ($d0 -lt 6.0) { "pawn" } else { "world" }
        $attach = if ($x.kind -eq "fixed playAt") { "fixed" } elseif ($x.upd.Count -gt 0) { "updated" } else { "never-updated" }
        # listener distance / pan / gain at start
        $L = $lis[$f0]; $dist = -1; $pan = 0; $gain = -1
        if ($L) {
            $lp = (V3 $L[2] $L[3] $L[4]); $lr = (V3 $L[8] $L[9] $L[10])
            $dist = Dist $x.p0 $lp
            if ($dist -gt 1e-3) {
                $side = (($x.p0[0] - $lp[0]) * $lr[0] + ($x.p0[1] - $lp[1]) * $lr[1] + ($x.p0[2] - $lp[2]) * $lr[2]) / $dist
                $amt = [Math]::Min(1, [Math]::Max(0, ($dist - $x.pan2) / [Math]::Max(1e-3, $x.pan3 - $x.pan2)))
                $pan = $side * $amt
            }
            $gain = if ($dist -le $x.minD) { $x.vol } elseif ($dist -ge $x.maxD) { $x.vol * $x.minD / ($x.minD + $x.roll * ($x.maxD - $x.minD)) } else { $x.vol * $x.minD / ($x.minD + $x.roll * ($dist - $x.minD)) }
        }
        $rows.Add([pscustomobject][ordered]@{
            instance = $k; cue = $cue; wave = (Split-Path $path -Leaf); kind = $x.kind; attachment = $attach; owner = $owner; loop = $x.loop
            start_frame = $f0; end_frame = $f1; lifetime_s = [Math]::Round(($f1 - $f0) / 60.0, 3); wave_s = $dur
            src_x = $x.p0[0]; src_y = $x.p0[1]; src_z = $x.p0[2]; owner_x = $own0[0]; owner_y = $own0[1]; owner_z = $own0[2]
            offset_m = [Math]::Round($d0, 3); local_fwd = [Math]::Round($localF, 3); local_right = [Math]::Round($localR, 3); local_up = [Math]::Round($localU, 3)
            updates = $x.upd.Count; min_dist = $x.minD; max_dist = $x.maxD; rolloff = $x.roll; volume = $x.vol
            listener_dist = [Math]::Round($dist, 3); pan = [Math]::Round($pan, 3); gain = [Math]::Round($gain, 4)
            owner_moved_m = [Math]::Round($maxMove, 3); max_offset_growth_m = [Math]::Round($maxGrow, 3)
            stops_following = [int]($owner -eq "pawn" -and $null -ne $lostAt); lost_at_frame = $lostAt })
    }
    Write-WfcCsv $rows (Join-Path $dir "instances.csv")
    # ---- per-cue results ----
    $id = "audio_attach.$name"
    Add-WfcResult $all "$id.ran" $(if ($rc -eq 0) { "PASS" } else { "FAIL" }) $rows.Count "positional instances analysed (exit $rc)"
    foreach ($g in ($rows | Where-Object owner -eq "pawn" | Group-Object wave)) {
        $cands = ($g.Group.cue | Sort-Object -Unique) -join " / "
        $bad = @($g.Group | Where-Object stops_following -eq 1)
        $moved = ($g.Group | Measure-Object owner_moved_m -Maximum).Maximum
        $att = ($g.Group.attachment | Sort-Object -Unique) -join "/"
        if ($bad.Count) {
            $worst = ($bad | Measure-Object max_offset_growth_m -Maximum).Maximum
            Add-WfcResult $all "$id.wave.$($g.Name)" "KNOWN" $worst ("cue {5}: {0} of {1} instance(s) stop following the pawn ({2}); offset grew up to {3:F1} m while the pawn moved {4:F1} m" -f $bad.Count, $g.Count, $att, $worst, $moved, $cands) "Systems" $null "m"
            $offending.Add(("{0}`t{1}`t{2}`t{3}`t{4:F1} m" -f $name, $cands, $g.Name, $att, $worst))
        } elseif ($moved -gt 2.0) {
            Add-WfcResult $all "$id.wave.$($g.Name)" "PASS" $moved ("cue {3}: {0} instance(s) follow the pawn ({1}) while it moved {2:F1} m" -f $g.Count, $att, $moved, $cands) "" $null "m"
        } else {
            Add-WfcResult $all "$id.wave.$($g.Name)" "INFO" $moved ("cue {2}: {0} instance(s) ({1}); pawn moved < 2 m while they played - attachment not testable here" -f $g.Count, $att, $cands) "" $null "m"
        }
    }
    "{0}: {1} instances, {2} pawn-owned, {3} stop following" -f $name, $rows.Count, @($rows | Where-Object owner -eq "pawn").Count, @($rows | Where-Object stops_following -eq 1).Count
}
# ---- voice lifetime (every scenario): live voices per frame from the spy (V start, S stop, E end of wave) ----
foreach ($name in $Scenarios) {
    $spy = Join-Path (Join-Path $OutDir $name) "audiospy.txt"
    if (-not (Test-Path $spy)) { continue }
    $live = @{}; $perFrame = New-Object System.Collections.Generic.List[int]; $loopStart = @{}; $paths = @{}
    foreach ($ln in [IO.File]::ReadLines($spy)) {
        $q = $ln.Split(" ")
        switch ($q[0]) {
            "L" { $paths[[int]$q[2]] = ($q[4..($q.Length - 1)] -join " ") }
            "V" { $live[[int]$q[2]] = @{ loop = ($q[5] -eq "1"); t = [double]$q[1]; snd = [int]$q[3]; pos = (V3 $q[6] $q[7] $q[8]) } }
            "U" { $v = $live[[int]$q[2]]; if ($v) { $v.pos = (V3 $q[3] $q[4] $q[5]) } }
            "S" { $live.Remove([int]$q[2]) }
            "E" { $live.Remove([int]$q[2]) }
            "F" { $perFrame.Add($live.Count) }
        }
    }
    $n = $perFrame.Count; if ($n -lt 30) { continue }
    $a = $perFrame.GetRange(0, [int]($n / 3)); $b = $perFrame.GetRange($n - [int]($n / 3), [int]($n / 3))
    $meanA = ($a | Measure-Object -Average).Average; $meanB = ($b | Measure-Object -Average).Average
    $maxAll = ($perFrame | Measure-Object -Maximum).Maximum
    $endLoops = @($live.Values | Where-Object { $_.loop })
    $id = "audio_life.$name"
    # Accumulation = the last third of the run holds clearly more live voices than the first third.
    $grow = $meanB -gt 2 * $meanA + 4
    Add-WfcResult $all "$id.no_voice_accumulation" $(if ($grow) { "FAIL" } else { "PASS" }) $meanB ("mean live voices first third {0:F1}, last third {1:F1}, max {2} over {3} frames" -f $meanA, $meanB, $maxAll, $n)
    Add-WfcResult $all "$id.loops_live_at_end" "INFO" $endLoops.Count (($endLoops | ForEach-Object { ($paths[$_.snd] -split "[/\\]")[-1] + "@" + $_.t }) -join ", ")
    if ($defs[$name].press) {
        $tp = ($defs[$name].press - 1) / 60.0
        # Player-owned = the voice sits on the pawn at the end (within 5 m); level ambient emitters are fixed in
        # the world and keep playing by design.
        $fr = @(Read-WfcFrames (Join-Path (Join-Path $OutDir $name) "wfc.log")); $last = $fr[$fr.Count - 1]
        $pp = V3 $last.x $last.y $last.z
        $stale = @($endLoops | Where-Object { $_.t -lt $tp -and (Dist $_.pos $pp) -lt 5.0 })
        Add-WfcResult $all "$id.vehicle_loops_end_after_exit" $(if ($stale.Count -eq 0) { "PASS" } else { "FAIL" }) $stale.Count ("player-owned looping voices (within 5 m of the pawn) started before the V->R press and still live {0:F1} s later: {1}" -f ($n / 60.0 - $tp), $(if ($stale.Count) { (($stale | ForEach-Object { ($paths[$_.snd] -split "[/\\]")[-1] }) -join ", ") } else { "none" }))
        $owned = @($endLoops | Where-Object { (Dist $_.pos $pp) -lt 5.0 })
        Add-WfcResult $all "$id.player_loops_live_at_end" "INFO" $owned.Count (($owned | ForEach-Object { ($paths[$_.snd] -split "[/\\]")[-1] }) -join ", ")
    }
}
# ---- presence checks (authored cue played in the scenario) ----
function Heard($scen, $pattern) { if (-not $played[$scen]) { return $null }; return @($played[$scen].Keys | Where-Object { $_ -like $pattern }).Count -gt 0 }
$expect = @(
    @("robot_footsteps", "BL_FS_LRG_BOT.FS_*", "robot footsteps (BL_FS_LRG_BOT, authored notifies)"),
    @("stationary_transform", "BL_TRANSFORM.OPTIMUS_BOT2VEH", "transform to vehicle cue (BL_TRANSFORM.OPTIMUS_BOT2VEH)"),
    @("jump_land", "BL_FS_LRG_BOT.FS_LAND*", "robot landing cue (BL_FS_LRG_BOT.FS_LAND_*)"),
    @("fine_aim", "*FINE_AIM_START", "fine aim start cue (WP_StartFineAim -> FINE_AIM_START)"),
    @("fine_aim", "*FINE_AIM_END", "fine aim end cue (FINE_AIM_END)"),
    @("hover_move", "*", "vehicle loops present (any cue while hovering)"))
foreach ($e in $expect) {
    $h = Heard $e[0] $e[1]
    if ($null -eq $h) { continue }
    Add-WfcResult $all "audio_attach.present.$($e[0]).$($e[1] -replace '[\*\.]', '_')" $(if ($h) { "PASS" } else { "KNOWN" }) $([int]$h) $e[2] $(if ($h) { "" } else { "Systems" })
}
$sum = Write-WfcReport $all (Join-Path $OutDir "report.json")
@("scenario`tcue (candidates sharing the wave)`twave`tattachment`tmax offset growth") + $offending.ToArray() | Set-Content (Join-Path $OutDir "offending_cues.txt")
"AUDIO SUMMARY: {0} pass, {1} FAIL, {2} known, {3} info; offending cue list: {4}" -f $sum.pass, $sum.fail, $sum.known, $sum.info, (Join-Path $OutDir "offending_cues.txt")
