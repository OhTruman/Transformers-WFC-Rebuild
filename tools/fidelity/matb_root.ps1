# (B)-only render root for the material eye pass (2026-10-17): integration's render data moves during the day ((A) decals / gibs,
# Rendering's map_fx regen), while Systems' matB/render is an 08:15 copy - so before / after would differ by more than (B). This
# builds work/matb/<name>_root = integration's CURRENT render data with only the (B) entries of each map's materials_glsl.json
# replaced by the candidate's (the MATFIX_MANIFEST.json material list), so the two arms differ by exactly (B):
#   - per map folder: its files copied (small: glb / json), its sub-folders (tex, lightmaps, vbake, ...) as junctions (read only)
#   - every other top-level folder (UI_*, _ui, umodel_out) a junction
#   - materials_glsl.json = integration's, with the manifest's materials taken from matB/render where present
#   powershell -File tools/fidelity/matb_root.ps1 [-Name matB] [-Src <integration render>] [-Cand <matB/render>] [-Manifest <json>]
param([string]$Name = "matB", [string]$Src = "F:\Transformers Rebuild\Rebuild\work\render",
      [string]$Cand = "F:\Transformers Rebuild\Rebuild-Systems\work\matB\render",
      [string]$Manifest = "F:\Transformers Rebuild\Rebuild-Systems\work\matB\handoff\MATFIX_MANIFEST.json", [switch]$NoCandidate, [string]$MapFxFrom = "")   # (-NoCandidate: the same snapshot without (B) = the before arm; -MapFxFrom <root>: each map's map_fx_runtime.json from that root, e.g. a pre-regen copy matching the exe)
$ErrorActionPreference = "Stop"
$O = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) "work\matb\${Name}_root"
if (Test-Path $O) {   # junctions inside: remove them with rmdir first, never recursively through them
    Get-ChildItem $O -Recurse -Attributes ReparsePoint -Directory | Sort-Object { $_.FullName.Length } -Descending | % { cmd /c rmdir "$($_.FullName)" }
    Remove-Item $O -Recurse -Force
}
New-Item -ItemType Directory -Force $O | Out-Null
$mats = @(if ($NoCandidate) { } else { (Get-Content -Raw $Manifest | ConvertFrom-Json).materials })   # (-NoCandidate: no manifest needed - the matB handoff may be gone)
$py = "F:\Transformers Rebuild\AssetTools\bin\py\python.exe"
$merge = @"
import json, sys
src, cand, out, names = sys.argv[1], sys.argv[2], sys.argv[3], set(open(sys.argv[4], encoding='utf-8-sig').read().split())   # (utf-8-sig: Set-Content utf8 writes a BOM onto the first name - it dropped Energon_Lava_Mat on 2026-10-17)
a = json.load(open(src, encoding='utf-8')); b = json.load(open(cand, encoding='utf-8')); n = 0
for k in names:
    if k in b and k in a and a[k] != b[k]: a[k] = b[k]; n += 1
json.dump(a, open(out, 'w', encoding='utf-8'), ensure_ascii=False)
print(n)
"@
$mergePy = Join-Path $env:TEMP "matb_merge_$PID.py"; $merge | Set-Content -Encoding utf8 $mergePy
$namesFile = Join-Path $env:TEMP "matb_names_$PID.txt"; $mats -join "`n" | Set-Content -Encoding utf8 $namesFile
foreach ($d in Get-ChildItem $Src -Directory) {
    $t = Join-Path $O $d.Name
    $glsl = Join-Path $d.FullName "materials_glsl.json"
    if (-not ($d.Name -like "MP_*") -or -not (Test-Path $glsl)) { cmd /c mklink /J "$t" "$($d.FullName)" | Out-Null; continue }
    New-Item -ItemType Directory -Force $t | Out-Null
    foreach ($c in Get-ChildItem $d.FullName) {
        if ($c.PSIsContainer) { cmd /c mklink /J (Join-Path $t $c.Name) "$($c.FullName)" | Out-Null }
        elseif ($c.Name -ne "materials_glsl.json") { Copy-Item $c.FullName $t }
    }
    if ($MapFxFrom) { $mf = Join-Path (Join-Path $MapFxFrom $d.Name) "map_fx_runtime.json"; if (Test-Path $mf) { Copy-Item $mf $t -Force } }
    $cg = Join-Path (Join-Path $Cand $d.Name) "materials_glsl.json"
    if ((Test-Path $cg) -and -not $NoCandidate) { $n = & $py -I $mergePy $glsl $cg (Join-Path $t "materials_glsl.json") $namesFile; "$($d.Name): $n (B) entries taken from the candidate" }
    else { Copy-Item $glsl $t; "$($d.Name): no candidate file - integration's copied" }
}
Get-ChildItem $Src -File | % { Copy-Item $_.FullName $O }
Remove-Item $mergePy, $namesFile
"root: $O"
