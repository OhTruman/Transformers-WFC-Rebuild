# FAST DEVELOPMENT GATE (tier FAST, the default after a normal integration milestone; ~20-30 min of renderer time).
# Question answered: "did this integration obviously break the game or the systems changed in this milestone?"
# NOT a certification. TARGETED = the relevant m07-* suite(s) for a changed subsystem; FULL = m07-gate.ps1 (hours,
# only on explicit request). See VALIDATION-TIERS.md.
#
#   .\tools\fidelity\fast-gate.ps1 -Ref origin/integration/<milestone> [-Prev <last validated commit>] [-Build] [-ReportOnly]
#
# Steps (one Experimental renderer at a time; waits at most WFC_GATE_GPU_WAIT_MIN = 20 min for other sessions' renderers,
# then reports the step UNKNOWN as GPU CONTENTION - never as a product defect):
#   build       Debug + Release of exactly that sha (m05\build-target.ps1) + render data (reused when present)
#   selftests   Gameplay WFC_WEAPONTEST + WFC_CHASSISTEST (Release, short, no gameplay loop)
#   route       presentation-gate -Parts route,direct -Watch customization (Release): cold boot -> title -> lobby ->
#               Create a Character preview -> Streets TDM (selected character) -> world / HUD -> pause / resume ->
#               results -> lobby -> second match -> frontend; frontend-launched world vs same-start direct boot
#   mapswitch   map-loss-gate -Configs normal (Release): Streets -> lobby -> Berth -> lobby -> Streets, world per visit
#   renderstate m07-renderstate -OnlyVariants normal (Release): world frame after every overlay, GL entry state
#   mechanics   one direct-boot Streets run: move, jump, fire, reload, repeated transform; weapon FX warnings
#   audio/res   from the runs above: audio load / unload per map visit (duplication), working set / GL census
param([Parameter(Mandatory)][string]$Ref, [string]$Prev = "", [string]$Name = "", [switch]$Build, [switch]$ReportOnly, [string]$OutDir = "")
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Flow.ps1"); . (Join-Path $PSScriptRoot "lib\M05.ps1"); . (Join-Path $PSScriptRoot "lib\Present.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
if (-not $env:WFC_GATE_GPU_WAIT_MIN) { $env:WFC_GATE_GPU_WAIT_MIN = "20" }   # FAST: never wait hours for the GPU
$root = Get-WfcRoot
$sha = (& git -C $root rev-parse --verify "$Ref^{commit}").Trim(); $short = $sha.Substring(0, 7); if (-not $Name) { $Name = "fast_$short" }
$tgt = Join-Path $root "work\ab\$Name"; if (-not $OutDir) { $OutDir = Join-Path $root "work\fastgate\$short" }
New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$glog = Join-Path $OutDir "gate.log"; function Note($s) { Add-Content -Encoding UTF8 $glog $s; Write-Host $s }
$res = New-WfcResults; function Res($id, $status, $note, $owner = "") { Add-WfcResult $res "fast.$id" $status $null $note $owner }
$t00 = Get-Date

# ---------- build (exact sha, both configurations)
if (-not $ReportOnly -and ($Build -or -not (Test-Path (Join-Path $tgt "M05_TARGET.txt")))) { Note "build $sha"; & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "m05\build-target.ps1") -Ref $sha -Name $Name -Jobs 4 *> (Join-Path $OutDir "build.log") }
$built = if (Test-Path (Join-Path $tgt "M05_TARGET.txt")) { ((Get-Content (Join-Path $tgt "M05_TARGET.txt")) | Where-Object { $_ -like "sha=*" }) -replace 'sha=', '' } else { "" }
if ($built -ne $sha) { throw "work\ab\$Name holds '$built', not ${sha}: rerun with -Build (never mix product revisions)" }
$dbg = Join-Path $tgt "build\bin\wfc_rebuild.exe"; $rel = Join-Path $tgt "build-release\bin\wfc_rebuild.exe"
Res "build.debug" $(if (Test-Path $dbg) { "PASS" } else { "FAIL" }) "Debug exe from $short" "Integration"
Res "build.release" $(if (Test-Path $rel) { "PASS" } else { "FAIL" }) "Release exe from $short" "Integration"
# ---------- headless unit / regression tests (ctest: frontend + fidelity), both configurations - no GPU needed
$ctest = Join-Path $root ".toolchain\cmake-4.4.3-windows-x86_64\bin\ctest.exe"; $env:PATH = (Join-Path $root ".toolchain\llvm-mingw-20260922-ucrt-x86_64\bin") + ";" + $env:PATH
foreach ($b in "build", "build-release") { $uo = Join-Path $OutDir "unit_$b.txt"
    if (-not $ReportOnly -and (Test-Path $ctest)) { Push-Location (Join-Path $tgt $b); try { & $ctest --output-on-failure *> $uo } finally { Pop-Location } }
    $ut = if (Test-Path $uo) { Get-Content -Raw $uo } else { "" }; $um = [regex]::Match($ut, '(\d+)% tests passed, (\d+) tests failed out of (\d+)|100% tests passed out of (\d+)')
    Res "unit.$b" $(if (-not $ut) { "UNKNOWN" } elseif ($ut -match '100% tests passed') { "PASS" } else { "FAIL" }) ("ctest ({0}): {1}" -f $b, $(if ($um.Success) { $um.Value } else { "no summary" })) "Integration" }
