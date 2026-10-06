# PROP CAPTURE (tier TARGETED): look at every placement of the props matching -Mesh on a map, from fixed cameras.
# For "a human saw something wrong over there": the placements come from the map's props.json (AssetTools export),
# glTF = UE (X, Z, Y) / 100; each camera sits -Back m behind (UE -X) and -Up m above the prop, looking at it. Direct boot
# in -Mode (TDM by default, so objective actors are hidden as in the reported match). Reports the renderer's material
# fallbacks for the props' materials and a contact sheet.
#
#   .\tools\fidelity\prop-capture.ps1 -Root work\ab\<target> -Map MP_ORB_Debris -Mesh 'DeadSoldier|DeadCarSoldier' -OutDir <dir> [-Max 12]
param([Parameter(Mandatory)][string]$Root, [Parameter(Mandatory)][string]$Map, [Parameter(Mandatory)][string]$Mesh, [Parameter(Mandatory)][string]$OutDir,
      [int]$Max = 12, [double]$Back = 4.0, [double]$Up = 2.0, [string]$Mode = "TDM", [switch]$ReportOnly)
$ErrorActionPreference = "Continue"
. (Join-Path $PSScriptRoot "lib\Run.ps1"); . (Join-Path $PSScriptRoot "lib\Shots.ps1"); . (Join-Path $PSScriptRoot "lib\M07.ps1")
Add-Type -AssemblyName System.Drawing
$Root = (Resolve-Path $Root).Path; New-Item -ItemType Directory -Force $OutDir | Out-Null; $OutDir = (Resolve-Path $OutDir).Path
$exe = Join-Path $Root "build-release\bin\wfc_rebuild.exe"
$props = Get-Content -Raw "F:\Transformers Rebuild\ExtractedAssets\VerticalSlice\Maps\$Map\props.json" | ConvertFrom-Json
$items = @(if ($props -is [array]) { $props } elseif ($props.props) { $props.props } else { $props.PSObject.Properties | ForEach-Object { $_.Value } | Where-Object { $_ -is [array] } | Select-Object -First 1 })
$hits = @($items | Where-Object { "$($_.mesh)" -match $Mesh -and $_.gltf_matrix } | Select-Object -First $Max)
"{0} placements of /{1}/ on {2} (capturing {3})" -f @($items | Where-Object { "$($_.mesh)" -match $Mesh }).Count, $Mesh, $Map, $hits.Count
$shots = @(); $meta = @()
for ($i = 0; $i -lt $hits.Count; $i++) {
    $m = $hits[$i].gltf_matrix; $x = [double]$m[12]; $y = [double]$m[13]; $z = [double]$m[14]
    $name = "p{0:D2}" -f $i
    $shots += @{ name = $name; c = @(($x - $Back), ($y + $Up), $z); t = @($x, ($y + 0.5), $z) }
    $meta += [pscustomobject]@{ shot = $name; mesh = ($hits[$i].mesh -replace '^.*\.', ''); actor = $hits[$i].actor; component = ($hits[$i].component -replace '^.*PersistentLevel\.', '')
        ue = "{0:N0}, {1:N0}, {2:N0}" -f $hits[$i].ue_matrix[3][0], $hits[$i].ue_matrix[3][1], $hits[$i].ue_matrix[3][2]; materials = (@($hits[$i].section_materials) -join " ") }
}
# warm-up views first (load / shader compile), then the props
$warm = @(); if ($shots.Count) { for ($w = 0; $w -lt 4; $w++) { $warm += @{ name = "warm$w"; c = $shots[0].c; t = $shots[0].t } } }
if (-not $ReportOnly -and $shots.Count) {
    if (-not (Wait-WfcGpu)) { "GPU busy - not run"; return }
    $null = Invoke-ShotList $exe $OutDir ($warm + $shots) @{ WFC_BOOT = "match"; WFC_MAP = $Map; WFC_GAMEMODE = $Mode; WFC_VISUALCHECK = "1" } "" @($shots | ForEach-Object { $_.name })
}
$log = Join-Path $OutDir "wfc.log"
$mats = @($meta | ForEach-Object { $_.materials -split ' ' } | Where-Object { $_ } | Select-Object -Unique)
$fb = if (Test-Path $log) { @(Select-String $log -Pattern 'material (\S+) failed to build; using glTF fallback' | ForEach-Object { $_.Matches[0].Groups[1].Value } | Select-Object -Unique) } else { @() }
$meta | Export-Csv -NoTypeInformation -Encoding UTF8 (Join-Path $OutDir "props.csv")
$tiles = @($meta | ForEach-Object { $mt = $_; $img = @("bmp", "jpg" | ForEach-Object { Join-Path $OutDir "$($mt.shot).$_" } | Where-Object { Test-Path $_ })[0]; if ($img) { @{ png = $img; label = "$($mt.shot) $($mt.mesh) UE($($mt.ue))" } } })
if ($tiles.Count) { New-WfcSheet $tiles (Join-Path $OutDir "sheet_props.png") 3 480 270 }
"props' materials: {0}" -f ($mats -join ", ")
"material build fallbacks in this run: {0}" -f $(if ($fb.Count) { $fb -join ", " } else { "none" })
"props' materials that fell back: {0}" -f $(if (@($fb | Where-Object { $mats -contains $_ }).Count) { (@($fb | Where-Object { $mats -contains $_ }) -join ", ") } else { "none" })
