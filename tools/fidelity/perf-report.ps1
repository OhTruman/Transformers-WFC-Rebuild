# Attribute perf-profile.ps1 samples to cost categories and origins, per scenario phase.
#
#   .\tools\fidelity\perf-report.ps1                 # reads work\fidelity\perf\*\{samples.txt,frames.csv}
#
# COST (what the CPU is doing on the main thread; first matching rule):
#   present        Win32Window::present (SwapBuffers: driver queue / GPU-bound wait)
#   visibility     Pipeline::computeEnv -> light visibility rays (CollisionWorld::segmentHit)
#   lighting       Pipeline::computeEnv (light environment) without the rays
#   collision      CollisionWorld queries outside lighting (hitscan, camera aim ray, movement, camera)
#   first_use      first-use GPU program / texture / static mesh builds (startup hitches)
#   skinning       CPU skinning + dynamic vertex build/upload (skinPose, Pipeline::buildVertices/drawDynamic)
#   particles      WeaponFx / VehicleFx code, renderer particle submission
#   audio          SoundCues, Win32Audio
#   weapon         WeaponMesh, weapon presentation/notifies, fireHitscan bookkeeping, recoil
#   anim           pose sampling/blending, Character animation (not skinning)
#   render         other renderer / GL driver work
#   gameplay       CharacterMovement / PlayerController / Weapon logic
#   sim            other World::tick
#   other
# ORIGIN (which subsystem's request caused it):
#   shell_mag      WeaponFx::draw -> renderer mesh draw (shell / magazine mesh particles)
#   weapon_fx      other WeaponFx draw/tick/emit
#   vehicle_fx     VehicleFx
#   character      Character draw/animation (robot/vehicle/weapon meshes)
#   hitscan        World::fireHitscan (weapon trace)
#   camera_aim     PlayerController::applyToPawn camera-ray aim trace
#   movement       CharacterMovement (ground/wall queries)
#   world          map draw, frame setup, post-process
#   audio, sim, present, other
param([string]$PerfDir = "", [string]$Exe = "")
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $PerfDir) { $PerfDir = Join-Path $root "work\fidelity\perf" }
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_prof.exe" }
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$sym = Find-WfcTool "llvm-symbolizer" $root
$imageBase = [UInt64]"0x140000000"

# ---- load samples ------------------------------------------------------------------------------
$scen = @{}
$allVa = New-Object 'System.Collections.Generic.HashSet[UInt64]'
foreach ($d in Get-ChildItem $PerfDir -Directory) {
    $sf = Join-Path $d.FullName "samples.txt"; $ff = Join-Path $d.FullName "frames.csv"
    if (-not (Test-Path $sf) -or -not (Test-Path $ff)) { continue }
    $mods = @{}; $samples = New-Object System.Collections.Generic.List[object]
    foreach ($line in [IO.File]::ReadLines($sf)) {
        if ($line.StartsWith("M ")) { $p = $line.Split(" ", 3); $mods[[int]$p[1]] = $p[2]; continue }
        if (-not $line.StartsWith("S ")) { continue }
        $parts = $line.Split(" ")
        $frames = New-Object System.Collections.Generic.List[string]
        for ($i = 2; $i -lt $parts.Length; $i++) { $frames.Add($parts[$i]) }
        $samples.Add(@{ t = [double]$parts[1]; f = $frames })
    }
    $exeId = ($mods.GetEnumerator() | Where-Object { $_.Value -like "*wfc_rebuild_prof.exe" } | Select-Object -First 1).Key
    foreach ($s in $samples) { foreach ($fr in $s.f) { $mi, $rva = $fr.Split(":"); if ([int]$mi -eq $exeId) { [void]$allVa.Add($imageBase + [Convert]::ToUInt64($rva, 16)) } } }
    $scen[$d.Name] = @{ mods = $mods; exe = $exeId; samples = $samples; frames = (Import-Csv $ff) }
}
if ($scen.Count -eq 0) { throw "no scenarios in $PerfDir" }