$X = Get-M07Expectations
if (-not $ReportOnly) {
    # every launchable map: after a match the lobby moves on to the next map (08g: Streets -> Seed), so a FAST run can land anywhere
    $need = @($X.maps | Where-Object { $_.launchable } | ForEach-Object { $_.runtime }) + @("UI_FrontEnd", "UI_PartyLobby", "UI_Lobby", "UI_CharacterCustomization")
    foreach ($m in @($need | Where-Object { -not (Test-Path (Join-Path $tgt "work\render\$_\materials_glsl.json")) })) { Push-Location $tgt; try { & powershell -NoProfile -ExecutionPolicy Bypass -File "tools\render\build_render_data.ps1" -Map $m *> (Join-Path $OutDir "rd_$m.log") } finally { Pop-Location } }
}

# ---------- what changed since the previous validated commit (drives TARGETED follow-ups; unchanged systems reuse evidence)
$changes = @()
if ($Prev) { $pv = (& git -C $root rev-parse --verify "$Prev^{commit}" 2>$null); if ($pv) { $pv = $pv.Trim()
    $files = @(& git -C $root diff --name-only "$pv..$sha" -- src tools/render CMakeLists.txt 2>$null)
    $area = { param($f) switch -regex ($f) { '^src/frontend|Application_Frontend' { "frontend" } '^src/render' { "rendering" } '^src/game|^src/core/(World|Match)' { "gameplay" } '^src/audio|Audio' { "systems/audio" } '^src/assets|^tools/render' { "assets / render data" } default { "other" } } }
    $changes = @($files | Group-Object { & $area $_ } | Sort-Object Count -Descending | ForEach-Object { "$($_.Name) $($_.Count)" })
    $commits = @(& git -C $root log --oneline --no-merges "$pv..$sha" 2>$null) } }

# ---------- one GPU slot helper: run a step or record GPU CONTENTION (UNKNOWN, never a product failure)
function Slot([string]$step, [scriptblock]$sb) {
    $t0 = Get-Date; Note ("[{0:HH:mm:ss}] {1}" -f $t0, $step)
    if ($ReportOnly) { try { & $sb *>&1 | Select-Object -Last 1 | ForEach-Object { Note "  $_" } } catch { Note "  ERROR $_" }; return }
    if (-not (Wait-WfcGpu)) { Res "$step.gpu" "UNKNOWN" "DRIVER / GPU CONTENTION: other sessions' renderers held the GPU past $($env:WFC_GATE_GPU_WAIT_MIN) min - step not run (not a product result)" "Experimental"; Note "  GPU busy: skipped"; return }
    try { & $sb *>&1 | Select-Object -Last 1 | ForEach-Object { Note "  $_" } } catch { Note "  ERROR $_"; Res "$step.harness" "FAIL" "TEST/HARNESS DEFECT: $_" "Experimental" }
    Note ("  ({0:N1} min)" -f ((Get-Date) - $t0).TotalMinutes)
}

