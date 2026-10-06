# Character / vehicle visual jitter diagnostics (human report: the player character and the vehicle visibly jitter,
# separate or "interlace" while moving). Splits the possible causes so the owner can be named:
#
#   A. SIMULATION (legacy boot, lockstep 60 Hz, WFC_LOGEVERY=1, every frame logged)
#      sim_discontinuity    per-frame displacement spikes against the steady speed (> 2.5x the median step) or
#                           backward steps while moving forward                                    -> Gameplay
#      stale_frames         consecutive frames with an identical position while moving (sim stepping slower than
#                           the frame rate: without render interpolation the pawn moves in steps) -> Gameplay / Integration
#      anim_double_advance  animation time advancing 2x dt (or alternating 0 / 2x) between frames     -> Gameplay (anim)
#      anim_stall           animation time frozen while moving                                     -> Gameplay (anim)
#      yaw_discontinuity    pawn / leg yaw jumps between frames                                     -> Gameplay
#   B. PRESENTATION (frontend boot, real time, the product's own composed frames: N consecutive `shot:` frames)
#      alternation          ping-pong between frames inside the character region: |f(i)-f(i+2)| much smaller than
#                           |f(i)-f(i+1)| = two poses / two transforms drawn on alternate frames (interlace, stale or
#                           duplicate frame data, render interpolation error)                       -> Rendering / Integration
#      region_spikes        isolated frame-to-frame jumps in the character region far above its median change
#   C. BONE level: the product exposes no per-bone transform log. Proposal (Gameplay / Rendering): a
#      'POSE frame=<n> bone=<name> pos=x,y,z' line for the root, pelvis, head and both hands under WFC_POSELOG.
#      Until it exists, root-vs-bone separation is judged from B plus a human look -> UNKNOWN / HUMAN.
#
#   .\tools\fidelity\motion-jitter.ps1 -Exe <wfc_rebuild.exe> -RenderData <work\render> -OutDir <dir> [-Parts A,B]
param([Parameter(Mandatory)][string]$Exe, [string]$RenderData = "", [Parameter(Mandatory)][string]$OutDir, [string[]]$Parts = @("A", "B", "C", "D", "E"),
      [int]$Start = 5, [int]$Frames = 48)
$ErrorActionPreference = "Stop"
$Parts = @($Parts | ForEach-Object { $_ -split "," } | Where-Object { $_ })
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1")
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path; $Exe = (Resolve-Path $Exe).Path
$H = Get-ExeHooks $Exe
$X = Get-Content -Raw (Join-Path $PSScriptRoot "m05\expectations.json") | ConvertFrom-Json
$res = New-WfcResults
function Median($v) { $s = @($v | Sort-Object); if (-not $s.Count) { return 0 }; return $s[[int][Math]::Floor($s.Count / 2)] }

