# Regenerate the WFC original-data render inputs for a map into <worktree>\work\render\<Map>.
# Reads only the shared read-only sources (Game Dump cooked packages, ExtractedAssets, AssetTools).
# Usage: powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1 [-Map MP_IAC_Streets]
# -Map Standard: the per-worktree standard set - MP_IAC_Streets plus the five frontend scene families the menus load
# (loadFrontendScene: title, customization / lobbies, campaign lobby). A tree without them shows no menu backgrounds.
param([string]$Map = "MP_IAC_Streets")
$ErrorActionPreference = "Stop"
if ($Map -eq "Standard") {
    foreach ($m in @("MP_IAC_Streets", "UI_FrontEnd", "UI_CharacterCustomization", "UI_PartyLobby", "UI_Lobby", "UI_CampaignLobby")) {
        Write-Host "== $m"
        & $PSCommandPath -Map $m
        if (-not $?) { throw "render data failed for $m" }
    }
    return
}
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition))
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$umodel = "F:\Transformers Rebuild\AssetTools\bin\umodel\umodel_64.exe"
$cooked = "F:/Transformers Rebuild/Game Dump/TransGame/CookedXenon"
$out = Join-Path $root "work\render\$Map"
$um = Join-Path $root "work\render\umodel_out"
New-Item -ItemType Directory -Force $out, $um | Out-Null

# 1. Lightmap atlases (all LightMapTexture2D of the _LM package + the small ones cooked inline in ART).
$env:PYTHONDONTWRITEBYTECODE = "1"
$lmPkgs = @((& $py -c "import sys; sys.path.insert(0, r'$PSScriptRoot'); from ue3obj import map_lm_packages; print(' '.join(map_lm_packages('$Map')))") -split ' ' | Where-Object { $_ })
foreach ($lp in $lmPkgs) { & $umodel -export "-path=$cooked" -game=trans "-out=$um" -png $lp | Out-Null }
$levelPkgs = @((& $py -c "import sys; sys.path.insert(0, r'$PSScriptRoot'); from ue3obj import map_packages; print(' '.join(map_packages('$Map')[0]))") -split ' ' | Where-Object { $_ })
$env:PYTHONDONTWRITEBYTECODE = "1"
$inline = @((& $py (Join-Path $PSScriptRoot "build_lighting.py") --list-inline $Map) -split ' ' | Where-Object { $_ })
if ($inline.Count -gt 0) { foreach ($pk in $levelPkgs) { & $umodel -export "-path=$cooked" -game=trans "-out=$um" -png $pk ($inline | ForEach-Object { "-obj=$_" }) | Out-Null } }

# 2. Lightmap bindings (3 coefficients), BSP rebuilt from the cooked vertex buffer, lights, fog.
& $py (Join-Path $PSScriptRoot "build_lighting.py") $Map $out $um
if ($LASTEXITCODE -ne 0) { throw "build_lighting failed" }

# 3. Materials: original graphs -> GLSL (world + BSP + decals + Optimus robot/vehicle + Ion Blaster
#    + the original vehicle/weapon FX materials listed in fx_materials.txt).
$fx = Get-Content (Join-Path $PSScriptRoot "fx_materials.txt") | Where-Object { $_ -match '\S' }
# Character materials: every MP chassis (robot + vehicle) from the AssetTools roster (character_materials.py),
# not an Optimus-specific list; only materials cooked into this map compile.
$chars = @((& $py (Join-Path $PSScriptRoot "character_materials.py")) | Where-Object { $_ -match '\S' })
# Scene actors not in world.glb (render_index skeletal actors, e.g. the frontend vignette ships)
$scene = @((& $py (Join-Path $PSScriptRoot "scene_materials.py") $Map) | Where-Object { $_ -match '\S' })
# Canvas (HUD marker) materials: compiled with per-draw runtime parameters
$ui = Get-Content (Join-Path $PSScriptRoot "ui_materials.txt") | Where-Object { $_ -match '\S' }
# Particle template library (every ParticleSystem cooked into the map packages: weapon muzzle / tracer / impact FX
# spawned at runtime by IRenderer::spawnParticleEffect)
# Every exported weapon's mesh materials (weapon_materials.py; M42: held weapons other than the Ion Blaster were
# never compiled and drew the glTF fallback)
$weapons = @((& $py (Join-Path $PSScriptRoot "weapon_materials.py")) | Where-Object { $_ -match '\S' })
$fxlib = @((& $py (Join-Path $PSScriptRoot "build_map_fx.py") --list-materials $Map) | Where-Object { $_ -match '^\S+\.\S+$' })
& $py (Join-Path $PSScriptRoot "build_materials.py") $Map $out `
    @chars WEP_IonBlaster_p.WEP_IonBlaster_MATINST @weapons @fx @ui @scene @fxlib
if ($LASTEXITCODE -ne 0) { throw "build_materials failed" }


# 4. Authored map presentation: movers / rule-gated visibility and the map particle components.
& $py (Join-Path $PSScriptRoot "build_movers.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_movers failed" }
& $py (Join-Path $PSScriptRoot "build_map_fx.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_map_fx failed" }
# [integration M06] Runtime render index for maps that ship only the AssetTools generic index (all but Streets).
& $py (Join-Path $PSScriptRoot "build_render_index.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_render_index failed" }

# Matinee float-property tracks (FOVAngle / DrawScale keys) for the frontend scene families.
& $py (Join-Path $PSScriptRoot "build_scene_floatprops.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_scene_floatprops failed" }
# 5. Map-independent HUD data: Canvas fonts + objective-marker setups -> <render root>\_ui
& $py (Join-Path $PSScriptRoot "build_hud.py") (Split-Path -Parent $out)
if ($LASTEXITCODE -ne 0) { throw "build_hud failed" }
& $py (Join-Path $PSScriptRoot "build_anim_choosers.py") (Split-Path -Parent $out)
if ($LASTEXITCODE -ne 0) { throw "build_anim_choosers failed" }
Write-Host "render data -> $out"