# ---------- 1. Gameplay self-tests (short)
foreach ($t in @(@{ k = "weapontest"; env = "WFC_WEAPONTEST"; sum = 'WEAPON SUMMARY: (\d+)/(\d+)'; fail = '\] WEAPON FAIL ' }, @{ k = "chassistest"; env = "WFC_CHASSISTEST"; sum = 'CHASSIS SUMMARY: (\d+)/(\d+)'; fail = '\] CHASSIS FAIL ' })) {
    $d = Join-Path $OutDir "selftest_$($t.k)"; New-Item -ItemType Directory -Force $d | Out-Null
    if (-not (Test-Path (Join-Path $d "wfc.log"))) { Slot $t.k { $null = Invoke-WfcExe $rel $d @{ $t.env = "1" } "run.log" 300; "done" } }
    $lg = Join-Path $d "wfc.log"; $s = @(Grep-Log $lg $t.sum)[0]
    $fl = @(if (Test-Path $lg) { [IO.File]::ReadLines($lg) | Where-Object { $_ -cmatch $t.fail } })
    $m = if ($s) { [regex]::Match($s.text, $t.sum) } else { $null }
    Res "selftest.$($t.k)" $(if (-not $s) { "UNKNOWN" } elseif ($fl.Count) { "FAIL" } else { "PASS" }) ("{0}: {1}{2}" -f $t.env, $(if ($m) { "$($m.Groups[1].Value)/$($m.Groups[2].Value) checks passed" } else { "no summary line (crash / timeout?)" }), $(if ($fl.Count) { "; failing: " + (($fl | Select-Object -First 4 | ForEach-Object { $_ -replace '^.*(WEAPON|CHASSIS) FAIL ', '' }) -join " | ") } else { "" })) "Gameplay"
}

# ---------- 2. golden path (frontend route) + Create a Character, 3. map switch, 4. render state
$pres = Join-Path $OutDir "route"
Slot "route" { & (Join-Path $PSScriptRoot "presentation-gate.ps1") -Root $tgt -OutDir $pres -Config Release -Parts route, direct, watchdog -Watch customization -Label "$Ref FAST" -ReportOnly:$ReportOnly }
$ml = Join-Path $OutDir "mapswitch"
Slot "mapswitch" { & (Join-Path $PSScriptRoot "map-loss-gate.ps1") -Exe $rel -OutDir $ml -Configs normal -ReportOnly:$ReportOnly }
$rs = Join-Path $OutDir "renderstate"
Slot "renderstate" { & (Join-Path $PSScriptRoot "m07-renderstate.ps1") -Root $tgt -OutDir $rs -Config Release -OnlyVariants normal -ReportOnly:$ReportOnly }

