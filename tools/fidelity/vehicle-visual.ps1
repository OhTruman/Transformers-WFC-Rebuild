# Vehicle visual A/B harness: fixed-camera / fixed-frame screenshots of the vehicle in every state,
# each with a material sidecar, on the deterministic lockstep exe (wfc_rebuild_observe).
#
#   .\tools\fidelity\vehicle-visual.ps1                         # -> work\fidelity\vehicle_visual\
#   .\tools\fidelity\vehicle-visual.ps1 -Scenarios idle_front34,boost
#
# Scenarios: idle_front34, idle_rear34, idle_side, hover_fx, boost, dash, nitro, transform_mid,
# dark_area, bright_area (darkest / brightest of the probed FFA spawns by mean luminance).
# Variants (idle angles): wfc (compiled original materials), legacy (WFC_LEGACYRENDER, GL1 path),
# bakes (WFC_GLTFMATERIALS: AssetTools glTF bakes through the WFC path) - same frame, same camera.
# Sidecar materials.json (per run): vehicle mesh material slots (vehicle.glb) -> resolved material
# instance + parent chain + master, lighting model/blend, textures by role (normal / mask / emissive /
# spec / diffuse / cubemap), active static switches, runtime parameters (Cust_Color_A/B,
# EnergonColor) and whether the game pushes customization colours (setCharacterColors call site).
# Output: <scenario>/<variant>.png, sheet.png, materials.json, report.json. No shader/material edits.
param([string[]]$Scenarios = @("idle_front34", "idle_rear34", "idle_side", "hover_fx", "boost", "dash", "nitro", "transform_mid", "dark_area", "bright_area"),
      [string]$Exe = "", [string]$OutDir = "", [string]$RenderData = "", [string]$SourceRoot = "", [int[]]$ProbeSpawns = @(0, 3, 6, 9, 12, 15, 18, 21),
      # Scene choices pinned to the milestone-02 probe so every tree films the same places (-Probe re-measures):
      # open run for motion states = spawn 18; darkest / brightest of the probed FFA spawns = 3 / 18.
      [int]$OpenSpawn = 18, [int]$DarkSpawn = 3, [int]$BrightSpawn = 18, [switch]$Probe)
$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "lib\Run.ps1")
$root = Get-WfcRoot
if (-not $Exe) { $Exe = Join-Path $root "build\bin\wfc_rebuild_observe.exe" }
if (-not $OutDir) { $OutDir = Join-Path $root "work\fidelity\vehicle_visual" }
if (-not $RenderData) { $RenderData = Join-Path $root "work\render\MP_IAC_Streets" }
if (-not $SourceRoot) { $SourceRoot = $root }
if (-not (Test-Path $Exe)) { throw "measurement build missing: $Exe" }
Add-Type -ReferencedAssemblies System.Drawing -Path (Join-Path $PSScriptRoot "lib\ImageStats.cs") -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.Web.Extensions
$ser = New-Object System.Web.Script.Serialization.JavaScriptSerializer; $ser.MaxJsonLength = [int]::MaxValue
New-Item -ItemType Directory -Force $OutDir | Out-Null