# ---- symbolize all exe addresses once -----------------------------------------------------------
$vaList = @($allVa)
$tmp = [IO.Path]::GetTempFileName()
[IO.File]::WriteAllLines($tmp, ($vaList | ForEach-Object { "0x{0:x}" -f $_ }))
$out = @(Get-Content $tmp | & $sym "--obj=$Exe" --no-inlines)   # addresses on stdin
Remove-Item $tmp
$symOf = @{}; $k = 0; $i = 0
while ($i -lt $out.Count -and $k -lt $vaList.Count) {
    if ($out[$i] -eq "") { $i++; continue }
    $fn = $out[$i]; $loc = if ($i + 1 -lt $out.Count) { $out[$i + 1] } else { "" }
    $file = ""; $ln = 0
    if ($loc -match "^(.*):(\d+):\d+$") { $file = $Matches[1] -replace '\\', '/'; $ln = [int]$Matches[2] }
    $symOf[$vaList[$k]] = @{ fn = $fn; file = $file; line = $ln }
    $k++; $i += 2
}

function Describe($sc, $fr) {
    $mi, $rva = $fr.Split(":")
    if ([int]$mi -eq $sc.exe) {
        $s = $symOf[$imageBase + [Convert]::ToUInt64($rva, 16)]
        if ($s) { return $s }
        return @{ fn = "?"; file = ""; line = 0 }
    }
    $m = $sc.mods[[int]$mi]; $leaf = if ($m) { Split-Path $m -Leaf } else { "?" }
    return @{ fn = "[$leaf]"; file = $leaf; line = 0 }
}

function Classify($stack) {
    # stack: leaf first. Cost = what the CPU does; origin = which subsystem asked for it.
    $fns = ($stack | ForEach-Object { $_.fn }) -join " | "
    $files = ($stack | ForEach-Object { $_.file }) -join " | "
    $cost =
        if ($fns -match "Win32Window::present") { "present" }
        elseif ($fns -match 'computeEnv' -and $fns -match 'segmentHit|World::load\(.*\)::\$_') { "visibility" }
        elseif ($fns -match "computeEnv") { "lighting" }
        elseif ($fns -match "CollisionWorld::") { "collision" }
        elseif ($fns -match "Pipeline::(programFor|buildProgram|texture|cubeTexture|upload)(\(|\s|$)") { "first_use" }   # symbolized names may carry a parameter list
        elseif ($fns -match "skinPose|Pipeline::buildVertices|Pipeline::drawDynamic") { "skinning" }
        elseif ($fns -match "WeaponFx::|VehicleFx::|drawParticles|drawSprites|drawFx") { "particles" }
        elseif ($fns -match "SoundCues::|RobotFoley::" -or $files -match "Win32Audio|SpyAudio") { "audio" }
        elseif ($fns -match "WeaponMesh::|tickWeaponPresentation|handleWeaponNotify|fireHitscan|Recoil") { "weapon" }
        elseif ($fns -match "samplePose|blendPose|evaluatePose|poseGlobals|Character::updateAnimation|Character::finalizePose" -or $files -match "SkinnedModel") { "anim" }
        elseif ($files -match "src/render/|opengl32|atio|amd|nvogl|ig\w+icd") { "render" }
        elseif ($fns -match "CharacterMovement|PlayerController|Weapon::") { "gameplay" }
        elseif ($fns -match "World::tick|Application::run") { "sim" }
        else { "other" }
    $origin = "other"
    $drawing = $false
    foreach ($f in $stack) {
        if ($f.fn -match "Pipeline::(draw|drawSubs|computeEnv)\b|drawMesh") { $drawing = $true }
        if ($f.fn -match "WeaponFx::draw") { $origin = $(if ($drawing) { "shell_mag" } else { "weapon_fx" }); break }   # mesh particles (shells/mags) go through drawMesh
        if ($f.fn -match "WeaponFx::") { $origin = "weapon_fx"; break }
        if ($f.fn -match "VehicleFx::") { $origin = "vehicle_fx"; break }
        if ($f.fn -match "fireHitscan") { $origin = "hitscan"; break }
        if ($f.fn -match "PlayerController::applyToPawn" -and $fns -match "CollisionWorld::segmentHit") { $origin = "camera_aim"; break }
        if ($f.fn -match "SoundCues::|RobotFoley::|tickEngineAudio" -or $f.file -match "Win32Audio") { $origin = "audio"; break }
        if ($f.fn -match "Character::(draw|updateAnimation)|WeaponMesh::|World::draw.*weapon") { $origin = "character"; break }
        if ($f.fn -match "CharacterMovement::") { $origin = "movement"; break }
        if ($f.fn -match "Win32Window::present") { $origin = "present"; break }
    }
    if ($origin -eq "other") {
        if ($fns -match "World::draw|GLRenderer::(beginFrame|endFrame)|Pipeline::(beginFrame|endFrame)") { $origin = "world" }
        elseif ($fns -match "World::tick|Application::run") { $origin = "sim" }
    }
    return @($cost, $origin)
}