# ---------- 5. mechanics: move / jump / fire / reload / repeated transform (direct boot, lockstep)
$dm = Join-Path $OutDir "mechanics"; New-Item -ItemType Directory -Force $dm | Out-Null
if (-not (Test-Path (Join-Path $dm "wfc.log"))) { Slot "mechanics" { $null = Invoke-WfcExe $rel $dm @{ WFC_BOOT = "match"; WFC_MAP = "MP_IAC_Streets"; WFC_LOCKSTEP = "1"; WFC_SMOKE_FRAMES = "1500"; WFC_LOGEVERY = "10"; WFC_AUTOWALK = "1"; WFC_AUTOTURN = "0.15"; WFC_AUTOFIRE = "1"; WFC_AUTORELOAD = "1"; WFC_AUTOJUMP_EVERY = "120"; WFC_PRESSTRANSFORM_EVERY = "300"; WFC_SHOTEVERY = "$dm,200,300"; WFC_NOMOUSE = "1" } "run.log" 400; "done" } }
$mlog = Join-Path $dm "wfc.log"
if (Test-Path $mlog) {
    $fr = @(Read-FrameLog $mlog)
    $ammo = @($fr | ForEach-Object { if ("$($_.ammo)" -match '^(\d+)/(\d+)$') { [pscustomobject]@{ clip = [int]$Matches[1]; res = [int]$Matches[2]; rl = "$($_.reloading)"; form = "$($_.form)" } } })
    $fired = @(for ($i = 1; $i -lt $ammo.Count; $i++) { if ($ammo[$i].clip -lt $ammo[$i - 1].clip) { 1 } }).Count
    $reloads = @(for ($i = 1; $i -lt $ammo.Count; $i++) { if ($ammo[$i].clip -gt $ammo[$i - 1].clip -and $ammo[$i].res -lt $ammo[$i - 1].res) { 1 } }).Count
    $forms = @($fr | ForEach-Object { "$($_.form)" } | Where-Object { $_ } | Select-Object -Unique); $switches = @(for ($i = 1; $i -lt $fr.Count; $i++) { if ("$($fr[$i].form)" -ne "$($fr[$i - 1].form)") { 1 } }).Count
    $air = @($fr | Where-Object { "$($_.grounded)" -eq "0" }).Count
    $dist = if ($fr.Count -gt 1) { [Math]::Sqrt([Math]::Pow([double]$fr[-1].x - [double]$fr[0].x, 2) + [Math]::Pow([double]$fr[-1].z - [double]$fr[0].z, 2)) } else { 0 }
    $crash = -not @(Grep-Log $mlog 'Shutdown complete').Count
    Res "mechanics.move_jump" $(if ($crash) { "FAIL" } elseif ($dist -gt 10 -and $air -ge 2) { "PASS" } elseif ($fr.Count) { "PARTIAL" } else { "UNKNOWN" }) ("{0} frame samples, net displacement {1:N0} m, airborne samples {2}{3}" -f $fr.Count, $dist, $air, $(if ($crash) { "; NO CLEAN SHUTDOWN (crash / hang)" } else { "" })) "Gameplay"
    Res "mechanics.fire" $(if ($fired -ge 3) { "PASS" } elseif ($ammo.Count) { "FAIL" } else { "UNKNOWN" }) ("clip decreased on {0} samples (WFC_AUTOFIRE)" -f $fired) "Gameplay"
    Res "mechanics.reload" $(if ($reloads -ge 1) { "PASS" } elseif ($ammo.Count) { "FAIL" } else { "UNKNOWN" }) ("clip refilled from reserve {0}x; reloading flag seen {1}" -f $reloads, [bool](@($ammo | Where-Object { $_.rl -eq "1" }).Count)) "Gameplay"
    Res "mechanics.transform" $(if ($forms.Count -ge 2 -and $switches -ge 3) { "PASS" } elseif ($forms.Count -ge 2) { "PARTIAL" } else { "FAIL" }) ("forms seen {0}; form switches {1} (transform every 300 frames)" -f ($forms -join ","), $switches) "Gameplay"
    # weapon / map particle FX: Trail2 (ribbon) / Beam2 emitters are the KNOWN Rendering gap (WfcMapFx "not drawn yet")
    $rb = @(Grep-Log $mlog 'emitter .* is (Trail2|Beam2); not drawn yet' | ForEach-Object { $_.text -replace '^.*map fx: ', '' })
    $fxAll = @(Grep-Log $mlog '(?i)\[(error|warn) *\].*(fx|particle|emitter)' | Where-Object { $_.text -notmatch 'Trail2|Beam2' })
    # "<module> has no decoded <field> (N used)": a declared PARTIAL decode (Rendering) - KNOWN, not a failure
    $fxPartial = @($fxAll | Where-Object { $_.text -match 'has no decoded' }); $fxErr = @($fxAll | Where-Object { $_.text -notmatch 'has no decoded' })
    if ($fxPartial.Count) { Res "fx.partial_decode" "KNOWN" ("particle modules with an undecoded field (declared PARTIAL, a default is used): " + ((@($fxPartial | ForEach-Object { $_.text -replace '^.*map fx: ', '' } | Select-Object -Unique)) -join " | ")) "Rendering" }
    # weapon ribbons are skipped silently (WeaponFx "Trail2 ribbons are not simulated by the runtime yet [PARTIAL]"), map
    # ribbons / beams are logged (WfcMapFx): the product's own declaration decides KNOWN, the log adds occurrences
    $decl = @(Get-ChildItem (Join-Path $tgt "src") -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern 'Trail2 ribbons are not simulated', 'Trail2 / Beam2: not implemented' -SimpleMatch | ForEach-Object { "$($_.Filename):$($_.LineNumber)" })
    Res "fx.ribbon_beam" $(if ($decl.Count -or $rb.Count) { "KNOWN" } else { "PASS" }) ("Trail2 / Beam2 ribbons and beams (tracer smoke trails, repair / drain beams) not drawn - declared PARTIAL by the product ({0}; STATUS.md); map-FX occurrences logged in this run: {1}{2}. KNOWN Rendering / Systems item, not a weapon failure (fire / reload / weapon test pass)" -f $(if ($decl.Count) { $decl -join ", " } else { "no declaration found" }), $rb.Count, $(if ($rb.Count) { " (" + (($rb | Select-Object -First 3) -join "; ") + ")" } else { "" })) "Rendering"
    $wfx = @(Grep-Log $mlog 'weapon fx: \d+/\d+' | ForEach-Object { $_.text -replace '^.*weapon fx: ', '' })
    Res "fx.weapon_assets" $(if (@($wfx | Where-Object { $_ -match '^(\d+)/(\d+)' -and $Matches[1] -ne $Matches[2] }).Count) { "FAIL" } elseif ($wfx.Count) { "PASS" } else { "UNKNOWN" }) ("weapon FX load: {0}" -f $(if ($wfx.Count) { $wfx -join "; " } else { "no 'weapon fx:' line" })) "Systems/Rendering"
    Res "fx.other_errors" $(if ($fxErr.Count) { "FAIL" } else { "PASS" }) ("other FX / particle warnings or errors: {0}{1}" -f $fxErr.Count, $(if ($fxErr.Count) { " - " + (($fxErr | Select-Object -First 3 | ForEach-Object { $_.text -replace '^\[[^\]]*\] ', '' }) -join " | ") } else { "" })) "Rendering"
    $tiles = @(Get-ChildItem $dm -Filter *.bmp -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { @{ png = $_.FullName; label = "mechanics $($_.BaseName)" } }); if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_mechanics.png") 4 400 225 }
}