# ---- material sidecar (static per tree) ----
function GlbJson($path) {
    $b = [IO.File]::ReadAllBytes($path); $len = [BitConverter]::ToUInt32($b, 12)
    $ser.DeserializeObject([Text.Encoding]::UTF8.GetString($b, 20, $len))
}
$assets = "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Characters\Optimus"
$vg = GlbJson (Join-Path $assets "vehicle.glb")
$mats = $ser.DeserializeObject([IO.File]::ReadAllText((Join-Path $RenderData "materials_glsl.json"))); if ($mats["materials"]) { $mats = $mats["materials"] }
$worldSrc = Join-Path $SourceRoot "src\game\World.cpp"
$pushes = (Test-Path $worldSrc) -and ((Get-Content -Raw $worldSrc) -match "setCharacterColors\s*\(")
$slots = @()
foreach ($gm in $vg["materials"]) {
    $nm = $gm["name"]
    # Same-named instances exist in the robot and vehicle packages: prefer the vehicle package.
    $cands = @($mats.Keys | Where-Object { $_ -eq $nm -or $_.EndsWith("." + $nm) -or $_ -like "TR_Optimus_VEH_p.*$nm*" })
    $key = @($cands | Sort-Object { if ($_ -like "TR_Optimus_VEH_p.*") { 0 } else { 1 } }) | Select-Object -First 1
    $e = if ($key) { $mats[$key] } else { $null }
    $info = if ($e) { $e["info"] } else { $null }
    $tex = [ordered]@{}
    if ($info) {
        foreach ($t in $info["textures"]) {
            $o = [string]$t["object"]
            $role = if ($t["kind"] -eq "cube" -or $t["class"] -eq "TextureCube") { "cubemap" } elseif ($o -match "Norm") { "normal" } elseif ($o -match "Mask|MSK") { "mask" } elseif ($o -match "Emis|Glow") { "emissive" } elseif ($o -match "Spec") { "specular" } elseif ($o -match "Diff|CLR|Color") { "diffuse" } else { "other" }
            if (-not $tex[$role]) { $tex[$role] = @() }; $tex[$role] += $o
        }
    }
    $slots += [ordered]@{
        glb_material = $nm; resolved = $key; compiled = [bool]($e -and $e["glsl"] -and -not $e["error"]); error = $(if ($e) { $e["error"] } else { "not in materials_glsl.json" })
        chain = $(if ($info) { $info["chain"] } else { @() }); master = $(if ($info) { $info["master"] } else { "" })
        lighting_model = $(if ($info) { $info["lighting_model"] } else { "" }); blend = $(if ($info) { $info["blend_mode"] } else { "" })
        connected = $(if ($info) { $info["connected"] } else { @() }); textures = $tex
        switches_on = $(if ($info) { @($info["switches"].Keys | Where-Object { $info["switches"][$_] }) } else { @() })
        runtime_params = $(if ($info) { $info["runtime_params"] } else { $null })
        energon_color = $(if ($info -and $info["runtime_params"] -and $info["runtime_params"]["EnergonColor"]) { $info["runtime_params"]["EnergonColor"] } else { "compiled default (no runtime override)" })
    }
}
$sidecar = [ordered]@{ vehicle_glb_materials = $vg["materials"].Count; game_pushes_customization = $pushes
                       customization_note = $(if ($pushes) { "World.cpp calls setCharacterColors (TnCharacterApplier colours applied)" } else { "no setCharacterColors call in World.cpp: Cust_Color_A/B and EnergonColor keep the compiled defaults" })
                       slots = $slots }
$sidecar | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $OutDir "materials.json")