# ---- phases --------------------------------------------------------------------------------------
function Phases($name, $frames) {
    $fs = @($frames | Where-Object { [double]$_.t -gt 0 })
    $t0 = [double]$fs[0].t + 2.0   # skip the first 2 s after the first frame (warm-up)
    $ph = @()
    $shots = @(); for ($i = 1; $i -lt $fs.Count; $i++) { if ([int]$fs[$i].ammo -lt [int]$fs[$i - 1].ammo) { $shots += [double]$fs[$i].t } }
    switch ($name) {
        "burst" {
            if ($shots.Count) {
                $first = $shots[0]; $last = $shots[-1]
                $ph += @{ n = "burst.firing"; a = $first - 0.02; b = $last + 0.02 }
                $ph += @{ n = "burst.after_0_1s"; a = $last + 0.02; b = $last + 1.0 }
                $ph += @{ n = "burst.after_1_3s"; a = $last + 1.0; b = $last + 3.0 }
                $ph += @{ n = "burst.settled"; a = $last + 3.0; b = [double]$fs[-1].t }
                $ph += @{ n = "burst.before"; a = [double]$fs[0].t + 0.5; b = $first - 0.02 }
            }
        }
        "held_fire" {
            if ($shots.Count -ge 2) {
                $ph += @{ n = "held.mag1_0_1s"; a = $shots[0] - 0.02; b = $shots[0] + 1.0 }
                $ph += @{ n = "held.mag1_2_3s"; a = $shots[0] + 2.0; b = $shots[0] + 3.2 }
                $ph += @{ n = "held.all"; a = $shots[0]; b = [double]$fs[-1].t }
            }
        }
        default { $ph += @{ n = "$name.steady"; a = $t0; b = [double]$fs[-1].t } }
    }
    return $ph
}