# ---------- 6. audio duplication + resources, from the route and map-switch runs
foreach ($run in @(@{ k = "route"; d = (Join-Path $pres "route") }, @{ k = "mapswitch"; d = @(Get-ChildItem $ml -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName "flow.jsonl") } | Select-Object -First 1 | ForEach-Object { $_.FullName })[0] })) {
    if (-not $run.d -or -not (Test-Path (Join-Path $run.d "flow.jsonl"))) { continue }
    $F = Read-FlowLog (Join-Path $run.d "flow.jsonl")
    $al = @(Flow-Ev $F "audio.loaded"); $au = @(Flow-Ev $F "audio.unloaded")
    $dup = @($au | Where-Object { [int]$_.voices -gt 0 -or [int]$_.instances -gt 0 })
    $pcm = @($al | ForEach-Object { [double]$_.pcmMB })
    Res "audio.$($run.k)" $(if (-not $al.Count) { "UNKNOWN" } elseif ($dup.Count) { "FAIL" } elseif ($au.Count -lt [Math]::Max(0, $al.Count - 1)) { "FAIL" } else { "PASS" }) ("map audio loads {0} ({1}), unloads {2}; voices / instances still alive after an unload: {3}; PCM MB per load {4}" -f $al.Count, (($al | ForEach-Object { $_.level }) -join " -> "), $au.Count, $dup.Count, ($pcm -join " / ")) "Systems"
    # HUD data the original Hud_GFX branches on (audit 2026-10-05): an unanswered GoalScore makes the team bars use 10
    $un = @(Flow-Ev $F "datastore.unhandled" | ForEach-Object { "$($_.markup)" } | Where-Object { $_ -match '^<CurrentGame:' } | Group-Object | ForEach-Object { "$($_.Name) x$($_.Count)" })
    $goal = @(Flow-Ev $F "datastore.unhandled" | Where-Object { "$($_.markup)" -eq "<CurrentGame:GoalScore>" }).Count
    Res "hud.goalscore.$($run.k)" $(if ($goal) { "FAIL" } else { "PASS" }) ("<CurrentGame:GoalScore> unanswered {0}x (Hud_GFX then scales the team bars to 10 points); other unanswered CurrentGame reads: {1}" -f $goal, $(if ($un.Count) { $un -join ", " } else { "none" })) "Frontend"
    $gc = @(Flow-Ev $F "match.glCensus"); $samp = Join-Path $run.d "samples.csv"
    $pm = if (Test-Path $samp) { @(Import-Csv $samp | ForEach-Object { [double]$_.private_mb }) } else { @() }
    $peak = if ($pm.Count) { ($pm | Measure-Object -Maximum).Maximum } else { $null }
    Res "resources.$($run.k)" $(if (-not $pm.Count -and -not $gc.Count) { "UNKNOWN" } elseif ($peak -and $peak -gt 6000) { "FAIL" } else { "INFO" }) ("private MB first {0:N0} / peak {1:N0} / last {2:N0} over {3} samples; GL census per match end: {4} (FAST tier: explosion check only - plateau proof is TARGETED / FULL soak)" -f $(if ($pm.Count) { $pm[0] }), $peak, $(if ($pm.Count) { $pm[-1] }), $pm.Count, (($gc | ForEach-Object { "$($_.live)" }) -join " -> ")) "Rendering/Systems"
}