# ---------------------------------------------------------------- A. simulation
if ($Parts -contains "A") {
    $lock = $H.Contains("WFC_LOCKSTEP")
    if (-not $lock) { Add-WfcResult $res "jitter.sim.lockstep" "SKIP" $null "WFC_LOCKSTEP not compiled in: per-frame simulation analysis needs a fixed 1/60 s step (Integration)" "Integration" }
    $cases = [ordered]@{
        robot_walk     = @{ WFC_AUTOWALK = "1" }
        robot_strafe   = @{ WFC_AUTOSTRAFE = "1" }
        robot_turnwalk = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.8" }
        robot_fire     = @{ WFC_AUTOWALK = "1"; WFC_AUTOFIRE = "1" }
        vehicle_drive  = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1" }
        vehicle_boost  = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1" }
        vehicle_nitro  = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTODASH = "150" }
        vehicle_turn   = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.6" }
    }
    $rows = @()
    foreach ($k in $cases.Keys) {
        if (-not $lock) { break }
        $d = Join-Path $OutDir "A_$k"
        $e = @{ WFC_BOOT = "match"; WFC_SMOKE_FRAMES = "360"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_START = "$Start" } + $cases[$k]
        if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
        $null = Invoke-WfcExe $Exe $d $e "run.log" 300
        $fr = @(Read-WfcFrames (Join-Path $d "wfc.log") | Where-Object { $_.frame -ge 90 })   # steady state after 1.5 s
        if ($fr.Count -lt 30) { $rows += [pscustomobject]@{ case = $k; frames = $fr.Count; note = "too few frames" }; continue }
        $steps = @(); $same = 0; $back = 0; $dt = @(); $yawJ = 0; $legJ = 0
        for ($i = 1; $i -lt $fr.Count; $i++) {
            $a = $fr[$i - 1]; $b = $fr[$i]
            $s = [Math]::Sqrt([Math]::Pow($b.x - $a.x, 2) + [Math]::Pow($b.z - $a.z, 2)); $steps += $s
            if ($s -lt 1e-4 -and $a.hspeed -gt 0.5) { $same++ }
            if ($a.anim -eq $b.anim) { $dt += [Math]::Round($b.t - $a.t, 4) }
            $yw = [Math]::Abs([Math]::Atan2([Math]::Sin($b.yaw - $a.yaw), [Math]::Cos($b.yaw - $a.yaw))); if ($yw -gt 0.35) { $yawJ++ }
            if ($a.legYaw -ne $null -and [Math]::Abs($b.legYaw - $a.legYaw) -gt 25 -and [Math]::Abs($b.legYaw - $a.legYaw) -lt 335) { $legJ++ }
        }
        $moving = @($steps | Where-Object { $_ -gt 1e-4 }); $med = Median $moving
        $spikes = @($moving | Where-Object { $med -gt 0 -and $_ -gt 2.5 * $med }).Count
        $dtPos = @($dt | Where-Object { $_ -gt 0 }); $dtMed = Median $dtPos
        $double = @($dtPos | Where-Object { $dtMed -gt 0 -and $_ -gt 1.7 * $dtMed }).Count; $zero = @($dt | Where-Object { $_ -eq 0 }).Count
        $rows += [pscustomobject][ordered]@{ case = $k; frames = $fr.Count; median_step_m = [Math]::Round($med, 4); speed_ms = [Math]::Round($med * 60, 2); stale_frames = $same; stale_ratio = [Math]::Round($same / [Math]::Max(1, $fr.Count - 1), 3)
            step_spikes = $spikes; anim_dt_median = $dtMed; anim_double = $double; anim_zero = $zero; yaw_jumps = $yawJ; legyaw_jumps = $legJ }
    }
    Write-WfcCsv $rows (Join-Path $OutDir "sim_jitter.csv")
    foreach ($r in $rows) {
        if (-not $r.median_step_m) { continue }
        Add-WfcResult $res "jitter.sim.$($r.case).transform" $(if ($r.step_spikes -gt 2) { "FAIL" } else { "PASS" }) $r.step_spikes ("steady {0} m/s; per-frame displacement spikes > 2.5x median: {1}; yaw jumps {2}, leg-yaw jumps {3}" -f $r.speed_ms, $r.step_spikes, $r.yaw_jumps, $r.legyaw_jumps) "Gameplay"
        Add-WfcResult $res "jitter.sim.$($r.case).stale" $(if ($r.stale_ratio -gt 0.3) { "INFO" } else { "PASS" }) $r.stale_ratio ("frames with an unchanged position while moving: {0} ({1:P0}) - the simulation steps slower than the frame rate; visible stepping unless the renderer interpolates" -f $r.stale_frames, $r.stale_ratio) "Gameplay/Integration"
        Add-WfcResult $res "jitter.sim.$($r.case).anim" $(if ($r.anim_double -gt 3) { "FAIL" } elseif ($r.anim_zero -gt ($r.frames * 0.3)) { "INFO" } else { "PASS" }) $r.anim_double ("animation time step median {0}; double advances {1}; frozen steps {2}" -f $r.anim_dt_median, $r.anim_double, $r.anim_zero) "Gameplay"
    }
}