# ---- cameras (spawn-relative; same convention as stills.ps1) ----
$spawn = @(363.5, -724.48, -341.8); $yaw = 1.01
$fwd = @(-[Math]::Sin($yaw), 0, -[Math]::Cos($yaw)); $right = @(-$fwd[2], 0, $fwd[0])
function Cam([double[]]$o, [double]$lookUp) {
    $c = @(($spawn[0] + $fwd[0] * $o[0] + $right[0] * $o[1]), ($spawn[1] + $o[2]), ($spawn[2] + $fwd[2] * $o[0] + $right[2] * $o[1]))
    $d = @(($spawn[0] - $c[0]), ($spawn[1] + $lookUp - $c[1]), ($spawn[2] - $c[2]))
    $len = [Math]::Sqrt($d[0] * $d[0] + $d[1] * $d[1] + $d[2] * $d[2])
    "{0:F3},{1:F3},{2:F3},{3:F4},{4:F4}" -f $c[0], $c[1], $c[2], [Math]::Atan2(-$d[0], -$d[2]), [Math]::Asin($d[1] / $len)
}
function Shot($dir, $name, [hashtable]$envs, [int]$frames) {
    $bmp = Join-Path $dir "$name.bmp"
    $all = @{ WFC_SMOKE_FRAMES = "$frames"; WFC_SHOT = $bmp; WFC_LOGEVERY = "1"; WFC_NOMOUSE = "1"; WFC_BOOSTLOG = "1"; WFC_DEBUGSTATE = (Join-Path $dir "$name.debug.txt") } + $envs
    $rc = Invoke-WfcExe $Exe $dir $all "$name.log"
    Move-Item -Force (Join-Path $dir "wfc.log") (Join-Path $dir "$name.wfc.log") -ErrorAction SilentlyContinue
    $L = (Read-WfcFrames (Join-Path $dir "$name.wfc.log")) | Where-Object frame -eq $frames | Select-Object -First 1
    $vfx = Select-String -Path (Join-Path $dir "$name.wfc.log") -Pattern "VFX boost=(\d) .*parts=(\d+)" | Select-Object -Last 1
    $png = if (Test-Path $bmp) { Convert-WfcBmp $bmp } else { $null }
    $luma = if ($png) { $g = [WfcImage]::Luma($png, 16); ($g[2..($g.Length - 1)] | Measure-Object -Average).Average } else { -1 }
    $lab = "$name"
    if ($L) { $lab = "{0} f{1} {2} {3} spd={4:F1} drv={5} dash={6:F2} nitro={7:F2}" -f $name, $frames, $L.form, $L.anim, $L.hspeed, $L.drv, $L.dash, $L.nitro }
    if ($vfx) { $lab += " vfxParts=" + $vfx.Matches[0].Groups[2].Value }
    return @{ png = $png; label = $lab; rc = $rc; luma = $luma; state = $L }
}
$idle = @{ idle_front34 = @(6.5, 6.5, 4.0); idle_rear34 = @(-6.5, 6.5, 4.0); idle_side = @(0, 9, 2.0) }
$res = New-WfcResults
$allTiles = @()
foreach ($name in $Scenarios) {
    $dir = Join-Path $OutDir $name; New-Item -ItemType Directory -Force $dir | Out-Null
    $tiles = @()
    switch -Wildcard ($name) {
        "idle_*" {
            $cam = Cam $idle[$name] 1.5
            foreach ($v in @(@("wfc", @{}), @("legacy", @{ WFC_LEGACYRENDER = "1" }), @("bakes", @{ WFC_GLTFMATERIALS = "1" }))) {
                $tiles += Shot $dir $v[0] (@{ WFC_STARTVEHICLE = "1"; WFC_RENDERCAM = $cam } + $v[1]) 90
            }
        }
        { $_ -in "hover_fx", "boost", "dash", "nitro" } {
            # Motion states need an open run: probe the candidate spawns once (boost for 2 s) and use the
            # spawn that reaches the highest speed.
            if (-not $Probe) { $script:open = @{ k = $OpenSpawn } }   # pinned (milestone-02 probe): same scene on every tree
            if (-not $script:open) {
                $pd = Join-Path $OutDir "_open_probe"; New-Item -ItemType Directory -Force $pd | Out-Null
                $best = $null
                foreach ($k in $ProbeSpawns) {
                    $t = Shot $pd "spawn$k" @{ WFC_STARTVEHICLE = "1"; WFC_SPAWN_INDEX = "$k"; WFC_AUTOWALK = "1"; WFC_AUTOBOOST = "1" } 150
                    $sp = if ($t.state) { [double]$t.state.hspeed } else { 0 }
                    if (-not $best -or $sp -gt $best.sp) { $best = @{ k = $k; sp = $sp } }
                }
                $script:open = $best
            }
            $sp = @{ WFC_STARTVEHICLE = "1"; WFC_AUTOWALK = "1"; WFC_SPAWN_INDEX = "$($script:open.k)" }
            $t = switch ($name) {
                "hover_fx" { Shot $dir "wfc" $sp 120 }
                "boost"    { Shot $dir "wfc" ($sp + @{ WFC_AUTOBOOST = "1" }) 150 }
                "dash"     { Shot $dir "wfc" ($sp + @{ WFC_AUTODASH = "100" }) 112 }
                "nitro"    { Shot $dir "wfc" ($sp + @{ WFC_AUTOBOOST = "1"; WFC_AUTODASH = "100" }) 130 }
            }
            $t.label += " spawn=$($script:open.k)"
            $tiles += $t
            Add-WfcResult $res "vehicle_visual.$name.moving" $(if ($t.state -and [double]$t.state.hspeed -gt 1) { "PASS" } else { "FAIL" }) $(if ($t.state) { $t.state.hspeed } else { 0 }) "vehicle moving at capture (m/s); a stalled vehicle invalidates the FX still"
        }
        "transform_mid" {
            $cam = Cam @(0, 12, 3) 2.0
            $tiles += Shot $dir "r2v_mid" @{ WFC_PRESSTRANSFORM = "60"; WFC_RENDERCAM = $cam } 120
            $tiles += Shot $dir "r2v_end" @{ WFC_PRESSTRANSFORM = "60"; WFC_RENDERCAM = $cam } 200
            $tiles += Shot $dir "direct_vehicle" @{ WFC_STARTVEHICLE = "1"; WFC_RENDERCAM = $cam } 200
        }
        { $_ -eq "dark_area" -or $_ -eq "bright_area" } {
            if (-not $Probe) { $script:areaProbe = @(@{ k = $DarkSpawn; luma = 0 }, @{ k = $BrightSpawn; luma = 1 }) }   # pinned
            if (-not $script:areaProbe) {
                $script:areaProbe = @()
                $pd = Join-Path $OutDir "_spawn_probe"; New-Item -ItemType Directory -Force $pd | Out-Null
                foreach ($k in $ProbeSpawns) { $t = Shot $pd "spawn$k" @{ WFC_STARTVEHICLE = "1"; WFC_SPAWN_INDEX = "$k" } 60; $t.k = $k; $script:areaProbe += $t }
            }
            $pick = if ($name -eq "dark_area") { $script:areaProbe | Sort-Object { $_.luma } | Select-Object -First 1 } else { $script:areaProbe | Sort-Object { $_.luma } -Descending | Select-Object -First 1 }
            $t = Shot $dir "wfc" @{ WFC_STARTVEHICLE = "1"; WFC_SPAWN_INDEX = "$($pick.k)" } 90
            $t.label += (" spawn={0} luma={1:F1}" -f $pick.k, $t.luma)
            $tiles += $t
        }
    }
    New-WfcSheet $tiles (Join-Path $dir "sheet.png") 3 640 360
    foreach ($t in $tiles) { $t.label = "$name/" + $t.label; $allTiles += $t }
    $ok = @($tiles | Where-Object { $_.png }).Count
    Add-WfcResult $res "vehicle_visual.$name.captured" $(if ($ok -eq $tiles.Count) { "PASS" } else { "FAIL" }) $ok ("{0}/{1} stills" -f $ok, $tiles.Count)
    foreach ($t in $tiles) {
        if ($t.state -and $name -in "boost", "nitro") { Add-WfcResult $res "vehicle_visual.$name.driving_state" $(if ($t.state.drv -eq 1) { "PASS" } else { "FAIL" }) $t.state.hspeed "vehicle in Driving at capture (speed m/s)" }
        if ($t.state -and $name -eq "dash") { Add-WfcResult $res "vehicle_visual.dash.dash_active" $(if ($t.state.dash -gt 0) { "PASS" } else { "FAIL" }) $t.state.dash "dash remaining at capture (s)" }
        if ($t.state -and $name -eq "nitro") { Add-WfcResult $res "vehicle_visual.nitro.nitro_active" $(if ($t.state.nitro -gt 0) { "PASS" } else { "FAIL" }) $t.state.nitro "nitro remaining at capture (s)" }
    }
    "{0}: {1} stills" -f $name, $tiles.Count
}
New-WfcSheet $allTiles (Join-Path $OutDir "sheet.png") 4 480 270
foreach ($s in $slots) {
    Add-WfcResult $res "vehicle_visual.material.$($s.glb_material)" $(if ($s.compiled) { "PASS" } else { "KNOWN" }) $null ("resolved {0}; master {1}; switches on: {2}" -f $s.resolved, $s.master, ($s.switches_on -join ",")) $(if ($s.compiled) { "" } else { "Rendering" })
}
Add-WfcResult $res "vehicle_visual.customization_pushed" $(if ($pushes) { "PASS" } else { "KNOWN" }) ([int]$pushes) $sidecar.customization_note $(if ($pushes) { "" } else { "Gameplay/Rendering" })
$sum = Write-WfcReport $res (Join-Path $OutDir "report.json")
"VEHICLE VISUAL: {0} pass, {1} FAIL, {2} known, {3} info -> {4}" -f $sum.pass, $sum.fail, $sum.known, $sum.info, (Join-Path $OutDir "sheet.png")