$report = New-Object System.Collections.Generic.List[object]
$costs = "present", "visibility", "lighting", "collision", "first_use", "skinning", "particles", "audio", "weapon", "anim", "render", "gameplay", "sim", "other"
$origins = "shell_mag", "weapon_fx", "vehicle_fx", "character", "hitscan", "camera_aim", "movement", "world", "audio", "sim", "present", "other"
foreach ($name in $scen.Keys) {
    $sc = $scen[$name]
    # profiler clock starts at static init, frame clock at process launch: align on the first sample in
    # the main loop vs the first logged frame (both within one frame of each other).
    $loopSamples = @($sc.samples | Where-Object { ($_.f -join " ") -ne "" })
    $classified = foreach ($s in $sc.samples) {
        $stack = @($s.f | ForEach-Object { Describe $sc $_ })
        $c = Classify $stack
        [pscustomobject]@{ t = $s.t; cost = $c[0]; origin = $c[1]; inLoop = (($stack | ForEach-Object { $_.fn }) -join " ") -match "Application::run" }
    }
    $firstLoop = ($classified | Where-Object inLoop | Select-Object -First 1).t
    $firstFrame = [double]($sc.frames | Where-Object { [double]$_.t -gt 0 } | Select-Object -First 1).t
    $off = if ($firstLoop) { $firstFrame - $firstLoop } else { 0 }
    foreach ($ph in (Phases $name $sc.frames)) {
        $fr = @($sc.frames | Where-Object { [double]$_.t -ge $ph.a -and [double]$_.t -lt $ph.b -and [double]$_.dt -gt 0 })
        if ($fr.Count -lt 3) { continue }
        $ms = ($fr | Measure-Object -Property dt -Average).Average * 1000
        $sorted = @($fr | ForEach-Object { [double]$_.dt * 1000 } | Sort-Object)
        $p95 = $sorted[[Math]::Min($sorted.Count - 1, [int]($sorted.Count * 0.95))]
        $ss = @($classified | Where-Object { ($_.t + $off) -ge $ph.a -and ($_.t + $off) -lt $ph.b })
        $row = [ordered]@{ phase = $ph.n; frames = $fr.Count; frame_ms = [Math]::Round($ms, 2); p95_ms = [Math]::Round($p95, 2)
                           particles = [int](($fr | Measure-Object -Property particles -Maximum).Maximum)
                           meshes = [int](($fr | Measure-Object -Property meshes -Maximum).Maximum); samples = $ss.Count }
        foreach ($c in $costs) { $row["cost_$c"] = if ($ss.Count) { [Math]::Round($ms * @($ss | Where-Object cost -eq $c).Count / $ss.Count, 2) } else { 0 } }
        foreach ($o in $origins) { $row["origin_$o"] = if ($ss.Count) { [Math]::Round($ms * @($ss | Where-Object origin -eq $o).Count / $ss.Count, 2) } else { 0 } }
        $report.Add([pscustomobject]$row)
    }
    # Accumulation test (held fire): frame time vs cumulative shots and vs live mesh particles.
    if ($name -eq "held_fire") {
        $fs = @($sc.frames | Where-Object { [double]$_.dt -gt 0 })
        $cum = 0; $pts = foreach ($i in 1..($fs.Count - 1)) { if ([int]$fs[$i].ammo -lt [int]$fs[$i - 1].ammo) { $cum++ }; [pscustomobject]@{ x = $cum; m = [int]$fs[$i].meshes; y = [double]$fs[$i].dt * 1000 } }
        foreach ($axis in "x", "m") {
            $n = $pts.Count; $mx = ($pts | Measure-Object -Property $axis -Average).Average; $my = ($pts | Measure-Object -Property y -Average).Average
            $sxy = 0; $sxx = 0; $syy = 0
            foreach ($p in $pts) { $dx = $p.$axis - $mx; $dy = $p.y - $my; $sxy += $dx * $dy; $sxx += $dx * $dx; $syy += $dy * $dy }
            $slope = if ($sxx) { $sxy / $sxx } else { 0 }; $r = if ($sxx -and $syy) { $sxy / [Math]::Sqrt($sxx * $syy) } else { 0 }
            $label = if ($axis -eq "x") { "ms_per_cumulative_shot" } else { "ms_per_live_mesh_particle" }
            $report.Add([pscustomobject]@{ phase = "held_fire.$label"; frames = $n; frame_ms = [Math]::Round($slope, 4); p95_ms = [Math]::Round($r, 3); samples = 0 })
        }
    }
}
# Inclusive hot functions and segmentHit callers during the firing phases (who pays, who asks).
$hot = New-Object System.Collections.Generic.List[string]
foreach ($name in @("held_fire", "burst") | Where-Object { $scen.ContainsKey($_) }) {
    $sc = $scen[$name]
    $fs = @($sc.frames | Where-Object { [double]$_.dt -gt 0 })
    $firing = @(for ($i = 1; $i -lt $fs.Count; $i++) { if ([int]$fs[$i].ammo -lt [int]$fs[$i - 1].ammo) { $fs[$i] } })
    if (-not $firing.Count) { continue }
    $a = [double]$firing[0].t - 0.4; $b = [double]$firing[-1].t
    $firstLoopT = $null
    $incl = @{}; $callers = @{}; $n = 0
    foreach ($s in $sc.samples) {
        $stack = @($s.f | ForEach-Object { Describe $sc $_ })
        $joined = ($stack | ForEach-Object { $_.fn }) -join " "
        if (-not $firstLoopT -and $joined -match "Application::run") { $firstLoopT = $s.t }
    }
    $off = [double]($sc.frames | Where-Object { [double]$_.t -gt 0 } | Select-Object -First 1).t - $firstLoopT
    foreach ($s in $sc.samples) {
        $t = $s.t + $off
        if ($t -lt $a -or $t -gt $b) { continue }
        $n++
        $stack = @($s.f | ForEach-Object { Describe $sc $_ })
        $seen = @{}
        foreach ($fr in $stack) { $key = ($fr.fn -replace '\(.*$', ''); if (-not $seen[$key]) { $seen[$key] = 1; $incl[$key] = 1 + $incl[$key] } }
        for ($j = 0; $j -lt $stack.Count - 1; $j++) {
            if ($stack[$j].fn -match "CollisionWorld::segmentHit") {
                $cf = $stack[$j + 1]
                $cname = $cf.fn -replace '\(.*$', ''
                $c = $cname + " (" + [IO.Path]::GetFileName($cf.file) + ":" + $cf.line + ")"
                $callers[$c] = 1 + $callers[$c]; break
            }
        }
    }
    $hot.Add("== $name firing window: $n samples")
    $incl.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 22 | ForEach-Object { $hot.Add(("  {0,5:P1}  {1}" -f ($_.Value / [Math]::Max(1, $n)), $_.Key)) }
    $hot.Add("  -- segmentHit direct callers:")
    $callers.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 8 | ForEach-Object { $hot.Add(("  {0,5:P1}  {1}" -f ($_.Value / [Math]::Max(1, $n)), $_.Key)) }
}
$hot | Set-Content (Join-Path $PerfDir "hot_functions.txt")
$hot | Write-Output

