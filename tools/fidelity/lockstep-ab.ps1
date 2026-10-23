# LOCKSTEP RENDERER A/B (tier TARGETED; Integration 2026-10-10: free-running real-play A/Bs are dominated by match-to-match
# variance - one quiet match moved Gorge's p90 by 0.5 ms and its draw counts by 25 %). Every arm plays the SAME match: direct boot,
# WFC_LOCKSTEP=1 (exactly one fixed sim step per rendered frame), the same WFC_SEED / WFC_FLOWSEED, the same bots and (optionally)
# the bot-driven local player (WFC_PLAYERBOT). Frame times are compared over the same SIM STEP range, aligned through WFC_SIMHASH.
#
# Validity (per arm vs the first arm): the per-step SIMHASH sequence over the measured range must be identical (= the same match,
# frame for frame); and the standing parity check (Integration 2026-10-10): mean draws (total / dyn / fx) and chars must agree
# within 3 %, else the row is UNKNOWN (counts: draws / dyn / fx / program binds; chars CPU is reported, not gated) - a renderer A/B must not change the workload.
# Lockstep caveat: the sim cost per frame is a fixed step, not free-running play - use it for A/Bs, not for the absolute verdict.
#
#   .\tools\fidelity\lockstep-ab.ps1 -OutDir <dir> -Map 510 -Arms "new|work\ab\m9c_47726bc|","old|work\ab\m9c_47726bc|WFC_NOLMARRAYS=1"
#        [-Seed 1234] [-Steps 9000] [-From 1800] [-RenderSize 3840x2160] [-PlayerBot 1] [-Reps 1] [-Batching: the arms after the first are a
#        declared batching / state-sort change - a binds drop <= 10 % is excused, see below]
# HD textures: arms run HD off (lib\Run.ps1 Add-WfcHarnessEnv); an HD-on arm sets "WFC_HDTEXTURES=1;WFC_HD_ROOT=<frozen copy>" in its extra
# (refused before any run without a frozen root), and HD-on arms of a rep must report the same verified pack, else the row is UNKNOWN.
param([Parameter(Mandatory)][string]$OutDir, [Parameter(Mandatory)][string]$Map, [Parameter(Mandatory)][string[]]$Arms,
      [int]$Seed = 1234, [int]$Steps = 9000, [int]$From = 1800, [string]$RenderSize = "", [int]$PlayerBot = 1, [int]$Reps = 1,
      [int]$Bots = 32, [switch]$Batching, [switch]$ReportOnly,
      # -Width / -Height: the profile window (windowed; 2026-10-14 1440p baselines at the display's native 2560x1440 - no mode change)
      [int]$Width = 1920, [int]$Height = 1080)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$Arms = @($Arms | ForEach-Object { "$_" -split ';;' } | Where-Object { $_.Trim() })
$levels = @{ "501" = "MP_IAC_Seed_Base_m"; "502" = "MP_IAC_Berth_Base_m"; "503" = "MP_UND_Complex_BASE_m"; "504" = "MP_IAC_Rust_BASE_m"
             "507" = "MP_ORB_Debris_BASE_m"; "508" = "MP_IAC_Streets_Base_m"; "509" = "MP_KON_Molten_Base_m"; "510" = "MP_UND_Gorge_BASE_m"
             "505" = "MP_ESC_BrokenHope_BASE_m"; "506" = "MP_ESC_Remnant_BASE_m" }   # 505 / 506: Escalation-only maps - booted in ESC (below)
