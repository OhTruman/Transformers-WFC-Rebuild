# Runtime probe: drive the REAL wfc_rebuild.exe through scripted scenarios and turn its logs into
# a fidelity report (same JSON schema as wfc_fidelity, so diff-reports.ps1 compares runs).
# Covers what the windowless harness cannot see: renderer startup, material/texture loads,
# weapon presentation (FX, notifies, cues), boost presentation, plus fixed-camera stills.
#
#   .\tools\fidelity\runtime-probe.ps1 -Exe work\ab\int\build\bin\wfc_rebuild.exe -RenderData work\int-src\work\render -Name int
#
# Output: work\fidelity\probe\<Name>\{report.json, <scenario>.log, <scenario>.png}
# Frame-time-based scenarios (the exe integrates wall-clock time) are judged on presence and
# counts, not exact timings.
param(
    [Parameter(Mandatory)][string]$Exe,
    [string]$RenderData = "",
    [string]$Name = "probe",
    [int]$Frames = 240,
    # startup, fire, reload, boost, transform, stills, materials
    [string[]]$Sections = @("startup", "fire", "reload", "boost", "transform", "stills", "materials")
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Exe = (Resolve-Path $Exe).Path
$out = Join-Path $root "work\fidelity\probe\$Name"
New-Item -ItemType Directory -Force $out | Out-Null
if ($RenderData) { $RenderData = (Resolve-Path $RenderData).Path }

$results = New-Object System.Collections.ArrayList
function Add-Result($group, $id, $status, $measured = $null, $expected = $null, $unit = "", $owner = "", $note = "", $source = "") {
    $o = [ordered]@{ id = "$group.$id"; status = $status }
    if ($null -ne $measured) { $o.measured = [double]$measured }
    if ($null -ne $expected) { $o.expected = [double]$expected; $o.tol = 0 }
    if ($unit) { $o.unit = $unit }
    if ($owner) { $o.owner = $owner }
    if ($source) { $o.source = $source }
    if ($note) { $o.note = $note }
    [void]$results.Add([pscustomobject]$o)
}
function Truth($g, $id, [bool]$ok, $note = "", $source = "") { Add-Result $g $id ($(if ($ok) { "PASS" } else { "FAIL" })) $null $null "" "" $note $source }
function Known($g, $id, [bool]$ok, $owner, $note, $source = "") { Add-Result $g $id ($(if ($ok) { "PASS" } else { "KNOWN" })) $null $null "" $owner ($(if ($ok) { "RESOLVED: $note" } else { $note })) $source }
function Info($g, $id, $v, $unit = "", $note = "") { Add-Result $g $id "INFO" $v $null $unit "" $note }

# Run one scenario; returns the log lines.
function Run-Scenario($scn, [hashtable]$envs, [int]$frames, [switch]$Shot) {
    $log = Join-Path $out "$scn.log"
    $bmp = Join-Path $out "$scn.bmp"
    Remove-Item -LiteralPath $log, $bmp, (Join-Path $out "$scn.png") -ErrorAction SilentlyContinue
    $all = @{ WFC_SMOKE_FRAMES = "$frames" } + $envs
    if ($Shot) { $all.WFC_SHOT = $bmp }
    if ($RenderData) { $all.WFC_RENDER_DATA = $RenderData }
    $saved = @{}
    foreach ($k in $all.Keys) { $saved[$k] = [Environment]::GetEnvironmentVariable($k, "Process"); [Environment]::SetEnvironmentVariable($k, [string]$all[$k], "Process") }
    try {
        $p = Start-Process -FilePath $Exe -WorkingDirectory $out -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError "$log.err"
    } finally { foreach ($k in $saved.Keys) { [Environment]::SetEnvironmentVariable($k, $saved[$k], "Process") } }
    if ($Shot -and (Test-Path $bmp)) {
        Add-Type -AssemblyName System.Drawing
        $img = [System.Drawing.Image]::FromFile($bmp)
        try { $img.Save((Join-Path $out "$scn.png"), [System.Drawing.Imaging.ImageFormat]::Png) } finally { $img.Dispose() }
        Remove-Item -LiteralPath $bmp
    }
    $lines = @(Get-Content $log) + @(Get-Content "$log.err" -ErrorAction SilentlyContinue)
    Truth "runtime" "$scn.exit_clean" ($p.ExitCode -eq 0) "exit $($p.ExitCode)"
    return ,$lines
}
function Count($lines, $pattern) { @($lines | Select-String -Pattern $pattern).Count }
function MaxNum($lines, $pattern) {   # max of the first capture group
    $m = 0.0; foreach ($x in ($lines | Select-String -Pattern $pattern)) { $v = [double]$x.Matches[0].Groups[1].Value; if ($v -gt $m) { $m = $v } }; return $m
}

# ---- 1. startup / rendering / materials -------------------------------------------------------
if ($Sections -contains "startup") {
$L = Run-Scenario "startup" @{} 120 -Shot
$g = "rendering"
$sp = $L | Select-String "wfc: shader path active: (\d+) materials, (\d+) lightmapped components, (\d+) lights, fog (\w+)" | Select-Object -First 1
Known $g "wfc_shader_path_active" ([bool]$sp) "Rendering" "WFC original-material path (else legacy fixed-function)"
if ($sp) {
    Info $g "materials_compiled" $sp.Matches[0].Groups[1].Value "mats"
    Info $g "lightmapped_components" $sp.Matches[0].Groups[2].Value "comps" "integration record: 1973"
    Info $g "lights" $sp.Matches[0].Groups[3].Value "lights"
    Truth $g "fog_on" ($sp.Matches[0].Groups[4].Value -eq "on") "HeightFog CONF" "map HeightFogComponent"
}
$fail = @($L | Select-String "wfc: material (\S+) failed to build" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
Add-Result $g "materials_failed_to_build" ($(if ($fail.Count -eq 0) { "PASS" } else { "KNOWN" })) $fail.Count 0 "mats" "Rendering" ($fail -join " ")
$texFail = @($L | Select-String "wfc: (texture|cubemap) decode failed: (.+)$" | ForEach-Object { $_.Matches[0].Groups[2].Value.Trim() } | Sort-Object -Unique)
Add-Result $g "textures_failed_to_decode" ($(if ($texFail.Count -eq 0) { "PASS" } else { "KNOWN" })) $texFail.Count 0 "tex" "Rendering" (($texFail | ForEach-Object { Split-Path $_ -Leaf }) -join " ")
$imgFail = @($L | Select-String "image: decode failed (.+)$" | ForEach-Object { Split-Path $_.Matches[0].Groups[1].Value.Trim() -Leaf } | Sort-Object -Unique)
Info $g "legacy_image_decode_failures" $imgFail.Count "img" "pre-existing (integration record): $($imgFail -join ' ')"
$lt = $L | Select-String "textures: (\d+) loaded, (\d+) failed" | Select-Object -First 1
if ($lt) { Info $g "legacy_textures_failed" $lt.Matches[0].Groups[2].Value "tex" "glTF/legacy texture loads ($($lt.Matches[0].Groups[1].Value) loaded)" }
$lm = $L | Select-String "lightmaps: (\d+)/(\d+) submeshes bound" | Select-Object -First 1
if ($lm) { Info $g "legacy_lightmap_bindings" $lm.Matches[0].Groups[1].Value "subs" "of $($lm.Matches[0].Groups[2].Value) (legacy path)" }
Truth $g "no_gl_errors" ((Count $L "\[error\]") -eq 0) (($L | Select-String "\[error\]" | Select-Object -First 3 | ForEach-Object { $_.Line }) -join " | ")
Truth $g "vertical_slice_loaded" ((Count $L "World: loaded vertical slice") -gt 0)
$wfx = $L | Select-String "weapon fx: (\d+)/(\d+) original FX textures loaded" | Select-Object -First 1
if ($wfx) { Truth "weapon_presentation" "fx_textures_loaded" ($wfx.Matches[0].Groups[1].Value -eq $wfx.Matches[0].Groups[2].Value) "$($wfx.Matches[0].Groups[1].Value)/$($wfx.Matches[0].Groups[2].Value)" }
$wmp = $L | Select-String "weapon fx: (\d+)/(\d+) mesh-particle meshes loaded" | Select-Object -First 1
if ($wmp) { Truth "weapon_presentation" "fx_meshes_loaded" ($wmp.Matches[0].Groups[1].Value -eq $wmp.Matches[0].Groups[2].Value) "$($wmp.Matches[0].Groups[1].Value)/$($wmp.Matches[0].Groups[2].Value) (shell, magazine)" }
}

# ---- 2. weapon presentation regression: sustained fire, notifies, cues, FX --------------------
if ($Sections -contains "fire") {
$L = Run-Scenario "fire" @{ WFC_AUTOFIRE = "1"; WFC_NOTIFYLOG = "1"; WFC_CUELOG = "1"; WFC_ANIMLOG = "1"; WFC_LOGEVERY = "5" } $Frames -Shot
$g = "weapon_presentation"
$fireCues = Count $L "^\S*\s*CUE \S*(GUN_ION|IONBLASTER|ION_BLASTER|SHOOT)"
$anyCue = Count $L "CUE "
Truth $g "fire_cues_play" ($anyCue -gt 0) "$anyCue cue launches ($fireCues matching Ion Blaster names)"
$fxNotifies = Count $L "NOTIFY fx "
Truth $g "fx_notifies" ($fxNotifies -gt 0) "$fxNotifies fx notifies"
Truth $g "shell_eject_notifies" ((Count $L "NOTIFY fx \S*[Ss]hell") -gt 0) "shell notifies: $(Count $L 'NOTIFY fx \S*[Ss]hell')"
Truth $g "no_unreconstructed_notifies" ((Count $L "not reconstructed") -eq 0) (($L | Select-String "notify effect (\S+) not reconstructed" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique) -join " ")
$pmax = MaxNum $L "FX particles=(\d+)"
Truth $g "fx_particles_live" ($pmax -gt 0) "max live particles $pmax"
Info $g "fx_mesh_particles_max" (MaxNum $L "FX particles=\d+ meshes=(\d+)") "meshes" "shells/magazine mesh particles"
Info $g "fx_impacts_max" (MaxNum $L "impacts=(\d+)") "impacts"
Truth $g "auto_reload_after_dump" ((Count $L "reload=1") -gt 0 -or (Count $L "Reload") -gt 0) "reload state seen while holding fire"
}

# ---- 3. reload while moving (regression: upper-body slot over locomotion) ---------------------
if ($Sections -contains "reload") {
# Walk + hold fire until the magazine empties: the auto-reload needs no key edge, so this is
# independent of the render rate (a manual R press can be lost; see scenario 3b). WFC_AUTOTURN
# makes the jog a ~3.7 m circle so the pawn never parks against the wall ~11 m ahead of the spawn.
$L = Run-Scenario "reload_moving" @{ WFC_AUTOWALK = "1"; WFC_AUTOTURN = "1.5"; WFC_AUTOFIRE = "1"; WFC_ANIMLOG = "1"; WFC_NOTIFYLOG = "1"; WFC_LOGEVERY = "3" } 1500
$g = "weapon_presentation"
$rm = @($L | Select-String "ANIM base=(Nav_Strafe\w+).*reload w=([\d.]+)" | Where-Object { [double]$_.Matches[0].Groups[2].Value -gt 0.9 })
Truth $g "reload_upper_slot_over_locomotion" ($rm.Count -gt 0) "frames with reload weight > 0.9 on a Nav_Strafe base: $($rm.Count)"
Truth $g "magazine_drop_notify" ((Count $L "NOTIFY fx \S*[Mm]ag") -gt 0) "mag notifies: $(Count $L 'NOTIFY fx \S*[Mm]ag')"
Truth $g "reload_fx_notify" ((Count $L "NOTIFY fx \S*Reload") -gt 0) "reload flare/smoke notifies: $(Count $L 'NOTIFY fx \S*Reload')"

# ---- 3b. manual reload press (WFC_AUTORELOAD presses R once, on one render frame) -------------
$L = Run-Scenario "reload_press" @{ WFC_AUTORELOAD = "1"; WFC_ANIMLOG = "1"; WFC_LOGEVERY = "3" } 300
$pressed = (Count $L "reload=1") -gt 0
Info "input_edges" "runtime_reload_press_registered" ([int]$pressed) "" "intermittent by nature (depends on whether the press frame runs a 60 Hz step); the deterministic check is wfc_fidelity input_edges.reload_press_survives_zero_step_frame"
}

# ---- 4. vehicle boost: physics AND presentation -----------------------------------------------
if ($Sections -contains "boost") {
$L = Run-Scenario "vehicle_boost" @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1"; WFC_CUELOG = "1"; WFC_ANIMLOG = "1"; WFC_LOGEVERY = "5" } 600 -Shot
$g = "boost_presentation"
$vmax = MaxNum $L "boost frame \d+ speed=([\d.]+)"
Add-Result $g "physics_speed_max" ($(if ($vmax -gt 15.5) { "PASS" } else { "FAIL" })) $vmax 50 "m/s" "" "boost held (WFC_AUTOBOOST = Sprint key in the rebuild)" "TnHoverCarSimulationBlueprint.DashSpeed 5000"
$boostCues = @($L | Select-String "CUE (\S*BOOST\S*)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
Known $g "boost_cue_played" ($boostCues.Count -gt 0) "Systems" "expected BL_VEH_OPTIMUS_PRIME.VEH_OPTIMUS_BOOST_START/LOOP/END (character.json sounds.vehicle); seen: [$($boostCues -join ' ')]; all cues during boost: $(Count $L 'CUE ')"
Known $g "boost_fx_emitted" ((MaxNum $L "FX particles=(\d+)") -gt 0) "Systems" "expected booster FX at BoostSocket_L/R, HoverBooster_* (vehicle sockets); max live particles while boosting: $(MaxNum $L 'FX particles=(\d+)'). Boost particle templates UNKNOWN (not located in extracted data)"
$vanims = @($L | Select-String "ANIM base=(\S+)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
Known $g "boost_anim_seen" ($vanims -contains "Nav_HoverToBoost_VEH") "Gameplay" "vehicle.glb Nav_HoverToBoost_VEH; base clips seen: $($vanims -join ' ')"
}

# ---- 5. transformation cues (presentation) ----------------------------------------------------
if ($Sections -contains "transform") {
$L = Run-Scenario "transform" @{ WFC_AUTOTRANSFORM = "30"; WFC_CUELOG = "1"; WFC_ANIMLOG = "1"; WFC_LOGEVERY = "5" } 1500
$g = "transform_presentation"
$tc = @($L | Select-String "CUE (\S*TRANS\S*|\S*BOT2VEH\S*|\S*VEH2BOT\S*)" | ForEach-Object { $_.Matches[0].Groups[1].Value } | Sort-Object -Unique)
$authoredTc = @($tc | Where-Object { $_ -match "OPTIMUS_BOT2VEH" }).Count -gt 0
Known $g "transform_cue_is_authored" $authoredTc "Systems" "character.json sounds.transformation BL_TRANSFORM.OPTIMUS_BOT2VEH; transform cues seen: [$($tc -join ' ')] (rebuild plays a generic gears wav via World::playSfx)"
Truth $g "transform_completes" ((Count $L "form=VEHICLE") -gt 0) "frame log reaches VEHICLE"
}

# ---- 6. fixed-camera stills for material A/B (vehicle + robot, WFC vs legacy) -----------------
if ($Sections -contains "stills") {
foreach ($v in @(@{ n = "still_vehicle_wfc"; e = @{ WFC_STARTVEHICLE = "1"; WFC_DEBUGCAM = "front" } },
                 @{ n = "still_vehicle_legacy"; e = @{ WFC_STARTVEHICLE = "1"; WFC_DEBUGCAM = "front"; WFC_LEGACYRENDER = "1" } },
                 @{ n = "still_robot_wfc"; e = @{ WFC_DEBUGCAM = "front" } })) {
    [void](Run-Scenario $v.n $v.e 60 -Shot)
}
Info "stills" "captured" (@(Get-ChildItem $out -Filter "still_*.png").Count) "png" "WFC_DEBUGCAM=front at the authored spawn (deterministic framing; lighting/time-of-frame may vary)"
}

# ---- 7. emissive colour consistency: AssetTools bake vs compiled WFC graph (no exe run) --------
# The legacy path draws AssetTools' bakes of the customization shader; the WFC path compiles the
# original graph. Both claim to be the same material, so the energon glow hue must agree.
if ($Sections -contains "materials" -and $RenderData) {
    $g = "material_consistency"
    Add-Type -AssemblyName System.Drawing
    $M = Get-Content -Raw (Join-Path $RenderData "MP_IAC_Streets\materials_glsl.json") | ConvertFrom-Json
    $tex = Join-Path (Split-Path (Split-Path $root -Parent) -Parent) "ExtractedAssets\VerticalSlice\Characters\Optimus\textures"
    if ($env:WFC_ASSETS) { $tex = Join-Path $env:WFC_ASSETS "Characters\Optimus\textures" }
    if (-not (Test-Path $tex)) { $tex = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Characters\Optimus\textures" }
    foreach ($c in @(@{ f = "vehicle"; mat = "TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST"; bake = "vehicle_0_RB_OptimusPrime_Cust2_Mat_INST_emissive.png" },
                     @{ f = "robot"; mat = "TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B"; bake = "robot_0_RB_OptimusPrime_Cust_Mat_INST_B_emissive.png" })) {
        # Bake hue: mean of bright texels.
        $b = [System.Drawing.Bitmap]::FromFile((Join-Path $tex $c.bake)); $sr = 0; $sb = 0; $n = 0
        for ($y = 0; $y -lt $b.Height; $y += 4) { for ($x = 0; $x -lt $b.Width; $x += 4) { $p = $b.GetPixel($x, $y); if (($p.R + $p.G + $p.B) -gt 150) { $sr += $p.R; $sb += $p.B; $n++ } } }
        $b.Dispose()
        $bakeBlue = $n -gt 0 -and $sb -gt $sr
        # Compiled HDR colour constants (any channel > 1): the energon glow colour.
        $hdr = @(([regex]::Matches($M.($c.mat).glsl, "vec4\(([\d.]+), ([\d.]+), ([\d.]+), 1\.0\)")) | Where-Object {
            [Math]::Max([double]$_.Groups[1].Value, [Math]::Max([double]$_.Groups[2].Value, [double]$_.Groups[3].Value)) -gt 1.0 })
        $glslBlue = @($hdr | Where-Object { [double]$_.Groups[3].Value -gt [double]$_.Groups[1].Value }).Count -gt 0
        $glslRed = @($hdr | Where-Object { [double]$_.Groups[1].Value -gt [double]$_.Groups[3].Value }).Count -gt 0
        $consts = ($hdr | ForEach-Object { "($($_.Groups[1].Value.Substring(0,[Math]::Min(5,$_.Groups[1].Value.Length))),$($_.Groups[2].Value.Substring(0,[Math]::Min(5,$_.Groups[2].Value.Length))),$($_.Groups[3].Value.Substring(0,[Math]::Min(5,$_.Groups[3].Value.Length))))" }) -join " "
        $sw = $M.($c.mat).info.switches.UseAutobotEnergonColor
        Known $g "$($c.f).energon_hue_bake_vs_compiled" (($bakeBlue -and $glslBlue -and -not $glslRed) -or (-not $bakeBlue -and $glslRed)) "Rendering" `
            "AssetTools bake $($c.bake): $(if ($bakeBlue) { 'BLUE' } else { 'RED' })-dominant ($n bright texels); compiled $($c.mat) HDR colour constants: $consts with UseAutobotEnergonColor=$sw. WFC Autobot energon reads blue; the two translations of one material disagree (energon branch of CHR_Transformer_NormSpec_Cust_E_Mat to be traced)"
    }
}

# ---- report -----------------------------------------------------------------------------------
$sum = [ordered]@{ pass = @($results | Where-Object status -eq PASS).Count; fail = @($results | Where-Object status -eq FAIL).Count
                   known = @($results | Where-Object status -eq KNOWN).Count; info = @($results | Where-Object status -eq INFO).Count; skip = 0 }
[ordered]@{ summary = $sum; exe = $Exe; renderData = $RenderData; results = $results } | ConvertTo-Json -Depth 5 | Set-Content (Join-Path $out "report.json") -Encoding UTF8
foreach ($r in $results) { if ($r.status -ne "PASS") { "  {0,-5} {1}  {2}" -f $r.status, $r.id, $r.note } }
"PROBE SUMMARY: $($sum.pass) pass, $($sum.fail) FAIL, $($sum.known) known, $($sum.info) info -> $out\report.json"
if ($sum.fail -gt 0) { exit 1 }
