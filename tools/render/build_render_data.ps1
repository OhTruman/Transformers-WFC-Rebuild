# Regenerate the WFC original-data render inputs for a map into <worktree>\work\render\<Map>.
# Reads only the shared read-only sources (Game Dump cooked packages, ExtractedAssets, AssetTools).
# Usage: powershell -ExecutionPolicy Bypass -File tools\render\build_render_data.ps1 [-Map MP_IAC_Streets]
param([string]$Map = "MP_IAC_Streets")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Definition))
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$umodel = "F:\Transformers Rebuild\AssetTools\bin\umodel\umodel_64.exe"
$cooked = "F:/Transformers Rebuild/Game Dump/TransGame/CookedXenon"
$out = Join-Path $root "work\render\$Map"
$um = Join-Path $root "work\render\umodel_out"
New-Item -ItemType Directory -Force $out, $um | Out-Null

# 1. Lightmap atlases (all LightMapTexture2D of the _LM package + the small ones cooked inline in ART).
& $umodel -export "-path=$cooked" -game=trans "-out=$um" -png "${Map}_ART_m_LM.xxx" | Out-Null
$env:PYTHONDONTWRITEBYTECODE = "1"
$inline = @((& $py (Join-Path $PSScriptRoot "build_lighting.py") --list-inline $Map) -split ' ' | Where-Object { $_ })
if ($inline.Count -gt 0) { & $umodel -export "-path=$cooked" -game=trans "-out=$um" -png "${Map}_ART_m.xxx" ($inline | ForEach-Object { "-obj=$_" }) | Out-Null }

# 2. Lightmap bindings (3 coefficients), BSP rebuilt from the cooked vertex buffer, lights, fog.
& $py (Join-Path $PSScriptRoot "build_lighting.py") $Map $out $um
if ($LASTEXITCODE -ne 0) { throw "build_lighting failed" }

# 3. Materials: original graphs -> GLSL (world + BSP + decals + Optimus robot/vehicle + Ion Blaster
#    + the original vehicle/weapon FX materials listed in fx_materials.txt).
$fx = Get-Content (Join-Path $PSScriptRoot "fx_materials.txt") | Where-Object { $_ -match '\S' }
# Canvas (HUD marker) materials: compiled with per-draw runtime parameters
$ui = Get-Content (Join-Path $PSScriptRoot "ui_materials.txt") | Where-Object { $_ -match '\S' }
& $py (Join-Path $PSScriptRoot "build_materials.py") $Map $out `
    TR_Optimus_ROBO_p.RB_OptimusPrime_Cust_Mat_INST_B TR_Optimus_ROBO_p.InteriorAlt_Energon_MAT_INST `
    TR_Optimus_VEH_p.RB_OptimusPrime_Cust2_Mat_INST TR_Optimus_VEH_p.InteriorAlt_Energon_MAT_INST `
    WEP_IonBlaster_p.WEP_IonBlaster_MATINST @fx @ui
if ($LASTEXITCODE -ne 0) { throw "build_materials failed" }


# 4. Authored map presentation: movers / rule-gated visibility and the map particle components.
& $py (Join-Path $PSScriptRoot "build_movers.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_movers failed" }
& $py (Join-Path $PSScriptRoot "build_map_fx.py") $Map $out
if ($LASTEXITCODE -ne 0) { throw "build_map_fx failed" }
Write-Host "render data -> $out"