$lvl = $levels["$Map"]; if (-not $lvl) { throw "unknown map $Map" }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "lockstep.$id" $status $null $note $owner }
$url = "{0}?GameModeTag=TDM?BotsAutobot={1}?BotsDecepticon={2}?BotDifficulty=1?ExtendedPlayers=1?PointsToWin=9999?TimeLimit=3600" -f $lvl, ($Bots - 1), $Bots
# (final gate, 2026-10-23) the Escalation-only maps run their own mode: the horde director, the human + friendly bots; -Bots sets the friendlies
if ("$Map" -in "505", "506") { $url = "{0}?GameModeTag=ESC?BotsFriendly={1}" -f $lvl, [Math]::Min(4, [Math]::Max(0, $Bots - 1)) }
$sfRe = 'interval ([\d.]+) ms: render ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)\), outside ([-\d.]+); gpu ([\d.]+) \(world ([-\d.]+), chars ([-\d.]+), fx ([-\d.]+), transl ([-\d.]+), post ([-\d.]+)(?:; [^)]*)?\); draws (\d+) \(dyn (\d+), fx (\d+)\), program binds (\d+)'
function Pct($a, $p) { if (-not $a.Count) { return [double]::NaN }; $s = @($a | Sort-Object); return $s[[Math]::Min($s.Count - 1, [int][Math]::Floor($p * $s.Count))] }
function Mean($a) { if (-not $a.Count) { return [double]::NaN }; return ($a | Measure-Object -Average).Average }
$rows = New-Object System.Collections.Generic.List[object]
# WFC_ASYNCSTEP=0 and WFC_LOCKSTEP_EXACT (Systems / Integration 2026-10-10) are for lockstep IMAGE comparisons only: they serialise the sim step / block on the occlusion query, so frame times are not
# the shipped path - refuse it in this timing harness rather than report its numbers.
$sync = @($Arms | Where-Object { $_ -match 'WFC_ASYNCSTEP\s*=\s*0|WFC_LOCKSTEP_EXACT\s*=' })
if ($sync.Count) { Res "asyncstep" "FAIL" ("WFC_ASYNCSTEP=0 / WFC_LOCKSTEP_EXACT in an arm (" + ($sync -join "; ") + ") - image comparisons only, never timing rows; not run") "Experimental"; $null = Write-WfcReport $res (Join-Path $OutDir "report.json"); "image-only switch refused"; exit 1 }
# HD textures (9116e4ff+): every arm runs HD off (lib\Run.ps1 pins WFC_HDTEXTURES=0) unless its extra sets WFC_HDTEXTURES=1, and then it must
# pin WFC_HD_ROOT to a frozen copy of the pack - checked for all arms BEFORE the first run, so a bad arm cannot leave a half-run A/B.
function ArmEnv([string]$extra) { $h = @{}; foreach ($kv in @($extra -split ';' | Where-Object { $_ -match '=' })) { $i = $kv.IndexOf('='); $h[$kv.Substring(0, $i).Trim()] = $kv.Substring($i + 1) }; return $h }
foreach ($arm in $Arms) {
    $p = $arm -split '\|', 3
    try { $null = Add-WfcHarnessEnv (ArmEnv $(if ($p.Count -gt 2) { $p[2] } else { "" })) }   # (the same pins + check Invoke-WfcExe applies)
    catch { Res "hdroot" "FAIL" ("arm '{0}': {1}; not run" -f $p[0], $_.Exception.Message) "Experimental"; $null = Write-WfcReport $res (Join-Path $OutDir "report.json"); "HD arm without a frozen WFC_HD_ROOT refused"; exit 1 }
}
for ($rep = 1; $rep -le $Reps; $rep++) {
    foreach ($arm in $Arms) {
        $p = $arm -split '\|', 3; $label = $p[0]; $root = (Resolve-Path $p[1]).Path; $extra = if ($p.Count -gt 2) { $p[2] } else { "" }
        $exe = Join-Path $root "build-release\bin\wfc_rebuild.exe"; $H = Get-ExeHooks $exe
        $d = Join-Path $OutDir ("{0}-r{1}" -f $label, $rep); $lg = Join-Path $d "wfc.log"
        if (-not $ReportOnly -and -not (Test-Path $lg)) {
            New-Item -ItemType Directory -Force $d | Out-Null
            $rtArm = [int]$(if ((ArmEnv $extra).WFC_RT_MODE) { (ArmEnv $extra).WFC_RT_MODE } else { 0 })   # (the profile row wins over WFC_RT_MODE at boot)
            (Get-BotProfile 0 0 1 -Width $Width -Height $Height -RayTracing $rtArm) | Set-Content -Encoding ASCII (Join-Path $d "wfc_profile.ini")
            # HD 2 (Enhanced) on a match boot (2026-10-16, package gate 1b read 'HD not on'): the match boot applies the profile's HD row
            # only with the exe's WFC_BOOT_PROFILE hook (01f2316a+), and Get-BotProfile writes HDTextures 0/1 - so for an HD 2 arm the
            # profile row is rewritten to 2 and WFC_BOOT_PROFILE=1 is added below (WFC_HDTEXTURES=2 stays: the frontend's own override)
            $hdArm = "$((ArmEnv $extra).WFC_HDTEXTURES)".Trim()
            $bootProf = ($hdArm -match '^[1-9]') -and $H.Contains("WFC_BOOT_PROFILE")
            if ($bootProf) { $pf = Join-Path $d "wfc_profile.ini"; (Get-Content $pf) -replace '^HDTextures=.*', "HDTextures=$hdArm" | Set-Content -Encoding ASCII $pf }
            if (-not (Wait-WfcGpu)) { Res "$label.r$rep.gpu" "UNKNOWN" "GPU busy - not run" "Experimental"; continue }
            $e = @{ WFC_BOOT = "match"; WFC_MATCH_URL = $url; WFC_LOCKSTEP = "1"; WFC_SEED = "$Seed"; WFC_SMOKE_FRAMES = "$($Steps + 600)"; WFC_LOGEVERY = "0"
                    WFC_NOMOUSE = "1"; WFC_SLOWFRAME = "0.01"; WFC_SIMHASH = "0" }   # every frame: 'interval' = wall frame time (PERF frame= is the lockstep dt)
            if ($H.Contains("WFC_FLOWSEED")) { $e.WFC_FLOWSEED = "$Seed" }
            if ($PlayerBot -ge 0 -and $H.Contains("WFC_PLAYERBOT")) { $e.WFC_PLAYERBOT = "$PlayerBot" }
            if ($RenderSize) { $e.WFC_RENDERSIZE = $RenderSize }
            if ($bootProf) { $e.WFC_BOOT_PROFILE = "1" }
            $ae = ArmEnv $extra; foreach ($k in $ae.Keys) { $e[$k] = $ae[$k] }
            $null = Invoke-WfcExe $exe $d $e "run.log" (1800)
        }
        if (-not (Test-Path $lg)) { Res "$label.r$rep" "UNKNOWN" "no log" "Experimental"; continue }
        $rs = Get-WfcRtState $lg; $rtReq = [int]$(if ((ArmEnv $extra).WFC_RT_MODE) { (ArmEnv $extra).WFC_RT_MODE } else { 0 })
        $hs = Get-WfcHdState $lg; $hdReq = [bool]((Add-WfcHarnessEnv (ArmEnv $extra)).WFC_HDTEXTURES -match '^\s*[1-9]')
        # HD 2 = Enhanced (Integration 2026-10-16): the M6 modern material maps must load too (hd_pbr_index.json beside hd_index.json)
        $hd2Req = [bool]((Add-WfcHarnessEnv (ArmEnv $extra)).WFC_HDTEXTURES -match '^\s*2'); $hdMaps = 0
        $pbrErr = if ($hd2Req -and (Test-Path $lg)) { @(Select-String -Path $lg -Pattern "wfcPbrEnvEnergy' undeclared" -SimpleMatch).Count } else { 0 }
        if ($hd2Req -and (Test-Path $lg)) { $mm = Select-String -Path $lg -Pattern 'HD modern material maps: (\d+)' | Select-Object -First 1; if ($mm) { $hdMaps = [int]$mm.Matches[0].Groups[1].Value } }
        $modOn = if ($hd2Req -and (Test-Path $lg)) { [bool](Select-String -Path $lg -Pattern 'Modern materials on for MP_' -SimpleMatch -Quiet) } else { $false }
        # walk the log: SIMHASH <step> <hash> marks the current sim step; PERF / SLOWFRAME lines belong to it
        $step = -1; $hash = @{}; $ft = New-Object System.Collections.Generic.List[double]; $sf = New-Object System.Collections.Generic.List[object]; $occT = 0; $occS = 0
        foreach ($l in [IO.File]::ReadLines($lg)) {
            $m = [regex]::Match($l, 'SIMHASH (\d+) ([0-9a-f]+)'); if ($m.Success) { $step = [int]$m.Groups[1].Value; $hash[$step] = $m.Groups[2].Value; continue }
            if ($step -lt $From -or $step -ge $Steps) { continue }
            if ($l.Contains('pawn occlusion:')) { $m = [regex]::Match($l, 'pawn occlusion: (\d+) tested, (\d+) part draws skipped'); if ($m.Success) { $occT += [int]$m.Groups[1].Value; $occS += [int]$m.Groups[2].Value }; continue }
            if ($l.Contains('SLOWFRAME f')) { $m = [regex]::Match($l, $sfRe); if ($m.Success) { $v = @($m.Groups | Select-Object -Skip 1 | ForEach-Object { [double]$_.Value }); $ft.Add($v[0]); $sf.Add($v) } }
        }
        $rng = @($hash.Keys | Where-Object { $_ -ge $From -and $_ -lt $Steps } | Sort-Object)
        $all = $sf; $slowF = @($sf | Where-Object { $_[0] -gt 3.333 })
        $col = { param($i) @($slowF | ForEach-Object { $_[$i] } | Where-Object { $_ -ge 0 }) }
        $colA = { param($i) @($all | ForEach-Object { $_[$i] } | Where-Object { $_ -ge 0 }) }
        $rows.Add([pscustomobject][ordered]@{ arm = $label; rep = $rep; steps = $rng.Count; frames = $ft.Count
            p50 = [Math]::Round((Pct $ft 0.5), 2); p90 = [Math]::Round((Pct $ft 0.9), 2); p99 = [Math]::Round((Pct $ft 0.99), 2)
            slow = $slowF.Count; gpu = [Math]::Round((Mean (& $col 8)), 2); gpu_world = [Math]::Round((Mean (& $col 9)), 2); gpu_fx = [Math]::Round((Mean (& $col 11)), 2)
            gpu_transl = [Math]::Round((Mean (& $col 12)), 2); gpu_post = [Math]::Round((Mean (& $col 13)), 2); cpu_render = [Math]::Round((Mean (& $col 1)), 2)
            cpu_chars = [Math]::Round((Mean (& $colA 3)), 2); outside = [Math]::Round((Mean (& $col 7)), 2)
            draws = [Math]::Round((Mean (& $colA 14)), 0); draws_dyn = [Math]::Round((Mean (& $colA 15)), 0); draws_fx = [Math]::Round((Mean (& $colA 16)), 0); binds = [Math]::Round((Mean (& $colA 17)), 0)
            occ_tested = $occT; occ_skipped = $occS; hd_req = $hdReq; hd2_req = $hd2Req; hd_maps = $hdMaps; modern_on = $modOn; pbr_err = $pbrErr; hd_on = $hs.on; hd_pack = $hs.pack; hd_root = $hs.root; rt_req = $rtReq; rt_mode = $rs.mode; rt_modeLogged = $rs.modeLogged; rt_scene = $rs.scene; rt_noshaders = $rs.noShaders
            hashes = ($rng | ForEach-Object { $hash[$_] }) -join ',' })
    }
}
# validity vs the first arm of the same rep: identical SIMHASH over the range + draws / chars parity (within 3 %)
foreach ($r in $rows) {
    $ref = @($rows | Where-Object { $_.rep -eq $r.rep })[0]
    $sameMatch = $r.steps -gt 0 -and $r.hashes -eq $ref.hashes
    $par = { param($a, $b) if (-not $b -or [double]::IsNaN($a) -or [double]::IsNaN($b)) { $true } else { [Math]::Abs($a - $b) / [Math]::Max(1e-6, [Math]::Abs($b)) -le 0.03 } }
    # workload COUNTS only (2026-10-10 validation: chars CPU is a time, it varies with machine noise; counts are the workload)
    $parity = (& $par $r.draws $ref.draws) -and (& $par $r.draws_dyn $ref.draws_dyn) -and (& $par $r.draws_fx $ref.draws_fx) -and (& $par $r.binds $ref.binds)
    # dyn by pawn occlusion (Integration + Experimental 2026-10-11, regress ad4e6c5 -> 5aa2d22): outside WFC_LOCKSTEP_EXACT a pawn query result
    # not yet READY counts as visible, so a faster GPU skips more hidden pawns and dyn drops with the scene unchanged (501-503: dyn -5..-9 per
    # frame, occlusion skips x2, pawns tested equal within 0.03 %; ~2.7 dyn draws per skipped part). Only dyn is excused, only when every other
    # count is in parity, the pawns tested (same pawns in view) agree within 0.5 % and dyn moves against the skips (fewer dyn with more skips).
    $occNote = ""
    if (-not $parity -and (& $par $r.draws $ref.draws) -and (& $par $r.draws_fx $ref.draws_fx) -and (& $par $r.binds $ref.binds) -and $r.occ_tested -gt 0 -and $ref.occ_tested -gt 0 -and
        [Math]::Abs($r.occ_tested - $ref.occ_tested) / $ref.occ_tested -le 0.005 -and [Math]::Sign($r.draws_dyn - $ref.draws_dyn) -eq -[Math]::Sign($r.occ_skipped - $ref.occ_skipped)) {
        $parity = $true
        $occNote = " (dyn {0} vs {1} excused: pawn-occlusion skips {2} vs {3}, pawns tested {4} vs {5})" -f $r.draws_dyn, $ref.draws_dyn, $r.occ_skipped, $ref.occ_skipped, $r.occ_tested, $ref.occ_tested
    }
    # binds by a declared batching / state-sort commit (Integration 2026-10-11, regress 9be437d -> 9c3560e: d8c6720's skin palette batching cut
    # binds 3-5 % on 501-503 with the scene unchanged). Only with -Batching (the caller declares the head a batching change), the same match,
    # binds DOWN by at most 10 %, non-dyn draws and fx in parity and the same characters in view (pawns tested within 0.5 %). Dyn draws are
    # character draws, which batching and occlusion skips both lower, so dyn may only fall. Scored PASS(excused) with the deltas printed.
    if (-not $parity -and $Batching -and $sameMatch -and $r -ne $ref -and $r.binds -lt $ref.binds -and ($ref.binds - $r.binds) / [Math]::Max(1, $ref.binds) -le 0.10 -and
        (& $par ($r.draws - $r.draws_dyn) ($ref.draws - $ref.draws_dyn)) -and (& $par $r.draws_fx $ref.draws_fx) -and $r.draws_dyn -le $ref.draws_dyn -and
        $r.occ_tested -gt 0 -and $ref.occ_tested -gt 0 -and [Math]::Abs($r.occ_tested - $ref.occ_tested) / $ref.occ_tested -le 0.005) {
        $parity = $true
        $occNote += " PASS(excused): declared batching - binds {0} -> {1} ({2:+0.0;-0.0} %), dyn {3} -> {4}, non-dyn draws {5} vs {6}, pawns tested {7} vs {8}" -f $ref.binds, $r.binds,
            (100.0 * ($r.binds - $ref.binds) / $ref.binds), $ref.draws_dyn, $r.draws_dyn, ($r.draws - $r.draws_dyn), ($ref.draws - $ref.draws_dyn), $r.occ_tested, $ref.occ_tested
    }
    # HD state (9116e4ff+): an arm that asked for HD must have it on with a verified pack, and every HD-on arm of the rep must load the
    # same pack ("HD texture pack available: N textures (root)" equal) - else the arms drew different textures: UNKNOWN.
    $hdOk = $true; $hdRef = @($rows | Where-Object { $_.rep -eq $r.rep -and $_.hd_on })[0]
    if ($r.hd_req -and (-not $r.hd_on -or $r.hd_pack -le 0)) { $hdOk = $false; $occNote += " HD requested but not on (pack ignored / incomplete: no 'HD texture pack available' line)" }
    elseif ($r.hd2_req -and $r.pbr_err -gt 0) { $hdOk = $false; $occNote += " HD 2: $($r.pbr_err) PBR programs failed (wfcPbrEnvEnergy undeclared: the exe is older than the materials_pbr data)" }
    elseif ($r.hd2_req -and $r.hd_maps -gt 0 -and -not $r.modern_on) { $hdOk = $false; $occNote += " HD 2: modern material maps loaded but no 'Modern materials on for MP_<map>' line" }
    elseif ($r.hd2_req -and $r.hd_maps -le 0) { $hdOk = $false; $occNote += " HD 2 requested but no modern material maps (no 'HD modern material maps: N' line: the root lacks hd_pbr_index.json)" }
    elseif (-not $r.hd_req -and $r.hd_on) { $hdOk = $false; $occNote += " HD on although not requested" }
    elseif ($r.hd_on -and $hdRef -and ($r.hd_pack -ne $hdRef.hd_pack -or $r.hd_root -ne $hdRef.hd_root)) {
        $hdOk = $false; $occNote += " HD pack differs from '{0}': {1} textures ({2}) vs {3} ({4})" -f $hdRef.arm, $r.hd_pack, $r.hd_root, $hdRef.hd_pack, $hdRef.hd_root }
    elseif ($r.hd_on) { $occNote += " HD on: {0} textures ({1}){2}" -f $r.hd_pack, $r.hd_root, $(if ($r.hd2_req) { "; modern maps $($r.hd_maps)" } else { "" }) }
    # Ray Tracing (5d1aecd0+): the arm's mode must be the one it asked for (WFC_RT_MODE, written into its profile, which wins at
    # boot), an RT-on arm must have built its RT scene with the RT shaders - else the arms drew different things: UNKNOWN.
    # (a mode set at init is inferred from the scene: Faithful vs Realistic is compared only when the renderer logged the mode)
    if (($r.rt_req -eq 0 -and $r.rt_mode -ne 0) -or ($r.rt_req -gt 0 -and $r.rt_mode -eq 0) -or ($r.rt_req -gt 0 -and $r.rt_mode -ne $r.rt_req -and $r.rt_modeLogged)) { $hdOk = $false; $occNote += " Ray Tracing mode {0} but {1} requested (profile vs WFC_RT_MODE?)" -f $r.rt_mode, $r.rt_req }
    elseif ($r.rt_req -gt 0 -and ($r.rt_noshaders -or -not $r.rt_scene)) { $hdOk = $false; $occNote += " Ray Tracing requested but no RT scene{0}" -f $(if ($r.rt_noshaders) { " (build without the RT shaders)" } else { "" }) }
    elseif ($r.rt_req -gt 0) { $occNote += " Ray Tracing mode {0}, scene built" -f $r.rt_req }
    $cm = Join-Path $OutDir ("{0}-r{1}\CONTAMINATED.txt" -f $r.arm, $r.rep); $contam = Test-WfcContaminated $cm
    $crect = Get-WfcClientRect (Join-Path $OutDir ("{0}-r{1}\wfc.log" -f $r.arm, $r.rep))
    if ($crect.differs) { $contam = $true; Res "$($r.arm).r$($r.rep).client_rect" "UNKNOWN" ("INVALID: the game window's client rect differs from the request ({0})" -f $crect.line) "Gameplay" }   # (lane processes only: Run.ps1)
    $st = if (-not $r.steps -or $contam) { "UNKNOWN" } elseif (-not $sameMatch) { "FAIL" } elseif (-not $parity -or -not $hdOk) { "UNKNOWN" } else { "PASS" }
    if ($contam) { Res "$($r.arm).r$($r.rep).contaminated" "UNKNOWN" ("foreign process during the row - rerun: " + ((Get-Content $cm) -join " | ")) "Experimental" }
    Res "$($r.arm).r$($r.rep)" $st ("{0}: steps {1} ({2}-{3}), same match as '{4}' {5}, workload parity {6}$occNote; frame p50 / p90 / p99 {7} / {8} / {9} ms over {10} frames; slow frames {11}: GPU {12} (world {13}, fx {14}, transl {15}, post {16}), CPU render {17}, outside {19}; all frames: chars CPU {18}, draws {20} (dyn {21}, fx {22}), binds {23}" -f `
        $r.arm, $r.steps, $From, $Steps, $ref.arm, $sameMatch, $parity, $r.p50, $r.p90, $r.p99, $r.frames, $r.slow, $r.gpu, $r.gpu_world, $r.gpu_fx, $r.gpu_transl, $r.gpu_post, $r.cpu_render, $r.cpu_chars, $r.outside, $r.draws, $r.draws_dyn, $r.draws_fx, $r.binds) "Experimental"
}
Write-WfcCsv @($rows | Select-Object * -ExcludeProperty hashes) (Join-Path $OutDir "lockstep.csv")
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"LOCKSTEP A/B: " + (($sum.Keys | ForEach-Object { "$_ $($sum[$_])" }) -join " / ")
$res.ToArray() | ForEach-Object { "{0} {1}: {2}" -f $_.status, $_.id, $_.note }