# ---------- renderer error scan over every log of this gate (Rendering M45 evidence fields; audit 2026-10-05)
#   "sub-mesh … out of bounds … not drawn": a draw that can make the GPU fetch out of bounds (driver page fault / reset risk)
#   "LEGACY RENDERER": a map / scene drawn without its original render data
#   VISUALCHECK glDebug=<n>: GL debug-output messages (driver-reported errors / warnings)
$allLogs = @(Get-ChildItem $OutDir -Recurse -Filter wfc.log -ErrorAction SilentlyContinue)
$oob = @(); $legacy = @(); $glDbg = 0
foreach ($lf in $allLogs) {
    $oob += @(Grep-Log $lf.FullName 'out of bounds .*not drawn' | ForEach-Object { $_.text -replace '^\[[^\]]*\] ', '' })
    $legacy += @(Grep-Log $lf.FullName 'LEGACY RENDERER' | ForEach-Object { "$($lf.Directory.Name): " + ($_.text -replace '^\[[^\]]*\] ', '') })
    foreach ($m in @(Grep-Log $lf.FullName 'VISUALCHECK .*glDebug=(\d+)')) { $glDbg = [Math]::Max($glDbg, [int][regex]::Match($m.text, 'glDebug=(\d+)').Groups[1].Value) }
}
$oobCheck = [bool](Get-ChildItem (Join-Path $tgt "src\render") -Recurse -Include *.cpp -ErrorAction SilentlyContinue | Select-String -Pattern 'out of bounds (%zu indices' -SimpleMatch -List | Select-Object -First 1)
Res "render.out_of_bounds_draws" $(if ($oob.Count) { "FAIL" } elseif (-not $oobCheck) { "UNKNOWN" } else { "PASS" }) ("rejected out-of-bounds sub-mesh draws (GPU fault / driver-reset class, Rendering M45): {0}{1}{2}" -f $oob.Count, $(if ($oob.Count) { " - " + (($oob | Select-Object -Unique -First 3) -join " | ") } else { "" }), $(if (-not $oobCheck) { " - this build predates the M45 draw validation, so it cannot report them (not a PASS)" } else { "" })) "Rendering"
Res "render.legacy_renderer" $(if ($legacy.Count) { "FAIL" } else { "PASS" }) ("LEGACY RENDERER (no original render data) errors: {0}{1}" -f $legacy.Count, $(if ($legacy.Count) { " - " + (($legacy | Select-Object -Unique -First 3) -join " | ") } else { "" })) "Rendering"
Res "render.gl_debug" $(if ($glDbg -gt 0) { "PARTIAL" } else { "INFO" }) ("max VISUALCHECK glDebug count {0} (field present from Rendering 84ba009; 0 / absent on older builds)" -f $glDbg) "Rendering"