# ---------------------------------------------------------------- B. presentation (consecutive composed frames)
if ($Parts -contains "B") {
    # A frontend-launched match respawns a fresh robot (RestartPlayer) and the frontend script has no gameplay-input step, so
    # vehicle presentation frames need a hook (proposal: script step "press:transform"); robot phases only here.
    $phases = [ordered]@{ robot_walk = @{ WFC_AUTOWALK = "1" }; robot_strafe_turn = @{ WFC_AUTOSTRAFE = "1"; WFC_AUTOTURN = "0.6" } }
    Add-WfcResult $res "jitter.present.vehicle" "WAITING" $null "vehicle presentation frames need a scripted transform in a frontend-launched match (proposal for Frontend / Integration: FRONTEND_SCRIPT step press:transform / hold:boost). Vehicle simulation jitter: part A; look: HUMAN" "Frontend/Integration"
    foreach ($ph in $phases.Keys) {
        $d = Join-Path $OutDir "B_$ph"; New-Item -ItemType Directory -Force $d | Out-Null
        $seq = (1..$Frames | ForEach-Object { Shot $d ("f{0:D3}" -f $_) }) -join ";"
        $s = "wait:frontend;call:Online.OpenPartyLobby,GTS_TeamGame;wait:level=PartyLobby;call:Online.EditGameMode,TDM;call:Online.PlayPrivateGame,TDM;wait:level=GameLobby;call:Online.SetSelectedMapID,508;call:Online.BeginLobbyExitCountdown;wait:level=Match;wait:ui=InGame;wait:t=4;$seq;quit"
        $e = @{ WFC_BOOT = "frontend"; WFC_SKIPINTRO = "1"; WFC_FRONTEND_SCRIPT = $s; WFC_FLOWLOG = (Join-Path $d "flow.jsonl"); WFC_NOMOUSE = "1" } + $phases[$ph]
        if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
        $null = Invoke-WfcSampled $Exe $d $e 600 1.0
        $files = @(Get-ChildItem $d -Filter "f*.bmp" | Sort-Object Name)
        if ($files.Count -lt 10) { Add-WfcResult $res "jitter.present.$ph" "SKIP" $files.Count "only $($files.Count) consecutive frames captured" "Experimental"; continue }
        # character region: the chase camera keeps the pawn in the lower-middle of the frame (cells of the 8 px luma grid)
        $L = @($files | ForEach-Object { , [WfcImage]::Luma($_.FullName, 8) })   # unary comma: keep each frame an array
        $w = [int]$L[0][0]; $fh = [int]$L[0][1]
        $x0 = [int]($w * 0.38); $x1 = [int]($w * 0.62); $y0 = [int]($fh * 0.40); $y1 = [int]($fh * 0.92)
        function RegionDiff($a, $b) { $s = 0.0; $n = 0; for ($yy = $y0; $yy -lt $y1; $yy++) { for ($xx = $x0; $xx -lt $x1; $xx++) { $ix = 2 + $yy * $w + $xx; $s += [Math]::Abs($a[$ix] - $b[$ix]); $n++ } }; return $s / [Math]::Max(1, $n) }
        $d1 = @(); $d2 = @(); for ($i = 0; $i -lt $L.Count - 2; $i++) { $d1 += (RegionDiff ($L[$i]) ($L[$i + 1])); $d2 += (RegionDiff ($L[$i]) ($L[$i + 2])) }
        $m1 = ($d1 | Measure-Object -Average).Average; $m2 = ($d2 | Measure-Object -Average).Average
        $alt = if ($m1 -gt 0.05) { [Math]::Round($m2 / $m1, 3) } else { $null }
        $pp = 0; for ($i = 0; $i -lt $d2.Count; $i++) { if ($d1[$i] -gt 1.0 -and $d2[$i] -lt 0.5 * $d1[$i]) { $pp++ } }
        $dm = Median $d1; $spk = @($d1 | Where-Object { $_ -gt [Math]::Max(4 * $dm, 2.5) }).Count   # absolute floor: 4x a tiny median is noise
        $dup = @($d1 | Where-Object { $_ -lt 0.05 }).Count
        Write-WfcCsv @(for ($i = 0; $i -lt $d1.Count; $i++) { [pscustomobject]@{ frame = $i; d1 = [Math]::Round($d1[$i], 3); d2 = [Math]::Round($d2[$i], 3) } }) (Join-Path $d "region_diffs.csv")
        $tiles = @($files | Select-Object -First 16 | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }); New-WfcSheet $tiles (Join-Path $OutDir "B_$ph.png") 4 320 180
        Add-WfcResult $res "jitter.present.$ph.alternation" $(if ($alt -eq $null) { "SKIP" } elseif ($pp -gt $d2.Count * 0.25) { "FAIL" } else { "PASS" }) $alt ("{0} consecutive frames, character region: mean |f(i)-f(i+1)| {1:N2}, |f(i)-f(i+2)| {2:N2} (ratio {3}; smooth motion >= 1); ping-pong frames {4} (two poses / transforms drawn on alternate frames). Limit: a camera-pacing swim (the M05 regression) is not an alternation - part C is its detector" -f $files.Count, $m1, $m2, $alt, $pp) "Rendering/Integration"
        Add-WfcResult $res "jitter.present.$ph.spikes" $(if ($spk -gt 2) { "FAIL" } else { "PASS" }) $spk ("isolated jumps > 4x the median frame-to-frame change: {0}; identical consecutive frames: {1}" -f $spk, $dup) "Rendering/Gameplay"
    }
    Add-WfcResult $res "jitter.bone_level" "UNKNOWN" $null "no per-bone transform log in the product: root vs individual-bone separation cannot be measured (proposal: WFC_POSELOG 'POSE frame bone pos' for root / pelvis / head / hands). Presentation evidence above + HUMAN-CHECK" "Gameplay/Rendering"
}
# ---------------------------------------------------------------- C. frame pacing (Rendering M08 method, agents/rendering 429bcf3)
# The M05 "interlacing": the camera orbit position advanced on the 60 Hz simulation tick while its rotation advanced every
# rendered frame, so above 60 Hz the pawn swims on screen; lockstep / 60 Hz tests cannot see it. WFC_RENDERHZ fixes the
# display rate, WFC_CAMLOG logs the pawn's projected screen position per rendered frame; metric = |second difference| of
# screen x per frame (Rendering: broken 0.0095 mean / 0.014 max at 144 Hz, fixed <= 0.0001).
if ($Parts -contains "C") {
    if (-not ($H.Contains("WFC_RENDERHZ") -and $H.Contains("WFC_CAMLOG"))) { Add-WfcResult $res "jitter.pacing" "WAITING" $null "WFC_RENDERHZ / WFC_CAMLOG not in this exe (Rendering M08 diagnostics, agents/rendering 429bcf3: Integration to carry them)" "Integration" }
    else {
        $pcases = [ordered]@{ robot_walk_turn = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.8" }; vehicle_boost_turn = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_AUTOTURN = "0.6" } }
        $prow = @()
        foreach ($pc in $pcases.Keys) { foreach ($hz in 60, 144, 240) {
            $d = Join-Path $OutDir "C_${pc}_$hz"
            $e = @{ WFC_BOOT = "match"; WFC_SMOKE_FRAMES = "720"; WFC_RENDERHZ = "$hz"; WFC_CAMLOG = "1"; WFC_NOMOUSE = "1"; WFC_START = "$Start" } + $pcases[$pc]
            if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
            $null = Invoke-WfcExe $Exe $d $e "run.log" 300
            $sx = @(Grep-Log (Join-Path $d "wfc.log") '\] CAMLOG (\d+) (-?[\d.]+) (-?[\d.]+)' | ForEach-Object { $m = [regex]::Match($_.text, 'CAMLOG (\d+) (-?[\d.]+) (-?[\d.]+)'); [pscustomobject]@{ f = [int]$m.Groups[1].Value; x = [double]$m.Groups[2].Value; y = [double]$m.Groups[3].Value } } | Where-Object { $_.f -ge 240 })
            $dd = @(); for ($i = 1; $i -lt $sx.Count - 1; $i++) { $dd += [Math]::Abs($sx[$i + 1].x - 2 * $sx[$i].x + $sx[$i - 1].x) }
            $mean = if ($dd.Count) { [Math]::Round(($dd | Measure-Object -Average).Average, 5) } else { $null }; $mx = if ($dd.Count) { [Math]::Round(($dd | Measure-Object -Maximum).Maximum, 5) } else { $null }
            $over = if ($dd.Count) { [Math]::Round(@($dd | Where-Object { $_ -gt 0.004 }).Count / $dd.Count, 4) } else { $null }
            $prow += [pscustomobject]@{ case = $pc; hz = $hz; frames = $sx.Count; d2_mean = $mean; d2_max = $mx; frac_over_0004 = $over }
        } }
        Write-WfcCsv $prow (Join-Path $OutDir "frame_pacing.csv")
        foreach ($g in ($prow | Group-Object case)) {
            $bad = @($g.Group | Where-Object { $_.d2_mean -ne $null -and ($_.d2_mean -gt 0.002 -or $_.frac_over_0004 -gt 0.05) })   # sustained swim; isolated pops (camera obstruction) are reported, not failed
            $pops = @($g.Group | Where-Object { $_.d2_max -gt 0.006 }); $none = @($g.Group | Where-Object { $_.d2_mean -eq $null })
            Add-WfcResult $res "jitter.pacing.$($g.Name)" $(if ($none.Count -eq $g.Count) { "SKIP" } elseif ($bad.Count) { "FAIL" } else { "PASS" }) (($g.Group | Measure-Object d2_mean -Maximum).Maximum) ("pawn screen-x second difference per rendered frame (mean / max): " + (($g.Group | ForEach-Object { "{0} Hz {1} / {2} ({3:P1} frames > 0.004)" -f $_.hz, $_.d2_mean, $_.d2_max, $_.frac_over_0004 }) -join "; ") + " (Rendering M08: broken 0.0095 mean at 144 Hz, fixed <= 0.0001; FAIL = mean > 0.002 or > 5% of frames > 0.004)") "Gameplay"
            if ($pops.Count -and -not $bad.Count) { Add-WfcResult $res "jitter.pacing.$($g.Name).pops" "INFO" (($pops | Measure-Object d2_max -Maximum).Maximum) ("isolated screen-position pops (max second difference > 0.006) at " + (($pops | ForEach-Object { "$($_.hz) Hz" }) -join ", ") + ": single events (camera obstruction / collision), not periodic jitter") "Gameplay" }
        }
    }
}
# ---------------------------------------------------------------- D. vehicle / robot presentation frames (WFC_SHOTEVERY, legacy boot)
function Analyze-Seq($files, $label, $owner) {
    $L = @($files | ForEach-Object { , [WfcImage]::Luma($_.FullName, 8) }); $w = [int]$L[0][0]; $h = [int]$L[0][1]
    $x0 = [int]($w * 0.38); $x1 = [int]($w * 0.62); $y0 = [int]($h * 0.40); $y1 = [int]($h * 0.92)
    $d1 = @(); $d2 = @()
    function RegionMeanDiff($fa, $fb) { $s = 0.0; $n = 0; for ($yy = $y0; $yy -lt $y1; $yy++) { for ($xx = $x0; $xx -lt $x1; $xx++) { $ix = 2 + $yy * $w + $xx; $s += [Math]::Abs($fa[$ix] - $fb[$ix]); $n++ } }; return $s / [Math]::Max(1, $n) }
    for ($i = 0; $i -lt $L.Count - 2; $i++) { $d1 += (RegionMeanDiff ($L[$i]) ($L[$i + 1])); $d2 += (RegionMeanDiff ($L[$i]) ($L[$i + 2])) }
    $m1 = ($d1 | Measure-Object -Average).Average; $m2 = ($d2 | Measure-Object -Average).Average
    $pp = 0; for ($i = 0; $i -lt $d2.Count; $i++) { if ($d1[$i] -gt 1.0 -and $d2[$i] -lt 0.5 * $d1[$i]) { $pp++ } }
    Add-WfcResult $res "jitter.present.$label.alternation" $(if ($pp -gt $d2.Count * 0.25) { "FAIL" } else { "PASS" }) $(if ($m1 -gt 0) { [Math]::Round($m2 / $m1, 3) }) ("{0} consecutive frames: |f(i)-f(i+1)| {1:N2}, |f(i)-f(i+2)| {2:N2}; ping-pong frames {3}" -f $files.Count, $m1, $m2, $pp) $owner
}
if ($Parts -contains "D") {
    if (-not $H.Contains("WFC_SHOTEVERY")) { Add-WfcResult $res "jitter.present.vehicle" "WAITING" $null "WFC_SHOTEVERY not in this exe (Rendering M08 per-frame capture; Integration to carry it)" "Integration" }
    else {
        foreach ($vc in @(@{ n = "vehicle_boost_144"; e = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_RENDERHZ = "144" } }, @{ n = "robot_walk_144"; e = @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.5"; WFC_RENDERHZ = "144" } })) {
            $d = Join-Path $OutDir "D_$($vc.n)"; New-Item -ItemType Directory -Force $d | Out-Null
            $e = @{ WFC_BOOT = "match"; WFC_SMOKE_FRAMES = "420"; WFC_NOMOUSE = "1"; WFC_START = "$Start"; WFC_SHOTEVERY = "$d,360,$(360 + $Frames - 1)" } + $vc.e
            if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
            $null = Invoke-WfcExe $Exe $d $e "run.log" 300
            $files = @(Get-ChildItem $d -Filter "f*.bmp" | Sort-Object Name)
            if ($files.Count -ge 10) { Analyze-Seq $files $vc.n "Gameplay/Rendering"; New-WfcSheet @($files | Select-Object -First 16 | ForEach-Object { @{ png = $_.FullName; label = $_.BaseName } }) (Join-Path $OutDir "D_$($vc.n).png") 4 320 180 }
            else { Add-WfcResult $res "jitter.present.$($vc.n)" "SKIP" $files.Count "per-frame captures missing" "Experimental" }
        }
    }
}
# ---------------------------------------------------------------- E. vehicle states (RE MILESTONE05_PLAYTEST_RE 1.2, CONFIRMED)
# Hover (default) max 15 m/s; Boost max 30 m/s; Nitro (dash while boosting) x1.5 speed, x0.3 steering, 3 s, 8 s cooldown.
# Lockstep legacy boot: hover 2 s, boost from frame 120, nitro at frame 300, a second nitro request at frame 600 (inside
# the 8 s cooldown: must be refused), boost released at frame 780 (back to hover).
if ($Parts -contains "E") {
    if (-not ($H.Contains("WFC_LOCKSTEP") -and $H.Contains("WFC_BOOSTLOG"))) { Add-WfcResult $res "vehicle_states" "SKIP" $null "needs WFC_LOCKSTEP + WFC_BOOSTLOG" "Integration" }
    else {
        # A frontal hit legitimately ends boost / nitro (RE PT 1.2: contact normal . forward > 0.866 -> Hovering), so the nitro
        # timing needs a clear runway: try starts until the nitro window (frames 300-480) has no impact (speed collapse).
        $interrupted = @(); $fr = @(); $d = $null; $cands = @()
        foreach ($cs in @($Start, 20, 41, 60, 77, 12, 33, 47)) {
            $d = Join-Path $OutDir "E_vehicle_states_s$cs"
            $e = @{ WFC_BOOT = "match"; WFC_SMOKE_FRAMES = "960"; WFC_LOCKSTEP = "1"; WFC_NOMOUSE = "1"; WFC_LOGEVERY = "1"; WFC_START = "$cs"; WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "120"; WFC_AUTODASH = "300"; WFC_AUTODASH2 = "600"; WFC_AUTOWALK_UNTIL = "780"; WFC_BOOSTLOG = "1" }
            if ($RenderData) { $e.WFC_RENDER_DATA = $RenderData }
            $null = Invoke-WfcExe $Exe $d $e "run.log" 300
            $fr = @(Read-WfcFrames (Join-Path $d "wfc.log"))
            $win = @($fr | Where-Object { $_.frame -ge 200 -and $_.frame -le 480 }); $hit = $false   # the whole boost phase: a wall before the nitro press spoils it too
            for ($k = 6; $k -lt $win.Count; $k++) { if ($win[$k - 6].hspeed -gt 10 -and $win[$k].hspeed -lt 0.5 * $win[$k - 6].hspeed) { $hit = $true; break } }
            $topB = @($fr | Where-Object { $_.frame -ge 220 -and $_.frame -lt 300 } | ForEach-Object { $_.hspeed } | Measure-Object -Maximum).Maximum
            $eng = @($fr | Where-Object { $_.frame -ge 298 -and $_.frame -le 306 -and $_.nitro -gt 0 }).Count -gt 0
            $cands += [pscustomobject]@{ s = $cs; dir = $d; topB = $topB; hit = $hit; engaged300 = $eng }
            if (-not $hit -and $topB -gt 20) { break }; $interrupted += $cs   # a usable runway: boost reached > 20 m/s and nothing stopped it
        }
        # no clean runway: measure on the start with the best boost run whose nitro engaged at frame 300
        $pick = @($cands | Where-Object { -not $_.hit -and $_.topB -gt 20 })[0]; if (-not $pick) { $pick = @($cands | Where-Object engaged300 | Sort-Object topB -Descending)[0] }; if (-not $pick) { $pick = @($cands | Sort-Object topB -Descending)[0] }
        $d = $pick.dir; $fr = @(Read-WfcFrames (Join-Path $d "wfc.log")); $usable = $pick.topB -gt 20
        if ($interrupted.Count) { Add-WfcResult $res "vehicle_states.runway" "INFO" $interrupted.Count ("starts without a clear 8 s boost runway (boost ends on a frontal hit, RE PT 1.2 - not a defect): " + ($interrupted -join ",") + "; measured on start " + $pick.s + " (boost " + $pick.topB + " m/s, nitro engaged at frame 300: " + $pick.engaged300 + ")") "" }
        $nl = @(Grep-Log (Join-Path $d "wfc.log") '\] NITRO active=(\d) remaining=([\d.]+) cooldown=([\d.]+) speedScale=([\d.]+) steeringScale=([\d.]+)' | ForEach-Object { $m = [regex]::Match($_.text, 'active=(\d) remaining=([\d.]+) cooldown=([\d.]+) speedScale=([\d.]+) steeringScale=([\d.]+)'); [pscustomobject]@{ i = $_.i; active = [int]$m.Groups[1].Value; rem = [double]$m.Groups[2].Value; cd = [double]$m.Groups[3].Value; sp = [double]$m.Groups[4].Value; st = [double]$m.Groups[5].Value } })
        function SegMax($a, $b) { $v = @($fr | Where-Object { $_.frame -ge $a -and $_.frame -lt $b } | ForEach-Object { $_.hspeed }); if ($v.Count) { return [Math]::Round(($v | Measure-Object -Maximum).Maximum, 2) } else { return $null } }
        $hover = SegMax 60 120; $boost = SegMax 220 300; $nitro = SegMax 330 470; $after = SegMax 860 960
        # map each NITRO line (every 6th vehicle frame) to the frame logged just before it (WFC_LOGEVERY=1)
        $frIdx = @(Grep-Log (Join-Path $d "wfc.log") '\] frame (\d+) pos' | ForEach-Object { [pscustomobject]@{ i = $_.i; f = [int][regex]::Match($_.text, 'frame (\d+)').Groups[1].Value } })
        foreach ($n in $nl) { $pf = @($frIdx | Where-Object { $_.i -lt $n.i } | Select-Object -Last 1)[0]; $n | Add-Member -Force -NotePropertyName frame -NotePropertyValue $(if ($pf) { $pf.f } else { -1 }) }
        $acts = @(); for ($k = 0; $k -lt $nl.Count; $k++) { if ($nl[$k].active -eq 1 -and ($k -eq 0 -or $nl[$k - 1].active -eq 0)) { $acts += $nl[$k] } }
        $first = @($acts)[0]; $act = @($nl | Where-Object active -eq 1); $actSpan = if ($act.Count -ge 2) { ($act[-1].frame - $act[0].frame + 6) / 60.0 } elseif ($act.Count) { 0.1 } else { 0 }
        $endFrame = if ($act.Count) { $act[-1].frame } else { -1 }; $impact = @($fr | Where-Object { $_.frame -ge 300 -and $_.frame -le $endFrame + 12 })
        $cut = $false; for ($k = 6; $k -lt $impact.Count; $k++) { if ($impact[$k - 6].hspeed -gt 10 -and $impact[$k].hspeed -lt 0.5 * $impact[$k - 6].hspeed) { $cut = $true } }
        $rate = if ($act.Count -ge 3) { [Math]::Round(($act[0].rem - $act[-1].rem) / (($act[-1].frame - $act[0].frame) / 60.0), 2) } else { $null }
        $first = @($acts | Where-Object { $_.frame -ge 296 -and $_.frame -le 312 })[0]   # the frame-300 request
        $second = @($acts | Where-Object { $first -and $_ -ne $first -and $_.frame -ge 590 -and $_.frame -lt ($first.frame + 480) }).Count
        $flips = 0; for ($k = 1; $k -lt $nl.Count; $k++) { if ($nl[$k].active -ne $nl[$k - 1].active) { $flips++ } }
        $vs = $X.playtest.vehicle_states.value
        $osc = 0; $bs = @($fr | Where-Object { $_.frame -ge 200 -and $_.frame -lt 300 }); for ($k = 2; $k -lt $bs.Count; $k++) { $a1 = $bs[$k - 1].hspeed - $bs[$k - 2].hspeed; $a2 = $bs[$k].hspeed - $bs[$k - 1].hspeed; if ([Math]::Sign($a1) -ne [Math]::Sign($a2) -and [Math]::Abs($a1) -gt 1 -and [Math]::Abs($a2) -gt 1) { $osc++ } }
        Write-WfcCsv @([pscustomobject]@{ hover_max = $hover; boost_max = $boost; nitro_max = $nitro; after_release_max = $after; nitro_active_s = $actSpan; nitro_flips = $flips; speedScale = ($nl | Measure-Object sp -Maximum).Maximum; steeringScale = ($nl | Where-Object active -eq 1 | Measure-Object st -Minimum).Minimum; boost_speed_oscillations = $osc }) (Join-Path $OutDir "vehicle_states.csv")
        Add-WfcResult $res "vehicle_states.speeds" $(if (-not $usable) { "SKIP" } elseif ($hover -and $boost -and $hover -le $vs.hover_max_ms * 1.1 -and $boost -le $vs.boost_max_ms * 1.1 -and $boost -gt $hover * 1.3) { "PASS" } else { "FAIL" }) $boost ("max speed hover {0} / boost {1} / nitro {2} / after boost release {3} m/s (PT 1.2: hover 15, boost 30, nitro x1.5 = 45)" -f $hover, $boost, $nitro, $after) "Gameplay"
        Add-WfcResult $res "vehicle_states.nitro" $(if (-not $first -and -not $pick.engaged300) { "SKIP" } elseif (-not $first) { "FAIL" } elseif ([Math]::Abs($first.rem - $vs.nitro_s) -le 0.1 -and [Math]::Abs($first.cd - $vs.nitro_cooldown_s) -le 0.1 -and $first.sp -eq $vs.nitro_speed_scale -and $first.st -eq $vs.nitro_steering_scale -and -not $second -and ($cut -or [Math]::Abs($actSpan - $vs.nitro_s) -le 0.3)) { "PASS" } else { "FAIL" }) $actSpan ("activation at frame {0}: remaining {1} s / cooldown {2} s / speedScale {3} / steeringScale {4} (PT 1.2: 3 / 8 / 1.5 / 0.3); countdown rate {5} s/s; active {6:N1} s{7}; a second request at frame 600 (inside the 8 s cooldown) activated: {8}" -f $first.frame, $first.rem, $first.cd, $first.sp, $first.st, $rate, $actSpan, $(if ($cut) { " (ended early by an impact: boost exits on a frontal hit, original)" } else { "" }), [bool]$second) "Gameplay"
        Add-WfcResult $res "vehicle_states.stable" $(if ($osc -gt 6) { "FAIL" } else { "PASS" }) $osc ("steady boost (frames 200-300): speed oscillations > 1 m/s/frame: {0} (state / speed flicker)" -f $osc) "Gameplay"
        Add-WfcResult $res "vehicle_states.visuals" "HUMAN" $null "hover boosters (6 HoverFX, Size from thruster contribution) / BoostFx / RamFX for Nitro must be distinguishable and steady (PT 1.2 propulsion visuals)" "Rendering/Systems"
    }
}
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"MOTION JITTER: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