$csv = Join-Path $PerfDir "attribution.csv"
$report | Export-Csv $csv -NoTypeInformation
$report | Format-Table phase, frames, frame_ms, p95_ms, meshes, particles, cost_present, cost_visibility, cost_lighting, cost_collision, cost_first_use, cost_skinning, cost_particles, cost_render, cost_anim, cost_audio, cost_weapon, cost_gameplay, cost_sim -AutoSize | Out-String -Width 260
$report | Where-Object { $_.samples -gt 0 } | Format-Table phase, frame_ms, origin_shell_mag, origin_weapon_fx, origin_vehicle_fx, origin_character, origin_hitscan, origin_camera_aim, origin_movement, origin_world, origin_audio, origin_sim, origin_present, origin_other -AutoSize | Out-String -Width 260
"-> $csv"

# ---- owning lane per cost (for handoffs) ----
$lane = [ordered]@{ first_use = "Rendering (first-use program/texture builds)"; collision = "Gameplay (queries) / Systems (CollisionWorld::segmentHit traversal fix)"; visibility = "Rendering (light visibility rays)"
                    lighting = "Rendering (dynamic light selection / env)"; skinning = "Rendering + Gameplay (CPU skinning / upload)"; render = "Rendering"
                    particles = "Systems (effects)"; audio = "Systems"; weapon = "Gameplay"; anim = "Gameplay"; gameplay = "Gameplay"; sim = "Gameplay"; present = "GPU/driver" }
$top = $report | Where-Object { $_.phase -match "held.mag1_2_3s|burst.firing" } | Select-Object -First 1
if ($top) {
    "PRIMARY COSTS in $($top.phase) ($($top.frame_ms) ms/frame):"
    $lane.Keys | ForEach-Object { [pscustomobject]@{ cost = $_; ms = $top."cost_$_"; owner = $lane[$_] } } | Sort-Object ms -Descending | Select-Object -First 6 | Format-Table -AutoSize | Out-String -Width 200
}