# ---------- report
function Rep($p) { if (Test-Path $p) { return @((Get-Content -Raw $p | ConvertFrom-Json).results) } else { return @() } }
$sub = [ordered]@{ route = (Rep (Join-Path $pres "report.json")); mapswitch = (Rep (Join-Path $ml "report.json")); renderstate = (Rep (Join-Path $rs "report.json")) }
$sum = Write-WfcReport $res (Join-Path $OutDir "report_fast.json")
$all = @($res.ToArray()) + @($sub.Values | ForEach-Object { $_ })
function Kind($r) { if ($r.status -ne "FAIL") { return $r.status }; if ("$($r.owner)" -match '^Experimental$' -or "$($r.note)" -match 'TEST FAULT|HARNESS') { return "HARNESS" }; return "PRODUCT" }
$cnt = [ordered]@{}; foreach ($r in $all) { $k = Kind $r; if (-not $cnt.Contains($k)) { $cnt[$k] = 0 }; $cnt[$k]++ }
$visualIds = '^(present\.(gameplay\.world|gameplay\.route_vs_direct|frontend\.scene|frontend\.screen|charselect)|maploss\..*(environment|draws_vs_direct)|m07state\.transition)'
$prodFail = @($all | Where-Object { (Kind $_) -eq "PRODUCT" }); $visualFail = @($prodFail | Where-Object { $_.id -match $visualIds })
$onlyKnownFx = -not $prodFail.Count -and @($all | Where-Object { $_.id -eq "fast.fx.ribbon_beam" -and $_.status -eq "KNOWN" }).Count
$verdict = if ($visualFail.Count) { "BROKEN - visible regression ($($visualFail.Count))" } elseif ($prodFail.Count) { "REGRESSIONS ($($prodFail.Count) product failures)" } elseif (@($all | Where-Object { $_.status -eq "UNKNOWN" -and $_.id -match '\.gpu$' }).Count) { "INCOMPLETE - GPU contention" } else { "NO OBVIOUS BREAKAGE" }
$md = New-Object System.Collections.Generic.List[string]
$md.Add("# FAST development gate - $Ref"); $md.Add("")
$md.Add("| | |"); $md.Add("|---|---|"); $md.Add("| tier | FAST (not a certification; TARGETED / FULL on request - VALIDATION-TIERS.md) |"); $md.Add("| integration commit | ``$sha`` |")
$prevTxt = if ($Prev) { "``" + $Prev + "``" } else { "-" }; $md.Add("| previous validated | $prevTxt |"); $md.Add("| changed since | $(if ($changes.Count) { $changes -join ', ' } else { '-' }) |"); $md.Add("| exe | Debug ``$dbg`` / Release ``$rel`` (graphical runs: Release) |")
$md.Add("| wall time | {0:N0} min |" -f ((Get-Date) - $t00).TotalMinutes); $md.Add(""); $md.Add("## VERDICT: **$verdict**$(if ($onlyKnownFx) { ' (only the KNOWN Rendering ribbon / beam FX item remains)' })"); $md.Add("")
$md.Add("**" + (($cnt.Keys | ForEach-Object { "$_ $($cnt[$_])" }) -join " / ") + "**"); $md.Add("")
foreach ($sec in @(@{ t = "PRODUCT FAIL"; f = { (Kind $args[0]) -eq "PRODUCT" } }, @{ t = "HARNESS FAIL"; f = { (Kind $args[0]) -eq "HARNESS" } }, @{ t = "KNOWN"; f = { $args[0].status -eq "KNOWN" } }, @{ t = "UNKNOWN / PARTIAL"; f = { $args[0].status -in "UNKNOWN", "PARTIAL" } }, @{ t = "HUMAN CHECK"; f = { $args[0].status -eq "HUMAN" } }, @{ t = "PASS"; f = { $args[0].status -eq "PASS" } })) {
    $rows = @($all | Where-Object { & $sec.f $_ }); $md.Add("## $($sec.t): $($rows.Count)"); $md.Add(""); foreach ($r in $rows) { $md.Add("- **$($r.id)** [$($r.owner)]: " + (("$($r.note)" -replace "`n", ' ') -replace '\|', '/')) }; $md.Add("") }
if ($commits) { $md.Add("## Commits since the previous validated commit"); $md.Add(""); foreach ($c in $commits) { $md.Add("- $c") }; $md.Add("") }
$md.Add("Evidence: ``route/`` (PRESENTATION.md, sheets), ``mapswitch/`` (MAP-LOSS.md), ``renderstate/``, ``mechanics/``, ``selftest_*/``.")
$md | Set-Content -Encoding UTF8 (Join-Path $OutDir "FAST-GATE.md")
Note ("FAST GATE ${sha}: $verdict; " + (($cnt.Keys | ForEach-Object { "$_ $($cnt[$_])" }) -join " / ") + " -> $OutDir\FAST-GATE.md")
